<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling the BonusClassicOption Monte Carlo

Where `rke_profile_bonusclassicoption` spends its instructions, and which improvements
carry over from this workload to a production market. 4.1, 4.3 and 4.5 are implemented.
Section 8 profiles a smile market, and section 9 lists the QuantLib changes it calls for.

## 1. What Was Measured

| Item     | Value                                                                                                                                                                                                                                                                                                            |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Revision | `33eeead`; its code is that of `9b8fa24`, which the first pass measured, and every figure below was re-measured on it                                                                                                                                                                                            |
| Build    | `profile` preset: `-O3 -g -fno-omit-frame-pointer`, Homebrew clang 23.1.2, C++17, QuantLib shared as it then defaulted to; a second `profile` tree with `-DBUILD_SHARED_LIBS=OFF`. Since 4.3 the preset links statically, so the shared figures need `-DBUILD_SHARED_LIBS=ON`                                    |
| Machine  | WSL2 laptop, no hardware counters                                                                                                                                                                                                                                                                                |
| Tools    | valgrind 3.27.1 (callgrind, cachegrind, DHAT, massif), `perf` in `cpu-clock` mode, google-benchmark from the submodule                                                                                                                                                                                           |
| Runs     | `discrete 1` and `continuous 1` of the engine before 4.1, which `--path-generation uncached` runs today, the default `cached` being 4.1's path; shared and static; one repricing, since callgrind counts exactly. Prototypes of 4.1 and 4.2 under callgrind; 4.1's implementation at `8a9fafe` against `7e29f80` |

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
Section 8 measures it at 7,609 Ir per step on a bilinear surface and 51,804 on a bicubic
one, against the 1,938 measured here. The findings below are kept only where they hold in
that branch as well.

## 4. Candidates That Carry Over to Production

Ranked by Ir saved per repricing on this workload. The prototypes are compiled into one
binary together with the unchanged engine, and each saving is taken against that engine:
34.05 G discrete and 38.83 G continuous, 0.3–0.4% below the profile executable's count for
the same pricing.

### 4.1 Precompute Path-Independent Step Terms Once per Grid

- **Status:** implemented, for the strike-independent branch only. On the shared `profile`
  build, one repricing drops from 34.18 G to 2.68 G Ir discrete (92.15%) and from 39.00 G to
  3.58 G continuous (90.81%), with the production NPVs unchanged to the last digit printed. The
  prototype measured 92.4% and 91.0% against its own engine build.
- **Lives in:** `BlackScholesStepCache` and `CachedStepPathGenerator` in
  `src/rke/ql/ext/methods/montecarlo/`. `MCBonusClassicEngine` takes its paths from them under
  its default traits, `CachedStepSingleVariate`, and `CachedStepBonusClassicPathPricer` takes
  its step variance from the cache. `makeBonusClassicOptionSetup` picks the traits through
  `PathGeneration`: `CachedStep` by default, `Uncached` for `QuantLib::SingleVariate`, which is
  what `--path-generation uncached` and the `*Uncached` benchmarks run.
- **Branch guard:** the cache classifies the volatility by type, then checks its step bitwise
  against `evolve` at two points on every step. That catches the two cases the type misses: a
  forced discretization, and a process built with an external local volatility. Where the
  check fails, `CachedStepPathGenerator` refuses the process at construction instead of
  falling back. Such a process, a smile included, takes `QuantLib::SingleVariate` through the
  engine's `MC` template parameter, and every step goes through `evolve` as before. The smile
  branch has its own cache, 4.5.
- **Cost of the explicit choice:** none in work, one instruction per step in register
  allocation. Callgrind on the static `profile` build, one repricing, master against this
  design: 2.658 G to 2.675 G Ir discrete and 3.558 G to 3.575 G continuous, 0.6% each, the
  NPVs bit-identical. The compiler spills the path pointer across `exp` where it kept it in a
  register before; the two loops differ in nothing else.
- **Finding:** every path recomputes per-step quantities that depend only on the time
  grid. That holds for any curve shape, not just flat ones.
- **Strike-independent branch:** variance, its square root, and the drift
  `(r_fwd − q_fwd)·dt − ½·var` are all per-step constants.
- **Smile branch:** the rate part `r_fwd − q_fwd` of `GeneralizedBlackScholesProcess::drift`
  is still a per-step constant. That is two `forwardRate` calls per step, 1,072 Ir on
  this setup's flat curves and more on interpolated ones. Only `localVol(t, x)` stays
  per path.
- **Evidence:** the measured figures are in Status. The continuous figure includes the
  pricer's step variance from the cache. These percentages belong to this workload's
  branch; 4.5 measures the smile branch's counterpart.

- **Lock impact:** bit-identical. The cached doubles are the ones `evolve` computes, and
  `CachedStepPathGeneratorTests` require every path point to equal
  `QuantLib::PathGenerator`'s exactly. The implementation reproduces:
  - the production setup: discrete `100.22671167047309` and continuous
    `99.883605619178169`;
  - both locks in `src/rke/testsuite/BonusClassicOption.cpp`: discrete
    `106.96041418042263` and continuous `105.88329042929441`.

  In the smile branch, 4.5 caches `r − q` in QuantLib's evaluation order `(r − q) − ½σ²`,
  bit-identical without FMA contraction; 4.5 gives the differences with it.

- **Design constraint:** the branch decision rests on
  `GeneralizedBlackScholesProcess`'s private `isStrikeIndependent_`,
  `forceDiscretization_` and `hasExternalLocalVol_`. Code outside QuantLib cannot read
  any of them.
  - The type check re-derives `isStrikeIndependent_` as `localVolatility()` does. The
    other two are invisible to it, which is why the cache also checks its step against
    `evolve`.
  - The path generator type is fixed by `McSimulation`'s `MC` template parameter, so the
    engine exposes it: `CachedStepSingleVariate` by default, `QuantLib::SingleVariate` for a
    process the cache cannot reproduce. The caller decides, not the generator.
  - The smile branch's discretization is a protected member with no accessor, so 4.5
    confirms it is `EulerDiscretization` only through the same bitwise check.

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
- **Overlap with 4.1:** in the strike-independent branch, 4.1 runs the forward rates once
  per grid point instead of once per path step, so there 4.2 adds almost nothing to this
  engine. Under a smile, 4.5 does the same for `drift`'s forward rates, so in this engine
  4.2 adds almost nothing in either branch. It still pays in every other engine that
  evolves this process.
- **Lock impact:** bit-identical, all four values above, to the IEEE bit pattern.
- **Upstream:** medium. The change is local and behaviour-preserving, but it duplicates
  the core of `YieldTermStructure::forwardRate` in the process.

### 4.3 Link QuantLib Statically

- **Status:** implemented. The figures below were measured before it, against a second
  tree, and correct the first pass's estimate of about 2% Ir.
- **Lives in:** setup, `external/CMakeLists.txt`. QuantLib's own CMake sets
  `BUILD_SHARED_LIBS` to `UNIX` when it is undefined; `external/CMakeLists.txt` now defines
  it as `OFF` first, unless it is given on the command line.
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

### 4.5 Evaluate the Local Volatility Once per Euler Step

Measured on the smile markets of section 8, not on this workload, whose step is exact.

- **Status:** implemented. Callgrind on the static `profile` build, one repricing at 2^16
  paths, uncached against cached:

  | Market           | Mode       | Uncached |   Cached | Saved |
  | ---------------- | ---------- | -------: | -------: | ----: |
  | `smile-bilinear` | discrete   | 127.22 G |  52.69 G | 58.6% |
  | `smile-bilinear` | continuous | 147.31 G |  72.79 G | 50.6% |
  | `smile-bicubic`  | discrete   | 865.79 G | 422.01 G | 51.3% |

- **Lives in:** `LocalVolStepCache` in `src/rke/ql/ext/methods/montecarlo/`, which
  `CachedStepPathGenerator` takes as its `StepCache` parameter. `LocalVolStepSingleVariate`
  pairs it with `QuantLib::SingleVariate`, and `makeBonusClassicOptionSetup` picks it under
  `PathGeneration::CachedStep` on the smile markets.
- **Finding:** in the Euler branch, `evolve` evaluates `localVol(t, x)` twice with the
  same arguments, once in `drift` and once through `EulerDiscretization::diffusion`, and
  calls `forwardRate(t, t + 1e-4)` twice per step although it depends on the grid only.
- **Change:** `r − q` and `√Δt` once per grid, and the local volatility once per step,
  through the process's own `localVolatility()` link. No QuantLib code is copied, so it
  holds for any local volatility: `LocalVolSurface` under a smile, `LocalConstantVol` or
  `LocalVolCurve` under a forced discretization, or an external one.
- **Branch guard:** the step is compared with `evolve` at two points on every step, as in
  4.1, but to a relative `LocalVolStepCache::stepTolerance` of 1e-13 instead of bitwise.
  Both sides evaluate the same local volatility at the same point, so only the step's own
  rounding can differ. The exact step on a variance curve fails the check, and so would any
  discretization other than `EulerDiscretization`. A constant volatility on flat curves
  passes it, because there the exact and the Euler step coincide in exact arithmetic.
- **Lock impact:** the locks take the exact step and do not move. On the smile markets the
  cache is bit-identical where the compiler does not contract to FMAs, which is the x86-64
  baseline and MSVC: on the static `profile` build here the NPVs are the same cached and
  uncached, bilinear discrete `100.23201398081022` and continuous `99.886405693076455`,
  bicubic discrete `100.27835252928404` and continuous `99.932975609578065`.
- **Under FMA contraction:** every arm64 build, and x86-64 with FMA enabled. The cached step
  is one expression, where the compiler may fuse a different product into the exponent's
  sum than it fuses across `evolve`'s function boundaries, so its last bit can differ.
  `LocalVolSurface::localVolImpl` divides second differences in strike by `dy²`, with
  `dy` as small as 1e-6, so that bit turns into a different σ and a path moves visibly:
  single bilinear paths end up to 3% apart, bicubic ones about 1e-5. The prices do not
  follow. Cached against uncached, one repricing at 2^16 paths, built with
  `-march=x86-64-v3`, relative:

  | Compiler | Surface  | Discrete | Continuous |
  | -------- | -------- | -------: | ---------: |
  | gcc      | bilinear |  −5.4e-7 |    −7.0e-7 |
  | gcc      | bicubic  |  −3.0e-9 |    −3.0e-9 |
  | clang    | bilinear |  +2.3e-7 |    −1.9e-6 |
  | clang    | bicubic  |  −6.1e-9 |    −1.9e-8 |

  No path changes its knock-out status. The Monte Carlo standard error is about 8.4e-4
  relative, and the uncached engine itself moves by 8.2e-6 between a contracting gcc build
  and the non-contracting one here, bilinear discrete.

- **Tests:** `LocalVolStepPathGeneratorTests` compare one step from the same point under a
  smile within `stepTolerance`, measured up to 4e-16 under contraction, and every path
  point within 1e-12 where the local volatility is constant, measured 7.7e-16. The engine
  test prices a smile within 1e-6 of `QuantLib::SingleVariate`, measured 1.3e-8 at 1,000
  paths. Without contraction all of them are exact.
- **What it leaves:**
  - In continuous mode `BonusClassicPathPricer` calls `process->variance` on each step of
    each path that stays above the barrier on the grid. Under Euler that is
    `EulerDiscretization::variance`, a third `localVol(t, x)` at the same `(tᵢ, xᵢ)` the
    generator already evaluated: 19.11 G of the cached bilinear run, 26.3%. Reusing the
    generator's σ needs a side channel to the pricer, since a `Path` carries values only.
  - The six `discount` calls per `localVolImpl` that depend on `t` only, and the surface
    lookups. Both live in QuantLib, section 9.
- **Alternative not taken:** a prototype that copies `LocalVolSurface::localVolImpl` and
  feeds it the cached discounts runs 1,670 Ir per step against this design's 3,168 on the
  bilinear surface, and 23,768 against 25,265 on the bicubic one. It was rejected to keep
  QuantLib code out of this repository and to stay generic over the local volatility.
  9.1's time slice would do the same computation inside QuantLib, without a copy.

## 5. Findings Dropped as Specific to This Market

| Finding                                                                                    | Measured here                               | Why it does not carry over                                                                                                                                                 |
| ------------------------------------------------------------------------------------------ | ------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Pass `extrapolate = true` to `blackVariance` in `GeneralizedBlackScholesProcess::variance` | 4.35 G Ir discrete (12.7%), bit-identical   | Only the strike-independent branch calls it. Under a smile, `variance` goes to the discretization, and `LocalVolSurface` already passes `true`.                            |
| Cache the continuous pricer's per-step `variance()`                                        | 3.86 G Ir continuous (9.9%)                 | Implemented with 4.1 for the strike-independent branch. With a smile the variance depends on `path[i]`, and the pricer's own header warns the bridge is approximate there. |
| Drop the second `localVolatility()` "trigger update" per step                              | 0.37 G Ir (1.1%)                            | Insignificant next to `localVolImpl`.                                                                                                                                      |
| Generate paths in log space and call `exp` only where a price is needed                    | about 0.95 G Ir after 4.1, moves both locks | `localVol(t, x)` needs the price level every step, and `localVolImpl` takes its own `log`.                                                                                 |
| The 92% headline of 4.1                                                                    | see 4.1                                     | It is the strike-independent branch's figure. The smile branch's counterpart is 4.5, at 51–59%.                                                                            |

## 6. What the Profile Rules Out

These hold for the branch measured. Under a smile, section 8 qualifies two of them.

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
  cannot gain more than that, and less under the heavier smile branch. With 4.1 in place
  this bound no longer holds on this workload: of 2.68 G Ir discrete, Sobol plus
  `InverseCumulativeNormal` take 0.92 G (34.3%) and `BrownianBridge::transform` 0.42 G
  (15.6%), next to 1.17 G (43.6%) for the cached step, 0.95 G of it in `exp`.
- **Under a smile:** the bicubic surface allocates on every lookup, 9.2. With 4.5 in
  place on the bilinear smile, Sobol plus `InverseCumulativeNormal` take 0.92 G (1.7%) and
  the bridge about 0.42 G (0.8%) of 52.69 G, so the machinery's bound holds there.

## 7. Wall Clock (Secondary)

`rke_benchmark_bonusclassicoption --benchmark_repetitions=5` from a shared `release` tree,
then the default and now `-DBUILD_SHARED_LIBS=ON`, and from a static one, now the `release`
preset's default, run back to back in one session with nothing else running, in the order
shown. Mean per repricing, with its cv:

| Build          |            Discrete |          Continuous |
| -------------- | ------------------: | ------------------: |
| shared, first  | 2,088 ms (cv 0.40%) | 2,371 ms (cv 0.52%) |
| static         | 1,910 ms (cv 0.38%) | 2,162 ms (cv 0.83%) |
| shared, second | 2,045 ms (cv 0.44%) | 2,333 ms (cv 0.34%) |

- **Baseline:** the first pass measured 2,035 ms and 2,352 ms. The two shared runs here
  drift 2% apart, more than their cv, so that is the resolution of any comparison.
- **4.3:** static is 6.6–8.5% faster discrete and 7.3–8.8% continuous, against 3.4% fewer
  Ir. Why the time gain is twice the Ir gain cannot be settled without hardware counters.
- **4.1:** the first pass timed its prototype at 13.7×. The implementation is timed in the
  table below, from a later session.

The implementation of 4.1 against `master`, both from the `release` preset, shared, run back
to back with nothing else running, alternating in the order shown, with
`--benchmark_repetitions=5`. Mean per repricing, with its cv:

| Build          |            Discrete |          Continuous |
| -------------- | ------------------: | ------------------: |
| master, first  | 2,128 ms (cv 6.69%) | 2,427 ms (cv 1.77%) |
| 4.1, first     |   160 ms (cv 2.10%) |   224 ms (cv 0.14%) |
| master, second | 2,077 ms (cv 0.95%) | 2,355 ms (cv 0.25%) |
| 4.1, second    |   159 ms (cv 2.13%) |   225 ms (cv 2.22%) |

- **Speed-up:** 13.0–13.4× discrete and 10.5–10.8× continuous, against Ir ratios of 12.7× and
  10.9×. The two master runs drift 2.5–3.1% apart, far below the effect.
- **Re-timing:** needs one build now. `BonusClassicOptionUncached` and
  `BonusClassicOptionContinuousUncached` run the uncached baseline in the same binary as the
  cached `BonusClassicOption` and `BonusClassicOptionContinuous`.

## 8. The Smile Market

### 8.1 What Was Measured

The markets `smile-bilinear` and `smile-bicubic`, from `SmileMarketData` in
`src/rke/common/BonusClassicOptionSetup.cpp`; option, grid and paths as in section 1:

- **Curves:** `ZeroCurve` on today, 3M, 6M, 1Y and 2Y, linear in the zero rate,
  continuously compounded. Risk-free 0.8%, 0.9%, 1.0%, 1.1%, 1.2%; dividend yield 3.2%,
  3.1%, 3.0%, 2.9%, 2.8%.
- **Surface:** `BlackVarianceSurface`, σ(K) = 0.20 − 0.08 · ln(K / 100) on every date,
  expiries 1M to 2Y, strikes 10 to 400 with constant strike extrapolation, which no path
  reaches. Bilinear or bicubic in time and strike, the markets' only difference.
- **Conventions:** `Actual360` and `NullCalendar` throughout, spot 100.

The engine is `--path-generation uncached`. One uncached `smile-bicubic` repricing runs
about a minute natively, so the breakdown below comes from a harness over the same setup at
2^10 paths, 261,120 steps; Ir per step does not depend on the path count. Per-step figures
are the path generator's inclusive Ir divided by that. 4.5's status table is the
implementation at 2^16 paths, at `70763ec`; the prototypes ran against `f080503`. Build and
tools as in section 1: the static `profile` preset, valgrind 3.27.1.

### 8.2 Where the Instructions Go

Discrete, uncached. Per step `evolve` makes 2 `localVolImpl` calls, which make 10
`blackVariance` and 12 `discount` calls, plus 2 `forwardRate` with 4 more `discount`:

| Callee               | Bilinear Ir per step | Bicubic Ir per step |
| -------------------- | -------------------: | ------------------: |
| all, path generation |                7,609 |              51,804 |
| `localVol` × 2       |                5,951 |              50,146 |
| `blackVariance` × 10 |                2,528 |              46,723 |
| `discount` × 16      |                2,902 |               2,902 |
| `forwardRate` × 2    |                1,130 |               1,130 |

The `discount` row includes the 4 calls inside `forwardRate`. On the bicubic surface,
`BicubicSplineImpl::value` is 88.5% of the run, about 4,599 Ir per lookup, and its
`shared_ptr` refcount atomics alone are 13.9%.

Against the same harness, per step:

| Variant                                   | Bilinear | Bicubic |
| ----------------------------------------- | -------: | ------: |
| engine, `QuantLib::SingleVariate`         |    7,609 |  51,804 |
| 4.5, `LocalVolStepCache`                  |    3,168 |  25,265 |
| copied `localVolImpl` (4.5's alternative) |    1,670 |  23,768 |

Without FMA contraction all three price to the same NPV bit pattern at 2^16 paths, discrete
and continuous, under both surfaces; 4.5 gives the differences under contraction.

## 9. QuantLib Changes the Smile Profile Calls For

None of these is made here. Each belongs on a branch of `ralfkonrad/QuantLib`, measured
with `--market smile-*`, as `.agents/changing-quantlib.md` describes, and reaches this
repository through a submodule bump once it is upstream. 4.5 gains from 9.2 unchanged;
for 9.1 its one `localVol` call switches to the slice overload.

### 9.1 A Time Slice for the Local Volatility

- **Finding:** `LocalVolSurface::localVolImpl(t, x)` computes `dr`, `dq` and the forward
  value at `t`, and four more discounts at `t ± dt`, on every call. All of them depend on
  `t` only, and a Monte Carlo grid asks for the same `t` once per path.
- **Change:** a virtual on `LocalVolTermStructure` that returns the terms for a time, with
  a default that holds the time only, and a `localVol` overload that takes them.
  `LocalVolSurface` precomputes its discounts and forward there, and its own
  `localVolImpl` goes through the same code, so without FMA contraction the result stays
  bit-identical.
  `LocalVolStepCache` builds one slice per grid time and passes it per step.
- **Evidence:** the copied-`localVolImpl` prototype does exactly this computation:
  3,168 → 1,670 Ir per step on the bilinear surface (47%), 25,265 → 23,768 on the bicubic.
- **Upstream:** medium. It adds API to a base class, but stays optional for every other
  local volatility.

### 9.2 A Time Slice for the Black Variance Surface

- **Finding:** `localVolImpl` asks the surface for 3 strikes at `t` and one each at
  `t + dt` and `t − dt`. For a fixed time, `BicubicSplineImpl::value` builds the same
  strike spline on every call: it evaluates each row spline at `t` into a freshly
  allocated section, constructs a `CubicInterpolation` over it, and evaluates that once.
  `BilinearInterpolationImpl::value` repeats the time `locate` and weights.
- **Change:** the same pattern one level down. A slice of `BlackVarianceSurface` at a time
  holds the strike spline, or the bilinear time weights, and `LocalVolSurface`'s slice of
  9.1 holds the three it needs. The bicubic slice builds the same spline from the same
  section, so without FMA contraction it stays bit-identical. The bilinear one does so only
  if it keeps the four corner values and the existing formula, saving just the `locate`.
- **Evidence:** unmeasured. Its ceiling is the surface's share after 4.5: 33% of the
  bilinear step and 92% of the bicubic one, against 35% and 4% for the six `discount`
  calls of 9.1. On the bicubic surface it replaces a spline construction, about 4,599 Ir,
  by a one-dimensional spline evaluation per lookup.
- **Upstream:** medium. A smaller form, which any caller gains from, reuses the section
  buffer instead of allocating it per call; it removes one allocation per lookup, not the
  spline construction.

### 9.3 The Forward Rate Without `InterestRate`

4.2, unchanged. Both step caches take the rate terms out of this engine's path loop, so
here it adds almost nothing; it pays in every other engine that evolves the process.

## 10. Next Step

1. 9.1 and 9.2 on a `ralfkonrad/QuantLib` branch, measured with
   `--market smile-bilinear` and `--market smile-bicubic` against 4.5's figures.
2. The continuous pricer's third `localVol` per step, 4.5's "What it leaves", in this
   repository: 26.3% of the cached bilinear continuous run.

## 11. Raw Profiles

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

For 4.1's implementation, in the same directory of its worktree:

- `callgrind-4.1-{before,after}-{discrete,continuous}.out`, the counts in 4.1's status
- `benchmark-4.1-master-branch.txt`, the second table of section 7

Under `build/profile-static/prof/`: `callgrind-{discrete,continuous}.out` of the static
build, with their annotations.

For section 8 and 4.5, in the same directory of the `smile-market-profile` worktree:

- `callgrind-smile-bilinear-{discrete,continuous}-{cached,uncached}.out` and
  `callgrind-smile-bicubic-discrete-{cached,uncached}.out`, the 2^16-path runs of 4.5's
  status, by the copy `rke_profile_bonusclassicoption-localvolstep`
- `prototype/smileproto.cpp`, which prices `engine`, `lite` (4.5) or `cached` (the copied
  `localVolImpl`) on either surface at a given path count; `cg-smile-*` and
  `cg-smile-lite-*`, its 2^10-path callgrind runs; `full-*.txt`, its 2^16-path NPVs with
  their bit patterns

In the `smile-market-profile` worktree, `build/gcc-v3` and `build/clang-v3` are release
trees configured with `-DCMAKE_CXX_FLAGS=-march=x86-64-v3`. They reproduce CI's contracting
jobs to the mismatch count and give 4.5's figures under FMA contraction.

`proto.cpp` is compiled by hand with the `profile` flags against `librke_ql_ext.a` and
`libQuantLib` of either tree.
