<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling the BonusClassicOption Monte Carlo

Where `rke_profile_bonusclassicoption` spends its instructions, and which improvements
carry over from this workload to a production market. Only 4.1 is implemented.

## 1. What Was Measured

| Item     | Value                                                                                                                                        |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Revision | `33eeead`; its code is that of `9b8fa24`, which the first pass measured, and every figure below was re-measured on it                        |
| Build    | `profile` preset: `-O3 -g -fno-omit-frame-pointer`, Homebrew clang 23.1.2, C++17; a second `profile` tree with `-DBUILD_SHARED_LIBS=OFF`     |
| Machine  | WSL2 laptop, no hardware counters                                                                                                            |
| Tools    | valgrind 3.27.1 (callgrind, cachegrind, DHAT, massif), `perf` in `cpu-clock` mode, google-benchmark from the submodule                       |
| Runs     | `discrete 1` and `continuous 1`, shared and static; one repricing, since callgrind counts exactly. Prototypes of 4.1 and 4.2 under callgrind |

The workload is `makeBonusClassicOptionSetup` in `src/rke/common/BonusClassicOptionSetup.cpp`:

- **Option:** barrier 90, bonus level 120, maturity 22 Jun 2026, evaluation date 22 Jun 2025.
- **Market:** `BlackScholesMertonProcess`, spot 100. Rates and volatility are flat:
  `FlatForward` risk-free rate 1% and dividend yield 3%, continuously compounded;
  `BlackConstantVol` at 20%, `NullCalendar`. Day count `Actual360` throughout.
- **Grid and paths:** 255 steps, from 252 steps per year over the Actual360 year fraction 365/360.
  2^16 Sobol paths with Joe-Kuo D7 directions, seed 42, Brownian bridge on.

Instruction counts (Ir) are the evidence. Wall-clock times on this machine are
secondary, and are quoted only with the same-session comparison next to them.

## 2. Where the Instructions Go

One repricing costs 34.18 G Ir discrete and 39.00 G Ir continuous.
`GeneralizedBlackScholesProcess::evolve` accounts for 32.38 G of that, or 94.8% of
discrete. It runs 16,711,680 times (65,536 paths × 255 steps), at 1,938 Ir per call:

| Callee of `evolve`                    | Ir per step | Contents                                                                                                                               |
| ------------------------------------- | ----------: | -------------------------------------------------------------------------------------------------------------------------------------- |
| `YieldTermStructure::forwardRate` × 2 |       1,072 | 4 `discount` (each `checkRange`, `LazyObject::calculate`, `exp`), 2 `impliedRate` (each `log`, an `InterestRate`, a `DayCounter` copy) |
| `variance`                            |         644 | 2 `blackVariance`, each with `checkRange` → `maxTime` (242 Ir per step) and `checkStrike`                                              |
| `apply`                               |          69 | one `exp`                                                                                                                              |
| rest                                  |         153 | `Handle::operator->`, `localVolatility()` "trigger update", own code                                                                   |

Everything else is small:

| Part                                                   |     Ir | Share |
| ------------------------------------------------------ | -----: | ----: |
| Sobol plus `InverseCumulativeNormal`, about 55 Ir/draw | 0.92 G |  2.7% |
| `BrownianBridge::transform`                            | 0.42 G |  1.2% |
| `BiasedBonusClassicPathPricer`                         | 0.05 G | 0.15% |
| `GeneralStatistics::add`                               |  2.2 M |   ≈ 0 |
| Per-pricing setup: Sobol tables, bridge, `TimeGrid`    |  < 1 M |   ≈ 0 |

In continuous mode `BonusClassicPathPricer` adds 4.87 G Ir. Of that, 3.86 G is
`variance()`: 6.0 M calls at 644 Ir, one per step of each of the 23,535 paths (36%)
that never touch the barrier on a grid point.

## 3. Which Branch of `evolve` Production Takes

`GeneralizedBlackScholesProcess` has two paths, and this workload exercises only one of them.

- **Strike-independent volatility** (`BlackConstantVol`, `BlackVarianceCurve`): this is
  the branch profiled above. `evolve` takes the exact lognormal step from
  `variance(t0, x0, dt)` and the forward rates over `[t0, t0 + dt]`. Neither depends on
  the path.
- **A smile** (`BlackVarianceSurface` and the like): `localVolatility()` links a
  `LocalVolSurface`, and `evolve` takes the Euler branch through `EulerDiscretization`.
  - `drift(t, x)` calls `diffusion(t, x)` and two `forwardRate(t, t + 1e-4)`.
  - `stdDeviation` calls `diffusion(t, x)` a second time, with the same arguments.
  - Each `LocalVolSurface::localVolImpl` makes 5 `blackVariance` calls on the surface,
    up to 6 `discount` calls, a `log` and two `exp`.

  The volatility now depends on the path's level, so it cannot be precomputed per step.

A production market has interpolated curves and a smile, so it takes the second branch.
Its per-step cost is expected to be well above the 1,938 Ir measured here, but that is
unmeasured: this profile never executed `localVolImpl`. The findings below are kept only
where they hold in that branch as well.

## 4. Candidates That Carry Over to Production

Ranked by Ir saved per repricing on this workload. The prototypes are compiled into one
binary together with the unchanged engine, and each saving is taken against that engine:
34.05 G discrete and 38.83 G continuous, 0.3–0.4% below the profile executable's count for
the same pricing.

### 4.1 Precompute Path-Independent Step Terms Once per Grid

- **Status:** implemented, for the strike-independent branch only. On the `profile` preset,
  one repricing drops from 34.18 G to 2.68 G Ir discrete (92.15%) and from 39.00 G to 3.58 G
  continuous (90.81%), with the production NPVs unchanged to the last digit printed. The
  prototype measured 92.4% and 91.0% against its own engine build.
- **Lives in:** `BlackScholesStepCache` and `BlackScholesPathGenerator` in
  `src/rke/ql/ext/methods/montecarlo/`. `MCBonusClassicEngine` takes its paths from them, and
  `BonusClassicPathPricer` takes its step variance from the cache.
- **Branch guard:** the cache classifies the volatility by type, then checks its step bitwise
  against `evolve` at two points on every step. That catches the two cases the type misses: a
  forced discretization, and a process built with an external local volatility. Under a smile,
  or when the check fails, every step goes through `evolve` as before. The smile branch's
  rate-only cache is not implemented.
- **Finding:** every path recomputes per-step quantities that depend only on the time
  grid. That holds for any curve shape, not just flat ones.
- **Strike-independent branch:** variance, its square root, and the drift
  `(r_fwd − q_fwd)·dt − ½·var` are all per-step constants.
- **Smile branch:** the rate part `r_fwd − q_fwd` of `GeneralizedBlackScholesProcess::drift`
  is still a per-step constant. That is two `forwardRate` calls per step, 1,072 Ir on
  this setup's flat curves and more on interpolated ones. Only `localVol(t, x)` stays
  per path.
- **Evidence:** a prototype that reproduces `MonteCarloModel::addSamples` with the
  strike-independent terms cached:

  | Mode       |     Engine |    Cached | Saving |
  | ---------- | ---------: | --------: | -----: |
  | discrete   | 34.05 G Ir | 2.60 G Ir |  92.4% |
  | continuous | 38.83 G Ir | 3.49 G Ir |  91.0% |

  The continuous figure includes caching the pricer's `variance()` per step. These
  percentages belong to this workload's branch. With a smile, the saving is at least
  the rate terms' absolute cost, but its share of the run is unmeasured.

- **Lock impact:** bit-identical. The cached doubles are the ones `evolve` computes.
  The prototype reproduces, to the IEEE bit pattern:
  - the production setup: discrete `100.22671167047309` and continuous
    `99.883605619178169`;
  - both locks in `src/rke/testsuite/BonusClassicOption.cpp`: discrete
    `106.96041418042263` and continuous `105.88329042929441`.

  In the smile branch, caching `r − q` keeps QuantLib's evaluation order
  `(r − q) − ½σ²`, so it should stay bit-identical as well. That follows from the source;
  no run checked it, and none can until a smile market is profiled.

- **Design constraint:** the branch decision rests on
  `GeneralizedBlackScholesProcess`'s private `isStrikeIndependent_`,
  `forceDiscretization_` and `hasExternalLocalVol_`. Code outside QuantLib cannot read
  any of them.
  - An rke implementation has to re-derive the decision from the volatility's type, as
    `localVolatility()` does. It has to fall back to `evolve` for anything it cannot
    classify, and it cannot see `forceDiscretization`.
  - The path generator type is fixed by `McSimulation`'s `MC` template parameter, so a
    custom generator comes in through our own Monte Carlo traits, not through
    `SingleVariate`.

### 4.2 Compute the Forward Rate Without `InterestRate` in the Process

- **Status:** verified by prototype, at the low end of the first pass's estimate of
  120–160 Ir per call, 4.0–5.3 G Ir and 12–16%.
- **Lives in:** QuantLib fork, `GeneralizedBlackScholesProcess::evolve`, `drift` and
  `expectation` in `ql/processes/blackscholesprocess.cpp`.
- **Finding:** each call takes `forwardRate(t1, t2, Continuous, NoFrequency, true).rate()`.
  That builds an `InterestRate` through `InterestRate::impliedRate`: a `DayCounter`
  copy, a refcount round-trip and the checks that go with it. All of it is thrown away
  except one double.
  Computing `std::log(discount(t1, true) / discount(t2, true)) / (t2 - t1)` directly
  does the same arithmetic as `impliedRate`'s continuous branch. The `compound == 1`
  case also gives 0.
- **Branches:** this applies to both. The exact branch calls it in `evolve`, the smile
  branch in `drift`, two calls per step either way.
- **Evidence:** `forwardRate` costs 536 Ir per call here. `impliedRate` takes 133 of
  that, of which `log` is 49; `dayCounter` takes 11 and `forwardRate`'s own code 66.
  The prototype overrides `evolve` in a subclass of `BlackScholesMertonProcess` with the
  direct formula, so QuantLib stays untouched. It saves 124 Ir per call: 4.13 G Ir, or
  12.1%, discrete, and the same 4.13 G, or 10.6%, continuous. The subclass lives in the
  executable rather than in `libQuantLib`, which leaves the calls it removes the same.
- **Overlap with 4.1:** with 4.1 in place, the forward rates run once per grid point
  instead of once per path step, in either branch, so 4.2 adds almost nothing to this
  engine. It pays where 4.1 does not reach: every other engine that evolves this process.
- **Lock impact:** bit-identical, all four values above, to the IEEE bit pattern.
- **Upstream:** medium. The change is local and behaviour-preserving, but it duplicates
  the core of `YieldTermStructure::forwardRate` in the process.

### 4.3 Link QuantLib Statically

- **Status:** measured, and corrected from the first pass's estimate of about 2% Ir.
- **Lives in:** setup, `external/CMakeLists.txt`. QuantLib's own CMake sets
  `BUILD_SHARED_LIBS` to `UNIX` when it is undefined, and we leave it undefined.
- **Finding:** `libQuantLib.so` is built with `-fPIC`, so calls between QuantLib
  functions go through the PLT. Cachegrind puts 17.7% of Ir in an unsymbolized bucket
  that holds the PLT stubs together with libm's `exp` and `log` kernels. Callgrind folds
  the stubs into their callers, so the first pass's count of about 48 PLT jumps per step
  could not be reproduced; the static build measures their effect directly.
- **Evidence:** a second `profile` tree configured with `-DBUILD_SHARED_LIBS=OFF` runs
  33.02 G Ir discrete and 37.69 G continuous. That saves 1.16 G, or 3.4%, and 1.32 G,
  or 3.4%: about 69 Ir per step discrete. The PLT traffic grows with the number of QuantLib calls
  per step, so the smile branch has more of it. Wall clock is in section 7.
- **Lock impact:** bit-identical. The production setup prints the same NPVs, and the
  prototype linked against the static library reproduces all four values above.

### 4.4 Refcount Traffic Ir Does Not Show

- **Status:** the count is verified exactly; its cycle cost is unverifiable here.
- **Not a separate change.** This is the reason 4.2 is worth more than its Ir.
- **Finding:** one repricing executes 200,540,525 lock-prefixed instructions, 12.0 per
  step, counted from callgrind's per-instruction dump against the disassembly. They lock
  even in a single-threaded run. Two sources, both `boost::shared_ptr` copies of the
  term structure's `DayCounter`:
  - 8 per step on the `forwardRate` path: the copy `TermStructure::dayCounter()` returns
    and its release, and the copy `impliedRate` puts into the `InterestRate` and its
    release in `evolve`, for each of the two calls. 4.2 removes these, 134 M per repricing.
  - 4 per step on `blackVariance` → `checkRange` → `maxTime`, which copies the day
    counter once per call. 4.2 does not touch them.
- **Evidence:** callgrind counts each as one instruction, which puts
  `TermStructure::dayCounter` at 2.15% of Ir. A `perf` pass in `cpu-clock` mode puts it
  at 14.4% of 19,857 samples instead. At about 20 cycles each, these operations could
  account for up to roughly 4 G cycles per repricing, 2.7 G of it in 4.2's share. That is
  an upper bound; confirming it needs `perf stat -e cycles` before and after 4.2 on a
  machine with hardware counters, which WSL2 lacks.
- **Coverage:** the `forwardRate` copies happen in both branches. The `maxTime` copies
  happen only where `checkRange` runs without `extrapolate`; `LocalVolSurface` passes
  `true`, so under a smile they drop out.

## 5. Findings Dropped as Specific to This Market

| Finding                                                                                    | Measured here                               | Why it does not carry over                                                                                                                      |
| ------------------------------------------------------------------------------------------ | ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Pass `extrapolate = true` to `blackVariance` in `GeneralizedBlackScholesProcess::variance` | 4.35 G Ir discrete (12.7%), bit-identical   | Only the strike-independent branch calls it. Under a smile, `variance` goes to the discretization, and `LocalVolSurface` already passes `true`. |
| Cache the continuous pricer's per-step `variance()`                                        | 3.86 G Ir continuous (9.9%)                 | With a smile the variance depends on `path[i]`, and the pricer's own header warns the bridge is approximate there.                              |
| Drop the second `localVolatility()` "trigger update" per step                              | 0.37 G Ir (1.1%)                            | Insignificant next to `localVolImpl`.                                                                                                           |
| Generate paths in log space and call `exp` only where a price is needed                    | about 0.95 G Ir after 4.1, moves both locks | `localVol(t, x)` needs the price level every step, and `localVolImpl` takes its own `log`.                                                      |
| The 92% headline of 4.1                                                                    | see 4.1                                     | It is the strike-independent branch's figure; 4.1 keeps only the part that carries over.                                                        |

## 6. What the Profile Rules Out

These hold for the branch measured. A smile-surface profile has to confirm them for the other.

- **Allocation:** one repricing allocates 119 blocks and 2.22 MB, nothing per path or
  per step (DHAT, 2-iteration minus 1-iteration run). 94.6% of the bytes are the doubling
  growth of `GeneralStatistics`' sample vector. Peak heap is 1.73 MB (massif).
  Allocation is below 1e-4 of Ir.
- **Memory and branches:** cachegrind shows D1 misses at 0.008% of data references, LL
  misses near zero, and conditional mispredicts at 0.68%. Its 11.2% indirect mispredict
  rate comes from the simulator's 512-entry predictor aliasing. The loop is
  instruction-bound.
- **Monte Carlo machinery:** RNG, bridge, pricer, statistics and setup together cost
  about 4%. A different inverse normal, dropping the bridge, or `IncrementalStatistics`
  cannot gain more than that, and less under the heavier smile branch.

## 7. Wall Clock (Secondary)

`rke_benchmark_bonusclassicoption --benchmark_repetitions=5` from the `release` preset and
from a `release` tree with `-DBUILD_SHARED_LIBS=OFF`, run back to back in one session with
nothing else running, in the order shown. Mean per repricing, with its cv:

| Build          |            Discrete |          Continuous |
| -------------- | ------------------: | ------------------: |
| shared, first  | 2,088 ms (cv 0.40%) | 2,371 ms (cv 0.52%) |
| static         | 1,910 ms (cv 0.38%) | 2,162 ms (cv 0.83%) |
| shared, second | 2,045 ms (cv 0.44%) | 2,333 ms (cv 0.34%) |

- **Baseline:** the first pass measured 2,035 ms and 2,352 ms. The two shared runs here
  drift 2% apart, more than their cv, so that is the resolution of any comparison.
- **4.3:** static is 6.6–8.5% faster discrete and 7.3–8.8% continuous, against 3.4% fewer
  Ir. Why the time gain is twice the Ir gain cannot be settled without hardware counters.
- **4.1:** the first pass also timed its prototype natively, at 13.7×. That was not
  repeated; the Ir ratio of 13.1× is the measured figure.

## 8. Next Step

Rank for production by profiling a production-like market: an interpolated
risk-free and dividend curve and a `BlackVarianceSurface` with a smile. Two things
from the source should be measured there:

- `localVolImpl` runs twice per step with identical `(t, x)`, once from `drift` and once
  from `diffusion`.
- Its discounts and forward value depend only on `t`, so they could be cached per step.

## 9. Raw Profiles

These are local and untracked. The recipes that produce them are in `.agents/profiling.md`.

Under `build/profile/prof/rke_profile_bonusclassicoption/`:

- `callgrind-{discrete,continuous}.out`, each with `-inclusive.txt`, `-self.txt`,
  `-tree.txt`, and the unthresholded `-inclusive-all.txt` and `-self-all.txt`
- `callgrind-discrete-instr.out`, with `--dump-instr=yes`, and `lockcount.py`, which
  counts its executed lock-prefixed instructions
- `byfn.py`, which sums callgrind_annotate rows per function, inlined rows merged
- `cachegrind-discrete.{out,txt}`, `dhat-discrete-{1,2}.json`, `massif-discrete.{out,txt}`
- `perf.data` and `flat.txt`, the `cpu-clock` pass at the default 10 iterations
- `benchmark-release-shared-static.txt`, the runs of section 7
- `prototype/`: `proto.cpp`, which prices `prod` or `lock` configurations with the
  `engine`, `cached` (4.1) or `directfwd` (4.2) variant; `lock-bits.txt`, every variant's
  NPV with its bit pattern, shared and static; and `cg-prod-*`, their callgrind runs

Under `build/profile-static/prof/`: `callgrind-{discrete,continuous}.out` of the static
build, with their annotations.

`proto.cpp` is compiled by hand with the `profile` flags against `librke_ql_ext.a` and
`libQuantLib` of either tree.
