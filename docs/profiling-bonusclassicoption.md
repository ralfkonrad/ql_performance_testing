<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling the BonusClassicOption Monte Carlo

Where `rke_profile_bonusclassicoption` spends its instructions, and which improvements
carry over from this workload to a production market. Nothing here is implemented.

## 1. What Was Measured

| Item     | Value                                                                                      |
| -------- | ------------------------------------------------------------------------------------------ |
| Revision | `9b8fa24`, merged unchanged as `1c8bc54`                                                   |
| Build    | `profile` preset: `-O3 -g -fno-omit-frame-pointer`, Homebrew clang 23.1.2, C++17           |
| Machine  | WSL2 laptop, no hardware counters                                                          |
| Tools    | valgrind 3.27.1 (callgrind, cachegrind, DHAT, massif), google-benchmark from the submodule |
| Runs     | `discrete 1` and `continuous 1`; one repricing, since callgrind counts exactly             |

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

In continuous mode `BonusClassicPathPricer` adds 4.87 G Ir. Of that, 3.87 G is
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

### 4.1 Precompute Path-Independent Step Terms Once per Grid

- **Lives in:** rke, the path generation of `MCBonusClassicEngine` in
  `src/rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp`.
- **Finding:** every path recomputes per-step quantities that depend only on the time
  grid. That holds for any curve shape, not just flat ones.
- **Strike-independent branch:** variance, its square root, and the drift
  `(r_fwd − q_fwd)·dt − ½·var` are all per-step constants.
- **Smile branch:** the rate part `r_fwd − q_fwd` of `GeneralizedBlackScholesProcess::drift`
  is still a per-step constant. That is two `forwardRate` calls per step, 1,072 Ir on
  this setup's flat curves and more on interpolated ones. Only `localVol(t, x)` stays
  per path.
- **Evidence:** a scratchpad prototype that reproduces `MonteCarloModel::addSamples` with
  the strike-independent terms cached:

  | Mode       |     Engine |    Cached | Saving |
  | ---------- | ---------: | --------: | -----: |
  | discrete   | 34.18 G Ir | 2.66 G Ir |  92.2% |
  | continuous | 39.00 G Ir | 3.56 G Ir |  90.9% |

  The continuous figure includes caching the pricer's `variance()` per step. These
  percentages belong to this workload's branch. With a smile, the saving is at least
  the rate terms' absolute cost, but its share of the run is unmeasured.

- **Lock impact:** bit-identical. The cached doubles are the ones `evolve` computes.
  The prototype reproduced, to the IEEE bit pattern:
  - the production setup: discrete `100.22671167047309` and continuous
    `99.883605619178169`;
  - both locks in `src/rke/testsuite/BonusClassicOption.cpp`: discrete
    `106.96041418042263` and continuous `105.88329042929441`.

  In the smile branch, caching `r − q` keeps QuantLib's evaluation order
  `(r − q) − ½σ²`, so it should stay bit-identical as well. That follows from the source;
  no run checked it.

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
  Removing everything except the discounts and the `log` saves an estimated 120–160 Ir
  per call, 4.0–5.3 G Ir per repricing, or 12–16% of this workload. That is a smaller
  share under the heavier smile branch. Estimated from the call graph, not prototyped.
- **Lock impact:** bit-identical.
- **Upstream:** medium. The change is local and behaviour-preserving, but it duplicates
  the core of `YieldTermStructure::forwardRate` in the process.

### 4.3 Link QuantLib Statically

- **Lives in:** setup, `external/CMakeLists.txt`. QuantLib's own CMake sets
  `BUILD_SHARED_LIBS` to `UNIX` when it is undefined, and we leave it undefined.
- **Finding:** `libQuantLib.so` is built with `-fPIC`, so calls between QuantLib
  functions go through the PLT. Cachegrind counts about 48 PLT jumps per step, inside
  an unsymbolized bucket of 17.7% that also holds libm's `exp` and `log` kernels.
- **Evidence:** an estimate of about 2% Ir. The PLT traffic grows with the number of
  QuantLib calls per step, so the smile branch has more of it. Not measured.
- **Lock impact:** expected bit-identical, since the floating-point code is the same.
  Verify that before relying on it.

### 4.4 Refcount Traffic Ir Does Not Show

- **Not a separate change.** This is the reason 4.2 is worth more than its Ir.
- **Finding:** `TermStructure::dayCounter()` returns a copy of a `boost::shared_ptr`-backed
  `DayCounter`, and `impliedRate` makes copies as well. Together they cause about
  200 M lock-prefixed refcount operations per repricing, about 12 per step. They lock
  even in a single-threaded run.
- **Evidence:** callgrind counts each as one instruction, which puts
  `TermStructure::dayCounter` at 2.15% of Ir. A `perf` pass in `cpu-clock` mode put it
  at 14.6% of samples instead. At about 20 cycles each, these operations could account
  for up to roughly 4 G cycles per repricing. That is an upper bound; confirming it needs
  hardware counters, which WSL2 lacks.
- **Coverage:** the same copies happen on every term-structure query, in both branches.

## 5. Findings Dropped as Specific to This Market

| Finding                                                                                    | Measured here                               | Why it does not carry over                                                                                                                      |
| ------------------------------------------------------------------------------------------ | ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Pass `extrapolate = true` to `blackVariance` in `GeneralizedBlackScholesProcess::variance` | 4.35 G Ir discrete (12.7%), bit-identical   | Only the strike-independent branch calls it. Under a smile, `variance` goes to the discretization, and `LocalVolSurface` already passes `true`. |
| Cache the continuous pricer's per-step `variance()`                                        | 3.87 G Ir continuous (9.9%)                 | With a smile the variance depends on `path[i]`, and the pricer's own header warns the bridge is approximate there.                              |
| Drop the second `localVolatility()` "trigger update" per step                              | 0.37 G Ir (1.1%)                            | Insignificant next to `localVolImpl`.                                                                                                           |
| Generate paths in log space and call `exp` only where a price is needed                    | about 0.95 G Ir after 4.1, moves both locks | `localVol(t, x)` needs the price level every step, and `localVolImpl` takes its own `log`.                                                      |
| The 92% headline of 4.1                                                                    | see 4.1                                     | It is the strike-independent branch's figure; 4.1 keeps only the part that carries over.                                                        |

## 6. What the Profile Rules Out

These hold for the branch measured. A smile-surface profile has to confirm them for the other.

- **Allocation:** one repricing allocates 119 blocks and 2.2 MB, nothing per path or
  per step (DHAT, 2-iteration minus 1-iteration run). 94.6% of the bytes are the doubling
  growth of `GeneralStatistics`' sample vector. Peak heap is 1.66 MB (massif).
  Allocation is below 1e-4 of Ir.
- **Memory and branches:** cachegrind shows D1 misses at 0.006% of data references, LL
  misses near zero, and conditional mispredicts at 0.68%. Its 14.6% indirect mispredict
  rate comes from the simulator's 512-entry predictor aliasing. The loop is
  instruction-bound.
- **Monte Carlo machinery:** RNG, bridge, pricer, statistics and setup together cost
  about 4%. A different inverse normal, dropping the bridge, or `IncrementalStatistics`
  cannot gain more than that, and less under the heavier smile branch.

## 7. Wall Clock (Secondary)

Same machine, same session, nothing else running:

- **Release benchmark:** `rke_benchmark_bonusclassicoption --benchmark_repetitions=5`
  gives 2,035 ms (cv 1.7%) discrete and 2,352 ms (cv 0.7%) continuous per repricing.
- **Prototype of 4.1, natively:** discrete 2.02 s → 0.147 s (13.7×), continuous
  2.34 s → 0.22 s, from five runs each at `profile` flags. The time ratio exceeds the
  12.8× Ir ratio, which fits the refcount cost in 4.4.

## 8. Next Step

Rank for production by profiling a production-like market: an interpolated
risk-free and dividend curve and a `BlackVarianceSurface` with a smile. Two things
from the source should be measured there:

- `localVolImpl` runs twice per step with identical `(t, x)`, once from `drift` and once
  from `diffusion`.
- Its discounts and forward value depend only on `t`, so they could be cached per step.

## 9. Raw Profiles

These are local and untracked, under `build/profile/prof/rke_profile_bonusclassicoption/`.
The recipes that produce them are in `.agents/profiling.md`.

- `callgrind-{discrete,continuous}.out`, each with `-inclusive.txt`, `-self.txt` and `-tree.txt`
- `callgrind-discrete-byfunction.txt`, with inlined rows summed per function
- `callgrind-continuous-pricer-src.txt`
- `cachegrind-discrete.{out,txt}`
- `dhat-discrete-{1,2}.json`, with `.txt` and `-bytes.txt` digests
- `massif-discrete.{out,txt}`
- `benchmark-release-baseline.json`
- `prototype/`: the prototype sources `proto.cpp` (production setup) and `proto_lock.cpp`
  (test-suite lock configuration), their callgrind annotations, and `native-timing.txt`
