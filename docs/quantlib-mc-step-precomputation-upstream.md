<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Precomputing Monte Carlo Step Terms in QuantLib: A Proposal

Which per-step computations of QuantLib's Monte Carlo path generation depend only on the time
grid, what caching them once per grid saves, and how that cache fits QuantLib's process and
path-generator API. Nothing here is in QuantLib; step 1 of section 5 is implemented on a branch and
measured in section 8. Every claim about its code refers to commit `966a4cc10` (`v1.43-625`, or
`v1.44-rc-1` by `git describe --tags`); every figure is a callgrind instruction count (Ir) from one
of the two setups in section 1, measured on a step cache built beside QuantLib, or in section 8 on
that branch, and every figure not marked as measured is an estimate.

## Management Summary

QuantLib's Monte Carlo engines recompute, on every step of every path, terms that depend only on
the time grid and the market: forward rates, variances, discount factors and, under a smile, half
of the local-volatility evaluations. Computing them once per grid removes most of the
path-generation cost and changes no price.

- **Measured gains.** On a one-year Black-Scholes workload with 255 steps and 2^16 paths, one
  repricing drops by 92% under a flat volatility and by 51 to 59% under a smile, measured as
  callgrind instruction counts (section 1). The flat result is bit-identical; the smile result is
  bit-identical without FMA contraction and within 2e-6 relative with it, against a Monte Carlo
  standard error of 8e-4.
- **Who benefits.** Every engine built on `PathGenerator` or `MultiPathGenerator`: the 19
  Black-Scholes and basket engines first, the Heston family and Hull-White after them (sections 2
  and 3). A production market with interpolated curves and a smile gains from the fused Euler
  step of 3.1 alone, which needs no new API.
- **No behaviour change.** Each cached term is the same double `evolve()` computes today, so no
  existing test moves; a subclass that overrides part of the step keeps its own `evolve()` behind
  an exact-type gate (section 4). Nothing public is deprecated.
- **What it costs.** Two virtuals on the process base classes, a public table type for each and one
  inspector on the Black-Scholes process, kept under QuantLib's five-release deprecation rule;
  the path generators become observers of their process (section 4).
- **Next steps.** Seven branches, each measured before the next starts, in proof-of-concept
  order: first the hook with the exact Black-Scholes step, the whole flat-volatility gain in one
  change, then the local-volatility changes (section 5). The first is implemented. Inside
  QuantLib it saves 91.9% of a flat discrete repricing, or 91.8% in wall clock, with every NPV
  bit-identical (section 8).

## 1. What Was Measured

Two setups, both the same instrument: a discretely monitored down-and-out bonus option, barrier
90, bonus level 120, spot 100, one year from the evaluation date, priced by Monte Carlo on 255
steps, from 252 steps per year over the `Actual360` year fraction, with 2^16 Sobol paths (Joe-Kuo
D7 directions, seed 42) and a Brownian bridge. `Actual360` and `NullCalendar` throughout; every
curve continuously compounded. The repository's shared setup has since moved the barrier to 70,
so its executables no longer reproduce the NPVs quoted here as they stand. Builds are `-O3`,
targeting baseline x86-64 without FMA; the smile rows link QuantLib statically, the flat row was
measured on the shared build, where the static uncached count is 33.02 G. One repricing is one
callgrind run; wall clock is not quoted.

- **Flat setup:** `BlackScholesMertonProcess` on `FlatForward` curves, risk-free rate 1% and
  dividend yield 3%, under a `BlackConstantVol` of 20%. `GeneralizedBlackScholesProcess::evolve`
  takes its strike-independent branch, the exact lognormal step.
- **Smile setup:** the same process on `ZeroCurve`s linear in the continuously compounded zero
  rate at today, 3M, 6M, 1Y and 2Y, risk-free 0.8%, 0.9%, 1.0%, 1.1%, 1.2% and dividend yield
  3.2%, 3.1%, 3.0%, 2.9%, 2.8%, under a `BlackVarianceSurface` with
  σ(K) = 0.20 − 0.08 · ln(K / 100) on every expiry from 1M to 2Y, strikes 10 to 400, constant
  strike extrapolation that no path reaches, bilinear or bicubic in time and strike. The process
  links a `LocalVolSurface` and `evolve` takes the Euler branch.

A step cache built beside QuantLib, one per branch of `evolve`, takes each step from a table built
once per `TimeGrid`:

| Setup           | Branch | Uncached |   Cached |  Saved | NPV                                                                               |
| --------------- | ------ | -------: | -------: | -----: | --------------------------------------------------------------------------------- |
| flat            | exact  |  34.18 G |   2.68 G | 92.15% | unchanged to the last digit printed                                               |
| smile, bilinear | Euler  | 127.22 G |  52.69 G |  58.6% | bit-identical without FMA contraction, within 1.9e-6 relative with it (section 4) |
| smile, bicubic  | Euler  | 865.79 G | 422.01 G |  51.3% | as bilinear                                                                       |

Three principles produce those results, and each holds inside QuantLib:

1. A term that depends only on `(t0, dt)` and the market is computed once per grid point, not once
   per path step.
2. It is computed through the same calls, in the same order and expression shape, so every path
   point is the same double and no existing test moves. Only a whole left-associative
   subexpression can be cached; `t0 + dt` stays the literal sum, never `grid[i + 1]`.
3. A process whose step the cache cannot reproduce keeps its own `evolve()`. Nothing is rerouted
   silently.

Two constraints that bind a cache outside QuantLib fall away inside it:

- **The branch is known.** Outside, the cache infers the branch from the volatility type, or
  takes whatever `localVolatility()` links, and then probes `evolve()` at two points on every grid
  step on construction, because `isStrikeIndependent_`, `forceDiscretization_` and
  `hasExternalLocalVol_` are private to `GeneralizedBlackScholesProcess`. The process itself reads
  them, and 3.2 adds one inspector for the condition they form (appendix A).
- **The step can be shared.** Under FMA contraction a step written as one expression cannot match
  `evolve()`, whose arithmetic is split across virtual calls, so the outside cache accepts its
  step within a relative 1e-13, 250 times the 4e-16 one step moves under contraction. Inside
  QuantLib, `evolve()` and the cache can call the same step helper (section 4).

The new problem is a subclass that overrides a virtual the step runs through, `evolve()`,
`drift()`, `diffusion()`, `variance()`, `stdDeviation()` or `apply()`, and would inherit a cache
that ignores the override. QuantLib's deprecation rules forbid that silent change, so section 4
gates the cache on the exact type and such a subclass keeps `evolve()`.

## 2. Where QuantLib Steps a Path

- **Generators:** `PathGenerator::next` calls `process_->evolve(t, path[i-1], dt, dw)` once per
  step. `MultiPathGenerator::next` does the same with `Array`s and refuses a Brownian bridge.
- **Lifetime:** `McSimulation::calculate` builds a new `MonteCarloModel`, and with it a new path
  generator, on every `calculate()`. `value()` and `valueWithSamples()` reuse it within that call.
  `MCLongstaffSchwartzEngine` builds a calibration and a pricing generator on the same grid, so
  two tables per `calculate()`. `MCHullWhiteCapFloorEngine::pathGenerator` builds a new
  `HullWhiteForwardProcess` from the model's `a` and `sigma` on every call.
- **Engines:** every concrete engine fixes its traits, so a cache reaches them only through the
  path generator they already use:
  - `SingleVariate` over `GeneralizedBlackScholesProcess`, 12 engines: `MCEuropeanEngine`,
    `MCDigitalEngine`, `MCAmericanEngine`, `MCBarrierEngine`, `MCDoubleBarrierEngine`, the three
    discrete Asian engines, `MCLookbackEngine`, `MCPerformanceEngine`, `MCForwardEuropeanBSEngine`,
    `MCVarianceSwapEngine`.
  - `MultiVariate` over a `StochasticProcessArray`, 7 engines: the European and American basket
    engines, `MCEverestEngine`, `MCHimalayaEngine`, `MCPagodaEngine`, `MCPathBasketEngine`,
    `MCAmericanPathEngine`.
  - Heston family: `MCEuropeanHestonEngine` (also with `BatesProcess` and `HestonSLVProcess`), the
    forward and Asian Heston engines, `MCEuropeanGJRGARCHEngine`.
  - Rates: `MCHullWhiteCapFloorEngine` (`SingleVariate`), `MCHestonHullWhiteEngine`.
- **Direct callers:** `HestonSLVMCModel::performCalculations` calls `evolve()` with steps outer and
  calibration paths inner. `test-suite/pathgenerator.cpp` drives `PathGenerator` without an
  engine over Black-Scholes, GBM, Ornstein-Uhlenbeck and square-root processes,
  `test-suite/brownianbridge.cpp` over Black-Scholes, `test-suite/gsr.cpp` over `GsrProcess`,
  `test-suite/normalclvmodel.cpp` over an `OrnsteinUhlenbeckProcess`, and
  `Examples/DiscreteHedging` a `MonteCarloModel` over Black-Scholes; the direct
  `MultiPathGenerator` users in the test suite step other processes. QuantLib-SWIG wraps
  `PathGenerator` as `GaussianPathGenerator` in `SWIG/montecarlo.i`, read at `dc84dd9f`, outside
  this document's pin.
- **Precedents in QuantLib:** `LogNormalFwdRatePc` precomputes `fixedDrifts_` and `calculators_`
  per step in its constructor; `TrinomialTree` caches the step variance in `v2Cache`;
  `BrownianBridge` its weights per grid; `LongstaffSchwartzPathPricer` and `MCBarrierEngine` their
  discounts. Two memoize instead: `GsrProcessCore` in `std::map`s keyed on the exact
  `(t0, t0 + dt)`, its `G` on its own `(w, t)` and its `y` on `t` alone, cleared only by
  `flushCache()`, which the constructor and `setForwardMeasureTime()` call and no parameter
  setter does, since `GsrProcess` registers with nothing; `JointStochasticProcess` keys its
  correlation on `(t0, dt)` and clears it in `update()`.
- **Tests:** two sets pin `evolve()` tightly. `pathgenerator.cpp`'s `testPathGenerator` at 2e-8
  (2e-7 antithetic) and `testMultiPathGenerator` at 2e-7; `testCached` of Himalaya, Everest and
  Pagoda at 1e-8. All five run on `flatVol`, a `BlackConstantVol` on `Actual360`, and
  `testPathGenerator` pins `GeometricBrownianMotionProcess`, `OrnsteinUhlenbeckProcess` and
  `SquareRootProcess` the same way. Every other MC test compares within a tolerance orders of
  magnitude above a last-bit change, against its error estimate or a stored value. Every MC test
  over `GeneralizedBlackScholesProcess` uses `BlackConstantVol`, except `testMCVarianceSwap` and
  `testChoiAsianEngineVsMC` on a `BlackVarianceCurve`, both statistical; none forces the
  discretization or links an external local volatility, so
  **no QuantLib MC test reaches the Euler branch**, and none pins the exact step on a
  `BlackVarianceCurve`. Nor can a path's terminal value lock it across platforms: `localVolImpl`
  divides by `dy²`, so a last bit that contracts differently moves single bilinear paths by up to
  3% (section 4).

## 3. Candidates

The candidates are numbered by where they live, not by the order of the branches: 3.1 to 3.3 in
the Black-Scholes process and the term structures it reads, 3.4 in the pricers over it, 3.5 the
Heston family, 3.6 Hull-White, 3.7 the multi-asset array. Section 5 orders the branches as a
proof of concept: the hook of section 4 with 3.2 first, the whole gain of the flat branch in one
change, then the Euler-branch changes 3.1 and 3.3, which the first step does not need. It splits
3.3 in two, since the rate pair needs the hook and the slices do not. Measured: 3.1 together with
3.3's rate pair, 3.2, and 3.3's local-vol time slice by prototype. The rest is unmeasured.
"Bit-identical" means without FMA contraction; section 4 keeps it so under contraction.

### 3.1 One Local-Volatility Evaluation per Euler Step

- **Status:** measured, together with 3.3's rate pair, as the smile rows of section 1. Proposed
  with no API change.
- **Lives in:** `GeneralizedBlackScholesProcess::evolve` in `ql/processes/blackscholesprocess.cpp`.
- **Finding:** the Euler branch returns
  `apply(x0, discretization_->drift(...) + stdDeviation(...) * dw)`.
  - `EulerDiscretization::drift` is `process.drift(t0, x0) * dt`, and
    `GeneralizedBlackScholesProcess::drift` calls `diffusion(t, x)` plus two
    `forwardRate(t, t + 0.0001)`.
  - `stdDeviation` goes to `EulerDiscretization::diffusion`, which is
    `process.diffusion(t0, x0) * std::sqrt(dt)`.
  - So `diffusion(t0, x0)`, which under a smile is `LocalVolSurface::localVolImpl`, runs twice per
    step with identical arguments, each time with 5 `blackVariance`, up to 6 `discount`, a `log`
    and 2 `exp`; at `t = 0`, the first step of every path, it differences one-sidedly in time,
    with 4 `blackVariance` and 4 `discount`.
  - The fix calls the virtual `diffusion(t0, x0)` once and builds drift and diffusion from it,
    when `discretization_` is exactly `EulerDiscretization`. A subclass of it may override either
    method, and `EndEulerDiscretization` evaluates at `t0 + dt`.
  - Companion: the forward rate without an `InterestRate`. Each call takes
    `forwardRate(t1, t2, Continuous, NoFrequency, true).rate()`, which builds an `InterestRate`
    through `InterestRate::impliedRate`, a `DayCounter` copy and a refcount round-trip included,
    and keeps one double of it. `std::log(discount(t1, true) / discount(t2, true)) / (t2 - t1)`
    is the same arithmetic as `impliedRate`'s continuous branch and saves 124 of 536 Ir per
    call on the flat setup, bit-identical. Until the hook of section 4 caches the rate pair, 3.1
    leaves it per step, so the companion pays here.
- **Branches:** the Euler branch: a smile, `forceDiscretization`, or an external local volatility.
  It works whatever local volatility is linked.
- **Evidence:** on the smile setup, per step of `evolve`, discrete monitoring:

  | Callee               | Bilinear Ir | Bicubic Ir |
  | -------------------- | ----------: | ---------: |
  | all, path generation |       7,609 |     51,804 |
  | `localVolImpl` × 2   |       5,951 |     50,146 |
  | `blackVariance` × 10 |       2,528 |     46,723 |
  | `discount` × 16      |       2,902 |      2,902 |
  | `forwardRate` × 2    |       1,130 |      1,130 |

  The `discount` row includes the 4 calls inside `forwardRate`. On the bicubic surface
  `BicubicSplineImpl::value` is 88.5% of the run, about 4,599 Ir per lookup, and its
  `shared_ptr` refcount atomics alone 13.9%. With one `localVolImpl` call and the rate pair
  cached, the step lands at 3,168 and 25,265. Inside QuantLib, 3.1 alone keeps the rate pair. If
  both `localVolImpl` calls cost the same, it lands near 4,600 bilinear and 26,700 bicubic; that
  split is derived, not measured.

- **Lock impact:** bit-identical without contraction: `diffusion` is a deterministic function of
  `(t0, x0)`, so the reused value is the double the second call would return. The drift stays
  `((r − q) − (0.5·σ)·σ)·dt` and the diffusion term `(σ·√dt)·dw`.
  - Under contraction the old step's arithmetic is split across `drift()`,
    `EulerDiscretization::drift()` and `EulerDiscretization::diffusion()`, each behind a virtual
    call; the new one computes it in `evolve()` and calls only the virtual `diffusion()`. A
    compiler that contracts only within an expression keeps the old bits when the new code writes
    each old expression as its own statement. GCC contracts across statements by default:
    `-ffp-contract=fast` is its default for C++ in every language mode, and only
    `-ffp-contract=on` stays within one expression (GCC manual, `-ffp-contract`). QuantLib's
    CMake sets neither flag.
  - No QuantLib test would notice either way, so the change comes with one. It pins single steps
    from fixed points within the relative 1e-13 of section 1, and a smile price within a
    statistical tolerance. A path's terminal value cannot serve (section 2).
- **Upstream:** high. Local to one function, no new interface. It keeps calling the virtual
  `diffusion()`, so `VegaStressedBlackScholesProcess`'s override still applies. It no longer calls
  `drift()`. QuantLib's only override of `drift()` is in `ExtendedBlackScholesMertonProcess`, which
  overrides `evolve()` as well. 3.1 changes `evolve()` only; `stdDeviation()`, `variance()` and
  `expectation()` stay, which is what keeps that subclass unaffected, since its Euler and
  predictor-corrector cases call `stdDeviation()`. A subclass outside QuantLib that overrides
  `drift()` alone would lose it silently on the Euler branch, the only one that calls it today,
  which QuantLib's deprecation rules forbid, so the fused step runs behind the exact-type gate of
  section 4 and such a subclass keeps today's path.

### 3.2 The Exact Black-Scholes Step per Grid

- **Status:** measured as the flat row of section 1; proposed on the hook of section 4.
- **Lives in:** `GeneralizedBlackScholesProcess`, overriding the hook; `StochasticProcessArray`
  composing its components' tables.
- **Finding:** the strike-independent branch runs, per step, `localVolatility()` twice, `variance`
  with two `blackVariance` (each `checkRange` → `maxTime` without `extrapolate`), and two
  `forwardRate(t0, t0 + dt, Continuous, NoFrequency, true)`. None depends on `x0` or `dw`. Inside
  QuantLib the hook reads the branch flags, so the volatility type check and the probe drop out;
  the exact-type gate of section 4 stays.
- **Branches:** strike-independent only: `BlackConstantVol`, `BlackVarianceCurve`, no forced
  discretization, no external local volatility.
- **Evidence:** on the flat setup `evolve` accounts for 94.8% of a discrete repricing, 32.38 G
  of 34.18 G, in 16,711,680 calls at 1,938 Ir each:

  | Callee of `evolve`                    | Ir per step | Contents                                                                                                                               |
  | ------------------------------------- | ----------: | -------------------------------------------------------------------------------------------------------------------------------------- |
  | `YieldTermStructure::forwardRate` × 2 |       1,072 | 4 `discount` (each `checkRange`, `LazyObject::calculate`, `exp`), 2 `impliedRate` (each `log`, an `InterestRate`, a `DayCounter` copy) |
  | `variance`                            |         644 | 2 `blackVariance`, each with `checkRange` → `maxTime` (242 Ir per step) and `checkStrike`                                              |
  | `apply`                               |          69 | one `exp`                                                                                                                              |
  | rest                                  |         153 | `Handle::operator->`, `localVolatility()` "trigger update", own code                                                                   |

  Of the 2.68 G that remain after the cache, Sobol plus `InverseCumulativeNormal` take 0.92 G,
  `BrownianBridge::transform` 0.42 G and the cached step 1.17 G, 0.95 G of it in `exp`. How much
  of the 92.15% an engine sees depends on its steps per path: none for the 1-step European and
  barrier tests, the most for `MCLookbackEngine` and `MCDoubleBarrierEngine`, tested at 2,000 and
  5,000 steps.

- **Lock impact:** bit-identical by construction without FMA contraction: the cached step is
  `evolve`'s double on every build measured except gcc at `-march=x86-64-v3` with
  `-D_GLIBCXX_ASSERTIONS`, where contraction moves path points by up to 5.4e-16 relative; the
  shared helper of section 4 removes that. QuantLib pins it through `testPathGenerator` and, via
  `StochasticProcessArray`, `testMultiPathGenerator` and the three `testCached`, all on
  `BlackConstantVol` (section 2). The `BlackVarianceCurve` branch has no bitwise pin, so the
  hook's test adds one.
- **Upstream:** high if the hook is accepted. This is the change that justifies the hook.

### 3.3 Time Slices and the Rate Pair of the Euler Step

- **Status:** the rate pair measured together with 3.1, the local-volatility slice measured by
  prototype, the surface slice unmeasured.
- **Lives in:** the rate pair in `GeneralizedBlackScholesProcess`'s hook, since it needs the grid
  (appendix D).
  The slices in `LocalVolTermStructure` and `LocalVolSurface`, and in `BlackVarianceSurface`;
  they need no hook.
- **Finding:** after 3.1 an Euler step still computes from `t` alone:
  - the rate pair `forwardRate(t, t + 0.0001)` of `drift`, and `√dt`;
  - inside `localVolImpl`, `dr`, `dq` and the forward at `t`, and four discounts at
    `t ± min(1e-4, t/2)`, or two at `t + 1e-4` on the first step, where `t = 0` and
    `localVolImpl` differences one-sidedly; a slice carries that branch;
  - inside the surface, the bilinear time `locate` and weights, or on the bicubic surface the same
    strike spline, rebuilt on every lookup into a freshly allocated section; beyond the last
    expiry, `blackVarianceImpl` scales the variance at `times_.back()` by `t / times_.back()`, so
    a slice there holds that spline and factor. The strike clamp under constant extrapolation
    depends on the strike and stays in the lookup.
  - The rate pair caches as one double `r − q`, exactly, since `drift` evaluates
    `(r − q) − 0.5·σ²` left to right.
  - The local-volatility slice is a virtual on `LocalVolTermStructure` that returns the terms for
    a time, with a default that holds the time only, and a `localVol` overload that takes them
    (appendix C). The surface slice does the same one level down. A slice is a value the caller
    builds per grid time and drops when the surface notifies; it holds the forward
    `underlying_->value() · dq / dr`, a snapshot of an observed `Quote`, and the term structure
    keeps no slice state. The hook's table holds one slice per grid time.
- **Branches:** the Euler branch. The slice is optional for every other local volatility, which
  keeps the default.
- **Evidence:** a prototype that copies `localVolImpl` and feeds it precomputed discounts, the
  computation the local-volatility slice would do, takes the step from 3,168 to 1,670 Ir on the
  bilinear surface and from 25,265 to 23,768 on the bicubic one, on the smile setup after 3.1.
  The surface slice is unmeasured; its ceiling is the interpolation's share of that step,
  `BilinearInterpolationImpl::value` at 33% and `BicubicSplineImpl::value` at 92%, below the
  checks and clamps of `blackVariance`, against 35% and 4% for the six `discount` calls the first
  slice removes. On
  the bicubic surface it replaces a spline construction of about 4,599 Ir per lookup by a
  one-dimensional spline evaluation. The rate pair is 1,130 Ir per step on the smile curves.
- **Lock impact:** bit-identical. `LocalVolSurface`'s own `localVolImpl` goes through the slice
  code. The slice holds whole subexpressions of today's formula: `strike·dr·dqpt / (drpt·dq)`
  parses as `((strike·dr)·dqpt) / (drpt·dq)`, so it holds `dr`, `dqpt` and the product `drpt·dq`,
  never their ratio. The bicubic slice builds the same spline from the same section; the bilinear
  one stays exact only if it keeps the four corner values and the existing formula. 3.1's step
  test covers all of it.
- **Upstream:** medium. The first slice adds API to a base class, optional for every other local
  volatility.

### 3.4 Per-Step Volatility in the Barrier and Digital Pricers

- **Status:** proposed, on the hook.
- **Lives in:** `BarrierPathPricer` in `ql/pricingengines/barrier/mcbarrierengine.cpp` and
  `DigitalPathPricer` in `ql/pricingengines/vanilla/mcdigitalengine.cpp`.
- **Finding:** both call `diffProcess_->diffusion(...)` every step for the Brownian-bridge crossing
  probability: the barrier pricer at `(timeGrid[i], path[i])`, the digital one at `timeGrid[i+1]`
  and the level it accumulates in log space up to step `i`.
  - On the strike-independent branch the value depends on `t` alone. `LocalConstantVol` returns
    its quote, and `LocalVolCurve::localVolImpl` makes two `blackVariance` calls, at `t` and
    `t + 1/365`. Once 3.2 removes the step's own calls, this is the only call into the process
    left in those loops, and the pricer takes it from the table.
  - Under a smile the barrier pricer's point is the one the generator just evaluated for step `i`:
    a third `localVolImpl` per step. A continuously monitored pricer that calls the process's
    `variance()` on every step of every path above the barrier spends 26.3% of the cached
    bilinear run on that third call, 19.11 G of 72.79 G. A `Path` carries values only, so reusing
    the generator's σ needs a side channel. The hook's table can be it, recording each step's σ
    for the pricer. The digital pricer's point is no generator point, and it reads up to the last
    grid point, so the table holds σ at every grid point, one more than the steps.
- **Branches:** both; the smile part for the barrier pricer only, and the barrier engine only on
  its unbiased `BarrierPathPricer`, since `BiasedBarrierPathPricer` makes no `diffusion()` call.
- **Evidence:** unmeasured on QuantLib's own pricers. The 26.3% is the continuous pricer of the
  section 1 instrument, which makes the same per-step call.
- **Lock impact:** bit-identical. The table stores the virtual `diffusion()` at the grid time,
  and under a smile the σ the generator computed at the same `(t, x)`.
  `VegaStressedBlackScholesProcess` overrides `diffusion()` with an `S`-dependent stress; the
  exact-type gate of section 4 leaves it on `evolve()`. QuantLib's barrier tests run one step;
  `testMCCashAtHit` runs 45.
- **Upstream:** medium. Two pricers change, and the default hook must offer "no table".

### 3.5 Heston, Bates, GJR-GARCH and Heston SLV

- **Status:** proposed, on the hook.
- **Lives in:** `HestonProcess`, `BatesProcess`, `GJRGARCHProcess`, `HestonSLVProcess` in
  `ql/processes/`.
- **Finding:**
  - Every Heston scheme computes the rate pair `forwardRate(t0, t0 + dt, Continuous)` per step,
    with the default `Annual` and `extrapolate = false`, unlike the Black-Scholes process. The
    table keeps those arguments, or the range check changes.
  - QE and QEM, the latter `HestonProcess`'s default, also compute `exp(−κ·dt)`, `k1`–`k4`, `A`
    and the second summand of `s2` per step; QE its `k0` too. The first summand of `s2` starts from
    `x0[1]`, and QEM's `k0` is path-dependent, so neither caches. `BatesProcess` and
    `GJRGARCHProcess` default to `FullTruncation`; the engines take the scheme from the process.
  - The truncation and reflection schemes cache the rate pair, `√dt` and `√(1 − ρ²)`.
  - `GJRGARCHProcess::evolve` recomputes `CumulativeNormalDistribution()(lambda_)`, an `exp`, four
    `sqrt` and some fifty to sixty flops from parameters fixed at construction, on every step.
  - `BatesProcess` runs `InverseCumulativePoisson(lambda_·dt)(p)` per step, summing Poisson terms
    (an `exp`, a `pow` and a factorial table lookup each) from zero until the partial sum reaches
    `p`, which can be as high as `1 − QL_EPSILON`. A per-step table of the partial sums
    reproduces the loop exactly up to a fixed length, with the loop as the fallback past it;
    `−lambda_·m_·dt` caches as one double.
  - `HestonSLVProcess` caches the rate pair, `exp(−κ·dt)`, `κ·θ·dt`, `√(1 − ρ²)` and the second
    summand of `s2`, the QE terms; the leverage function `localVol(t0, x0[0], true)` stays per
    path.
    `HestonSLVMCModel` loops steps outer, so a per-step hoist there pays even without the hook.
- **Branches:** all schemes except non-central chi-square and Broadie-Kaya, where a root solve per
  step dominates (section 6).
- **Evidence:** unmeasured. What remains per QE step is one `exp` and three `sqrt`, or one `exp`,
  one `sqrt`, an `erf` and a `log`, QEM adding a `log` to either branch, so the relative gain
  stays below the Black-Scholes 92%.
- **Lock impact:** bit-identical when cached as listed. QuantLib's Heston-family MC tests, in
  `hestonmodel`, `batesmodel`, `gjrgarchmodel`, `hestonslvmodel`, `asianoptions` and
  `forwardoption`, compare against an error estimate (`testMcVsCached` at 2.34 σ) or at a fixed
  tolerance no tighter than 5e-4 relative (GJR-GARCH at 0.15 against stored MC values) and would
  not notice a last-bit change; the `hybridhestonhullwhiteprocess` and `fdmlinearop` MC tests
  step the hybrid's own `evolve()`, which never calls `HestonProcess::evolve`. So the change adds
  a path pin for one QEM and one full-truncation path.
- **Upstream:** medium to high; each process changes alone. GJR-GARCH's constants go in the table,
  not the constructor (section 4).

### 3.6 Hull-White, Its Forward-Measure Version and the Hybrid

- **Status:** proposed, on the hook.
- **Lives in:** `HullWhiteProcess`, `HullWhiteForwardProcess` in
  `ql/processes/hullwhiteprocess.cpp`; `HybridHestonHullWhiteProcess`.
- **Finding:**
  - The expectation is the Ornstein-Uhlenbeck one plus `alpha(t0 + dt) − alpha(t0)·exp(−a·dt)`,
    where `alpha(t)` ends in `h_->forwardRate(t, t, Continuous, NoFrequency)`. Per step: two
    `forwardRate`, five `exp`, `exp(−a·dt)` among them twice, and a `sqrt`. The forward-measure
    version adds `M_T(t0, t0 + dt, T_)`, three more `exp`. Only `(x0 − level)·exp(−a·dt)` and
    `stdDeviation(dt)·dw` depend on the path; the standard deviation, computed as
    `√(0.5·σ²/a · (1 − exp(−2a·dt)))` and cached in that form, is the cached `sqrt`. Its small-`a`
    limit switches at `√QL_EPSILON`, while `alpha`, `M_T` and `B` switch at `QL_EPSILON`.
  - The hybrid, in its `BSMHullWhite` discretization, computes about 10 `discount`, 5 `log` and 14
    of its 15 `exp` per step from `(t0, dt)` alone, the last, `exp(mu + vol)`, being
    path-dependent, and evaluates `alpha(t0)` and the OU variance twice each.
  - Neither `HullWhiteProcess` nor the hybrid registers with anything, and the hybrid's `update()`
    notifies no one. A per-calculate table does not need them to (section 4).
  - Only `HullWhiteForwardProcess` has an engine that steps it: `MCHullWhiteCapFloorEngine` builds
    one in `pathGenerator()` on every `calculate()`, and the hybrid wraps one. `HullWhiteProcess`
    has the same expectation without `M_T` and gets the same table; inside QuantLib nothing steps
    it, `FdHestonHullWhiteVanillaEngine` takes one but reads only `a()` and `sigma()`, so only a
    direct `PathGenerator` user reaches its table.
- **Branches:** all; the hybrid's Euler discretization drops one variance.
- **Evidence:** unmeasured. `forwardRate(t, t)` takes two `discount` 1e-4 apart and an
  `impliedRate`, the work of the Black-Scholes rate pair, plus one `checkRange` without
  `extrapolate`, which copies the `DayCounter` and which the Black-Scholes pair skips by passing
  `true`. Those copies are what callgrind undercounts: on the flat setup one repricing executes
  12 lock-prefixed instructions per step, 8 of them `DayCounter` copies on the `forwardRate`
  path and 4 in `maxTime`, each one Ir to callgrind and about 20 cycles on hardware.
- **Lock impact:** bit-identical without FMA contraction, with `alpha(t0 + dt)`,
  `alpha(t0)·exp(−a·dt)` and `M_T` as three separate doubles, since the OU term is leftmost and
  path-dependent. Inside `M_T`, `t − s` is
  `(t0 + dt) − t0`, not `dt`. The hybrid keeps `m4` and `m5` separate. `test-suite/gsr.cpp` pins
  `HullWhiteForwardProcess::expectation` and `variance` against `GsrProcess` at 1e-8, so a step
  helper that `expectation()` shares with the table keeps its arithmetic.
  `MCHullWhiteCapFloorEngine` has no test: the change adds one. The hybrid's tests are
  statistical.
- **Upstream:** medium. No QuantLib model reaches either process: `HullWhite::Dynamics`, and with
  it `TrinomialTree`, run on an `OrnsteinUhlenbeckProcess`, `Gaussian1dModel` on `GsrProcess` or
  `MfStateProcess`, and `FdHestonHullWhiteVanillaEngine` reads `a()` and `sigma()` only, though
  its `FdmHestonHullWhiteSolver` registers with the `HullWhiteProcess`, so the registration of
  section 4 starts notifying it on a curve move.

### 3.7 Allocation-Free Multi-Asset Steps

- **Status:** proposed. Settles the multivariate hook's signature, the fifth step of section 5.
- **Lives in:** `StochasticProcessArray::evolve`, `MultiPathGenerator::next`, and every
  multi-factor `evolve()` returning an `Array`.
- **Finding:** `StochasticProcessArray::evolve` allocates `dz = sqrtCorrelation_ * dw` and the
  result on every step, `MultiPathGenerator` move-assigns that result, and Heston and GJR-GARCH
  allocate `retVal`, Bates through Heston's. The correlation product depends on `dw` and cannot
  be cached. This is
  not precomputation: it is what remains around the arithmetic once 3.2 and 3.5 remove the curve
  calls. The multivariate step writes into a caller-owned `Array` instead.
- **Branches:** the seven `StochasticProcessArray` engines and the Heston family.
- **Evidence:** unmeasured. On the flat setup DHAT finds no per-step allocation: one repricing
  allocates 119 blocks and 2.22 MB, 94.6% of the bytes in the doubling growth of
  `GeneralStatistics`' sample vector. Under a bicubic smile the surface allocates on every lookup,
  which 3.3's surface slice addresses. A basket needs its own run.
- **Lock impact:** bit-identical: same arithmetic, other storage. `testMultiPathGenerator` and the
  three `testCached` pin it.
- **Upstream:** medium. The `Array`-returning `evolve()` stays.

## 4. API Inside QuantLib

| Option                                                                                                   | For                                                                                                                                                                                                   | Against                                                                                                                                                                                                                                                                                  |
| -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A.** A hook on the process takes a `TimeGrid` and returns a step table; the generators step through it | Reaches both generators and every process through one mechanism; no shared mutable state; built per generator, rebuilt on notification; the process knows its own branch; adds and deprecates nothing | A new virtual in both core base classes and a public table type, each kept five releases after a future deprecation; the generators become observers; a subclass gets the table only when its exact type is known                                                                        |
| **B.** Each process memoizes its terms inside `evolve()`, keyed on `(t0, dt)`, cleared in `update()`     | No API change; every caller benefits, `HestonSLVMCModel` included                                                                                                                                     | Grows without bound across grids, as `GsrProcessCore` does; goes stale for processes that observe nothing (Hull-White, the hybrid, the SLV leverage); a lookup per step; with an AD type for `QL_REAL`, records a memoized value on whichever tape was active when it was first computed |
| **C.** A second path-generator type with its own traits, stepping through a table it builds itself       | Changes no existing behaviour                                                                                                                                                                         | Every concrete engine fixes its traits, so no engine benefits without new template parameters                                                                                                                                                                                            |

**Recommendation: A,** under QuantLib's deprecation rule: a public API is never changed, only
deprecated with its replacement in place, and a feature deprecated in release N goes in N + 5, as
`News.md` at the pin shows for 1.39 and 1.44. The hook deprecates nothing, so the rule binds what
it adds, which stays supportable for years, and what it may change, which is nothing a caller sees
today.

- **Shape:** `StochasticProcess1D` gains a virtual, `stepTable1D()`, that takes a `TimeGrid` and
  returns a step table with `evolve(i, x0, dw)`, `evolvePath()` for a whole path in one call and,
  for 3.4, each grid point's σ where a pricer can reuse it; the suffix keeps it apart from the
  multivariate hook (appendix A). The default forwards to `evolve(grid[i], x0, grid.dt(i), dw)`,
  so every process and subclass that does not override it behaves exactly as today.
  `StochasticProcess` gains the multivariate counterpart, writing in place (3.7); the
  `Array`-returning `evolve()` stays the public step, undeprecated, the in-place overload defaults
  to it, and `MultiPathGenerator` moves to the overload, as the checklist asks of internal
  callers. `ql/experimental/` is no home for any of it: a virtual on `StochasticProcess1D` cannot
  live there, and `ql/processes/` cannot depend on it. The public surface therefore stays at what
  the generators need, two virtuals and two table types, plus one inspector on
  `GeneralizedBlackScholesProcess` for the branch its step takes,
  which its builder and a subclass read instead of the private flags (appendix A); anything more,
  once shipped, is kept five releases after its deprecation. `News.md` lists
  the change as a performance improvement, not among the deprecations.
- **Lifetime:** the generator builds the table lazily, on the first `next()`, and rebuilds it when
  the process notifies: `PathGenerator` and `MultiPathGenerator` gain an `Observer` base and
  register with the process, an `Observable` through `StochasticProcess`. A direct user who moves
  the market between two `next()` calls sees the new market, as today, so no behaviour changes
  and nothing goes into `News.md` for it; the table is a snapshot only between notifications.
  The one exception is notifications dropped by `ObservableSettings::disableUpdates(false)`,
  after which the generator needs an explicit `update()`, as a `LazyObject` does; its class
  documentation says so.
  `GeneralizedBlackScholesProcess`, `HestonProcess`, `GJRGARCHProcess` and
  `StochasticProcessArray` already register with their inputs; `HullWhiteProcess` and
  `HullWhiteForwardProcess` register with their curve handle, and the hybrid, whose `update()`
  recomputes `endDiscount_` and notifies no one, with its two component processes, each
  forwarding `update()` when it gets its hook, as every price input of an `Observable` should.
  Building the table in the constructor would
  make an empty handle that is linked later fail, for a process whose constructor registers
  without dereferencing, as `GeneralizedBlackScholesProcess` does; the Hull-White constructors
  already dereference the curve. Engines are unaffected, because they build a new generator per
  `calculate()`.
- **Everything cached lives in the table,** GJR-GARCH's constants included, never in a
  constructor. Design opinion, not decidable from the source: with an AD type for `QL_REAL`, a
  table built per `calculate()` is recorded on the pricing tape, a memoized value on whichever
  tape was active when it was first computed, which is held against option B.
- **Floating point:** the process's own `evolve()` calls the same inline step helper as its table,
  so both compile the same expression. That removes the cause of the smile rows' differences in
  section 1: a step built as one expression next to an `evolve()` split across virtual calls lets
  a compiler that contracts to FMAs fuse the two differently. `LocalVolSurface::localVolImpl`
  divides second differences in strike by `dy²`, with `dy` as small as 1e-6, so a last bit turns
  into a different σ and single bilinear paths end up to 3% apart, bicubic ones about 1e-5, while
  no path changes its knock-out status. The prices, one repricing at 2^16 paths built with
  `-march=x86-64-v3`, cached against uncached, relative:

  | Compiler | Surface  | Discrete | Continuous |
  | -------- | -------- | -------: | ---------: |
  | gcc      | bilinear |  −5.4e-7 |    −7.0e-7 |
  | gcc      | bicubic  |  −3.0e-9 |    −3.0e-9 |
  | clang    | bilinear |  +2.3e-7 |    −1.9e-6 |
  | clang    | bicubic  |  −6.1e-9 |    −1.9e-8 |

  The Monte Carlo standard error is about 8.4e-4 relative, and the uncached engine itself moves
  by 8.2e-6 between a contracting gcc build and a non-contracting one, bilinear discrete. The
  x86-64 baseline and MSVC at its default `/fp:precise` since Visual Studio 2022 do not contract;
  every arm64 build, x86-64 with FMA enabled and MSVC under `/fp:fast` do. A bitwise step test
  proves the shared helper on such a tree, or on QuantLib's macOS CI, whose `macos-15` and
  `macos-26` runners are arm64.

- **Exact-type gate:** a subclass that overrides a virtual the step runs through, `evolve()`,
  `drift()`, `diffusion()`, `variance()`, `stdDeviation()` or `apply()`, would inherit a table that
  ignores the override, a silent change to a public contract, which the rules forbid; a documented
  contract plus a probe under `QL_EXTRA_SAFETY_CHECKS`, which only the weekly and dispatch-only
  workflows enable,
  does not prevent it. So `GeneralizedBlackScholesProcess` builds a table only when `typeid(*this)`
  is one of the five classes `ql/processes/blackscholesprocess.hpp` declares, the four thin
  subclasses adding constructors only. Every other subclass keeps `evolve()` until it overrides
  the hook itself, calling a protected builder when its overrides leave the step intact.
  `ExtendedBlackScholesMertonProcess` and `VegaStressedBlackScholesProcess` then need no override,
  and a subclass outside QuantLib sees no change. The same gate guards 3.1, whose fused step no
  longer calls `drift()`: decided once per process there, on the first `evolve()`, and kept in a
  flag (appendix B); once per grid in the hook. `typeid` has no use in `ql/` yet, but RTTI is
  already required, since
  `dynamic_pointer_cast` appears in 219 of its files. A probe that guesses the branch at two
  points per step, which a cache outside QuantLib needs, adds nothing to an exact type; the step
  tests pin the table against `evolve()` instead.

## 5. Sequence of Upstream Changes

Each step is one branch, measured on the setups of section 1 before the next one starts. The
order is a proof of concept: first the change with the largest measured gain in the fewest
files, then the Euler-branch changes, none of which the first step needs.

1. The hook with 3.2, one-dimensional only: `StochasticProcess1D::stepTable1D()` with its
   forwarding default, the override in `GeneralizedBlackScholesProcess` behind the exact-type
   gate, and `PathGenerator` building the table on its first path and dropping it when the
   process notifies. The observation stays in: it is what keeps a direct `PathGenerator` user
   correct. Left out of this step: the multivariate overload of 3.7 and with it
   `MultiPathGenerator`, the branch inspector, and the table's σ column. The two flat-branch
   findings section 6 drops, `extrapolate = true` in `variance` and the forward rate without
   `InterestRate`, stay dropped: this step takes those calls out of the path loop. Measured as
   the first item of section 7.
2. 3.1 behind the same gate, with its step test and the forward-rate helper. No process API;
   QuantLib's own engines gain under a smile.
3. 3.3's time slices, local volatility first, then the surface. No process API; a cache outside
   QuantLib gains from them unchanged.
4. 3.3's rate pair and 3.4, on the hook, with the inspector and the σ column.
5. 3.7: the multivariate signature and `MultiPathGenerator`, which brings the table to the
   seven `StochasticProcessArray` engines.
6. 3.5, one process per branch.
7. 3.6, with the missing `MCHullWhiteCapFloorEngine` test.

## 6. Findings Dropped or Adjacent

| Finding                                                                          | Why it is not a candidate                                                                                                                                                                                                                      |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `OrnsteinUhlenbeckProcess`: two `exp` and a `sqrt` per step, all from `dt`       | Small; `HullWhiteProcess`, `HullWhiteForwardProcess`, `G2Process` and `G2ForwardProcess` wrap it, so 3.6 covers it; alone, only `testPathGenerator`, `testMultiPathGenerator` and `testMoustacheGraph` drive it                                |
| `G2Process`: two `forwardRate`, 11 `exp`, 4 `sqrt`, 4 allocations per step       | No engine drives it through a path generator; one test does                                                                                                                                                                                    |
| `MfStateProcess`: the whole `stdDeviation` depends on `(t, dt)`                  | No Monte Carlo caller; Gaussian1d integration only                                                                                                                                                                                             |
| `CoxIngersollRossProcess`, `GeometricBrownianMotionProcess`, `SquareRootProcess` | CIR caches `exp(−a·dt)` and the state-free summand of `s2`, as QE does; GBM and the square-root process `√dt` only; the rest depends on the state. `testPathGenerator` locks GBM and the square-root process (section 2); CIR has no path lock |
| `GsrProcess`                                                                     | Already memoizes in `GsrProcessCore`; a table would only replace four map lookups per step                                                                                                                                                     |
| `JointStochasticProcess`                                                         | Already memoizes its correlation; no concrete subclass in QuantLib                                                                                                                                                                             |
| Heston non-central chi-square and Broadie-Kaya schemes                           | A root solve per step dominates whatever is cached                                                                                                                                                                                             |
| `MCVarianceSwapEngine`'s per-node `diffusion()`                                  | Its nodes are `SegmentIntegral` sums, not grid times, so no table reproduces them                                                                                                                                                              |
| Forward rate without `InterestRate`                                              | Kept as 3.1's companion; once the hook caches the rates it pays only outside path generation                                                                                                                                                   |
| `extrapolate = true` in `GeneralizedBlackScholesProcess::variance`               | 4.35 G Ir, 12.7% of a flat discrete repricing, bit-identical; but only the exact branch calls it, and 3.2 takes the call out of the path loop altogether                                                                                       |
| `TermStructure::maxTime` copying the `DayCounter`                                | 4 lock-prefixed instructions per step on the exact branch (3.6); leaves the per-path loop with 3.2; a term-structure change, not a step cache                                                                                                  |
| Sobol, `InverseCumulativeNormal`, `BrownianBridge`, least-squares regression     | Nothing grid-only is left to cache; folding the bridge's `1/√dt` into the step would move bits                                                                                                                                                 |
| Paths in log space                                                               | Moves every bitwise lock, and the local volatility needs the price level every step                                                                                                                                                            |

## 7. Measurements That Decide the Ranking

Each is a callgrind count of one repricing at 2^16 paths on the setups of section 1, `-O3`,
no FMA, QuantLib static except where section 1 says shared, against the same build without the
change. Wall clock is secondary.

1. **3.2:** the flat setup through QuantLib's own `evolve()`, 34.18 G discrete on the shared
   build and 33.02 G static, against the hook branch. The criterion: plain `SingleVariate` lands
   within 1% of 2.68 G, with the NPV unchanged,
   discrete `100.22671167047309` and continuous `99.883605619178169`. Choosing a generator type
   explicitly instead of through the traits costs 0.6% on its own, from one register spill across
   `exp`. Met: section 8.
2. **3.1:** the smile setup through QuantLib's own `evolve()`, against 127.22 G and 865.79 G
   uncached. The criterion: the NPVs stay bit-identical on a non-contracting build, bilinear
   discrete `100.23201398081022` and continuous `99.886405693076455`, bicubic discrete
   `100.27835252928404` and continuous `99.932975609578065`.
3. **3.3's time slices:** the smile setup, bilinear and bicubic, through the Euler step with one
   `localVolImpl` and cached rates, against 52.69 G and 422.01 G discrete.
4. **3.4:** `MCBarrierEngine` on the smile setup with a continuous-monitoring step count,
   callgrind before and after the table. Until then the 26.3% of 3.4 is a proxy.
5. **3.7:** DHAT on `MCEuropeanBasketEngine`, allocations per step before and after.
6. **3.5:** a Heston QEM workload with daily steps, `Actual360`, `NullCalendar`, continuously
   compounded flat curves, daily steps over the `Actual360` year fraction, through
   `MCEuropeanHestonEngine`.
7. **3.6:** a Hull-White cap/floor priced by `MCHullWhiteCapFloorEngine`, which reaches
   `HullWhiteForwardProcess` only, and `HullWhiteProcess` through `PathGenerator` directly; the
   same conventions.

## 8. Step 1 Measured

Step 1 of section 5 is implemented on QuantLib `v1.44-15-gfbec07c67` with appendix A's design:
`stepTable1D()` with its forwarding default, `evolvePath()`, the exact table behind the gate,
`lognormalDrift` and `lognormalIncrement` shared with `evolve()`, and `PathGenerator` observing
its process. As section 5 plans, the inspector, the σ column and the multivariate table are left
out. None of the five files it touches changed between `966a4cc10` and that commit.

The setups are those of section 1 with the barrier at 70, so the NPVs differ from section 7's. Two
workloads run on them: the bonus option through its Monte Carlo engine on plain `SingleVariate`,
and its down-and-out put leg through `MCBarrierEngine`. The put's discrete monitoring goes through
`BiasedBarrierPathPricer` and its continuous monitoring through `BarrierPathPricer`.

The build is clang 23.1.3, `-O3 -DNDEBUG`, baseline x86-64 without FMA contraction, with debug info
and frame pointers on every target and QuantLib linked statically. Each count is one repricing
under callgrind (valgrind 3.27.1) at 2^16 paths, against the same build at `fbec07c67`:

| Workload, flat setup |  Before |    After |  Saved | NPV, before and after |
| -------------------- | ------: | -------: | -----: | --------------------- |
| bonus, discrete      | 33.07 G |  2.676 G | 91.91% | 115.4999787787627     |
| bonus, continuous    | 44.75 G | 14.355 G | 67.92% | 115.1799377705109     |
| put, discrete        | 33.16 G |  2.763 G | 91.67% | 18.49732387605334     |
| put, continuous      | 40.39 G | 10.004 G | 75.23% | 18.174165037327128    |

- **The criterion of section 7 is met.** Plain `SingleVariate` lands at 2,676,482,445 Ir. The
  step cache of section 1 lands at 2,675,790,419 on the same build, so the table is 0.03% above
  it. Every NPV is the same double before and after.
- **Per step:** `evolve()` took 1,870 Ir. `evolvePath()` takes 73 Ir, 57 of them in `exp`; the
  cache's step takes 68. Building the table costs 0.46 M Ir, about one `evolve()` per grid
  point. What remains of the discrete repricing is Sobol with `InverseCumulativeNormal` at
  0.92 G, `BrownianBridge::transform` at 0.42 G and the step at 1.22 G.
- **The path-level call and the local copy, measured:**
  - With one virtual `evolve(i, x0, dw)` per step instead of `evolvePath()`, the discrete
    repricing lands at 2,975,064,339 Ir, 11.2% above the cache.
  - The copy of the table that `next()` holds for each path costs 786,441 Ir, 12 Ir per path.
- **Continuous monitoring keeps its pricers' cost.** For the put, `BarrierPathPricer` takes
  7.43 G of the 10.00 G. Of that, 2.87 G is the per-step `diffusion()` call that 3.4's σ column
  caches; the rest is the crossing probability and its uniform draws. The bonus option's own
  continuous pricer takes 11.78 G of the 14.35 G.
- **The Euler branch is unchanged.** It runs on the forwarding table, with one virtual call per
  step to the process as before. Both workloads on the bilinear smile at 256 paths move by less
  than 0.1% with identical NPVs: 501.3 M to 500.8 M Ir discrete and 682.9 M to 682.4 M continuous
  for the bonus option, 501.7 M to 501.2 M and 716.9 M to 716.4 M for the put.
- **FMA contraction:** built with clang 23.1.3 and with gcc 15.2.0, both at
  `-march=x86-64-v3 -D_GLIBCXX_ASSERTIONS`, `evolve()`, the table builder and the table's step
  all contain `vfmadd`. The step tests still pin the table to `evolve()` bitwise there, and
  `testPathGenerator`, `testMultiPathGenerator`, `BrownianBridgeTests` and the three `testCached`
  pass. The whole QuantLib test suite passes on the non-contracting build.

Wall clock is secondary. It comes from google-benchmark on the same build without debug info,
as the median of 5 repetitions of 10 repricings each, every coefficient of variation at or below
2.7%. The machine is an Intel i9-13950HX laptop under WSL2, which has no hardware counters:

| Workload, flat setup |  Before |  After | Saved |
| -------------------- | ------: | -----: | ----: |
| bonus, discrete      | 1901 ms | 156 ms | 91.8% |
| bonus, continuous    | 2497 ms | 747 ms | 70.1% |
| put, discrete        | 1910 ms | 159 ms | 91.7% |
| put, continuous      | 2257 ms | 492 ms | 78.2% |

The step cache of section 1 runs both bonus workloads at 154.5 and 294.3 ms after the change, and
at 156.1 and 297.7 ms before it. Nothing it runs changed, so that difference is the spread of a
WSL2 laptop.

## Appendix: Sketches of 3.2, 3.1, 3.3 and the Euler Table

How the first four changes of section 5 would look in QuantLib at `966a4cc10`, in that order:
the exact step table of 3.2 on the hook, the fused Euler step of 3.1, the local-volatility time
slice of 3.3, the Euler table that puts 3.3's rate pair and 3.4 on the hook, and then what a
caller sees. Every listing is a sketch, uncompiled, against the file it names, and the prose
around it says what to notice. Each sketch shows the whole design of its section; where
section 5 ships a step without part of it, the prose says which part. All four touch existing
files only, so nothing is registered in CMake, Autotools or the Visual Studio solution. The
sketches follow sections 3.2, 3.1, 3.3, 3.4 and 4; where one forces a decision those sections
leave open, the prose says so.

### A. 3.2: The Exact Black-Scholes Step per Grid

Three places change: `StochasticProcess1D` gets the hook and its default,
`GeneralizedBlackScholesProcess` overrides it behind the gate, and `PathGenerator` steps through
the table.

**The hook.** `ql/stochasticprocess.hpp`, inside `StochasticProcess1D`:

```cpp
    class StochasticProcess1D : public StochasticProcess {
      public:
        ...
        //! the steps of this process along a fixed time grid
        /*! A table is a snapshot of the process between two notifications: a path
            generator builds one on its first path and drops it when the process
            notifies. evolve(i, x0, dw) returns the double that
            evolve(grid[i], x0, grid.dt(i), dw) returns.
        */
        class StepTable1D {
          public:
            virtual ~StepTable1D() = default;
            //! the process's evolve() over step i of the grid
            virtual Real evolve(Size i, Real x0, Real dw) const = 0;
            /*! writes x0 into path[0] and evolve(i, path[i], dw[i]) into path[i+1]
                for each grid step i, with -dw[i] if antithetic is set; the default
                calls evolve() per step, an override saves that call.
            */
            virtual void evolvePath(Real x0, const std::vector<Real>& dw,
                                    bool antithetic, Real* path) const;
            /*! \f$ \sigma(t_i, x) \f$ at grid point i where it depends on
                \f$ t_i \f$ alone, Null<Real>() otherwise; for a pricer that
                evaluates the diffusion at grid points (3.4).
            */
            virtual Real diffusion(Size i) const { return Null<Real>(); }
        };
        /*! returns a step table over the grid. The default forwards every step
            to evolve(), so a process that does not override this method behaves
            as today. An override returns its own table only when the step is
            known to be this class's (the exact-type gate of section 4).
        */
        virtual ext::shared_ptr<const StepTable1D> stepTable1D(const TimeGrid& grid) const;
        ...
    };
```

Its default, `ql/stochasticprocess.cpp`:

```cpp
    void StochasticProcess1D::StepTable1D::evolvePath(Real x0, const std::vector<Real>& dw,
                                                      bool antithetic, Real* path) const {
        path[0] = x0;
        for (Size i = 0; i < dw.size(); ++i)
            path[i + 1] = evolve(i, path[i], antithetic ? -dw[i] : dw[i]);
    }

    namespace {

        class ForwardingStepTable : public StochasticProcess1D::StepTable1D {
          public:
            ForwardingStepTable(const StochasticProcess1D& process, TimeGrid grid)
            : process_(process), grid_(std::move(grid)) {}
            Real evolve(Size i, Real x0, Real dw) const override {
                return process_.evolve(grid_[i], x0, grid_.dt(i), dw);
            }
            // one virtual call per step, to the process, as before step tables
            void evolvePath(Real x0, const std::vector<Real>& dw,
                            bool antithetic, Real* path) const override {
                path[0] = x0;
                for (Size i = 0; i < dw.size(); ++i)
                    path[i + 1] = process_.evolve(grid_[i], path[i], grid_.dt(i),
                                                  antithetic ? -dw[i] : dw[i]);
            }
          private:
            const StochasticProcess1D& process_;
            TimeGrid grid_;
        };

    }

    ext::shared_ptr<const StochasticProcess1D::StepTable1D>
    StochasticProcess1D::stepTable1D(const TimeGrid& grid) const {
        return ext::make_shared<ForwardingStepTable>(*this, grid);
    }
```

The table refers to its process by reference; the generator that holds the table holds the
process's `shared_ptr` too, so the reference outlives it.

**The multivariate counterpart, signature only (3.7).** `StochasticProcess` gets the in-place
step that `MultiPathGenerator` moves to, and the table that returns it; the `Array`-returning
`evolve()` stays the public step. Step 1 of section 5 leaves `StochasticProcess` untouched and
step 5 adds this; it is sketched here because the one-dimensional names are chosen against it. A
`stepTable()` on `StochasticProcess1D` with this signature would override the virtual below
rather than hide it, and a `shared_ptr` return type cannot be covariant, so the declaration would
not compile. The one-dimensional hook is therefore `stepTable1D()`, returning a `StepTable1D`, and
a one-dimensional process inherits this pair unchanged:

```cpp
    class StochasticProcess : public Observer, public Observable {
      public:
        ...
        /*! writes evolve(t0, x0, dt, dw) into out; the default calls that
            overload, an override avoids its allocation.
        */
        virtual void evolve(Time t0, const Array& x0, Time dt, const Array& dw,
                            Array& out) const;
        class StepTable {
          public:
            virtual ~StepTable() = default;
            virtual void evolve(Size i, const Array& x0, const Array& dw,
                                Array& out) const = 0;
        };
        virtual ext::shared_ptr<const StepTable> stepTable(const TimeGrid& grid) const;
        ...
    };
```

**The override.** `ql/processes/blackscholesprocess.hpp`:

```cpp
    class GeneralizedBlackScholesProcess : public StochasticProcess1D {
      public:
        ...
        Real evolve(Time t0, Real x0, Time dt, Real dw) const override;
        /*! the exact lognormal step per grid step on the strike-independent
            branch, the default table elsewhere and for every subclass but the
            four declared below
        */
        ext::shared_ptr<const StepTable1D> stepTable1D(const TimeGrid& grid) const override;
        //@}
        //! \name Inspectors
        //@{
        ...
        const Handle<LocalVolTermStructure>& localVolatility() const;
        /*! whether evolve() takes the exact lognormal step: a strike-independent
            Black volatility, no forced discretization and no external local
            volatility. Triggers the same update as localVolatility().
        */
        bool usesExactStep() const;
        //@}
      protected:
        /*! the exact table on the strike-independent branch, the forwarding
            table otherwise; for a subclass whose overrides leave the step
            intact and which overrides stepTable1D() to say so
        */
        ext::shared_ptr<const StepTable1D> exactStepTable(const TimeGrid& grid) const;
        ...
    };
```

`ql/processes/blackscholesprocess.cpp`:

```cpp
    #include <typeinfo>

    namespace {

        // The exact-type gate of section 4: the five classes blackscholesprocess.hpp declares,
        // whose step is this file's. Any other subclass keeps today's evolve().
        bool isPlainBlackScholes(const GeneralizedBlackScholesProcess& p) {
            const std::type_info& t = typeid(p);
            return t == typeid(GeneralizedBlackScholesProcess)
                || t == typeid(BlackScholesProcess)
                || t == typeid(BlackScholesMertonProcess)
                || t == typeid(BlackProcess)
                || t == typeid(GarmanKohlagenProcess);
        }

        // The drift and the exponent of the exact lognormal step: the expressions evolve()
        // and the table share, so that a compiler that contracts to FMAs fuses both the
        // same way.
        inline Real lognormalDrift(Rate r, Rate q, Time dt, Real var) {
            return (r - q) * dt - 0.5 * var;
        }

        inline Real lognormalIncrement(Real stdDev, Real dw, Real drift) {
            return stdDev * dw + drift;
        }

        class ExactStepTable : public StochasticProcess1D::StepTable1D {
          public:
            ExactStepTable(std::vector<Real> stdDeviation,
                           std::vector<Real> drift,
                           std::vector<Real> sigma)
            : stdDeviation_(std::move(stdDeviation)), drift_(std::move(drift)),
              sigma_(std::move(sigma)) {}
            Real evolve(Size i, Real x0, Real dw) const override { return step(i, x0, dw); }
            void evolvePath(Real x0, const std::vector<Real>& dw,
                            bool antithetic, Real* path) const override {
                path[0] = x0;
                for (Size i = 0; i < drift_.size(); ++i)
                    path[i + 1] = step(i, path[i], antithetic ? -dw[i] : dw[i]);
            }
            Real diffusion(Size i) const override { return sigma_[i]; }
          private:
            Real step(Size i, Real x0, Real dw) const {
                // apply()'s body; the gate has made sure it is this class's.
                return x0 * std::exp(lognormalIncrement(stdDeviation_[i], dw, drift_[i]));
            }
            std::vector<Real> stdDeviation_, drift_, sigma_;
        };

    }

    ext::shared_ptr<const StochasticProcess1D::StepTable1D>
    GeneralizedBlackScholesProcess::stepTable1D(const TimeGrid& grid) const {
        // Every subclass outside the gate steps through its own evolve(); one whose step is
        // this class's overrides this method and returns exactStepTable() itself (E).
        if (!isPlainBlackScholes(*this))
            return StochasticProcess1D::stepTable1D(grid);
        return exactStepTable(grid);
    }

    bool GeneralizedBlackScholesProcess::usesExactStep() const {
        localVolatility(); // trigger update: sets isStrikeIndependent_
        return isStrikeIndependent_ && !forceDiscretization_;
    }

    ext::shared_ptr<const StochasticProcess1D::StepTable1D>
    GeneralizedBlackScholesProcess::exactStepTable(const TimeGrid& grid) const {
        if (!usesExactStep())
            return StochasticProcess1D::stepTable1D(grid);   // the Euler branch: B's fused step
        const Size steps = grid.size() - 1;
        std::vector<Real> stdDeviation, drift, sigma;
        stdDeviation.reserve(steps);
        drift.reserve(steps);
        sigma.reserve(steps + 1);
        for (Size i = 0; i < steps; ++i) {
            Time t0 = grid[i], dt = grid.dt(i);
            // The strike-independent branch of evolve(), call for call. t0 + dt stays the
            // literal sum: grid[i + 1] need not be the same double.
            Real var = variance(t0, x0(), dt);              // x0 is unused on this branch
            Rate r = riskFreeRate_->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true).rate();
            Rate q = dividendYield_->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true).rate();
            stdDeviation.push_back(std::sqrt(var));
            drift.push_back(lognormalDrift(r, q, dt, var));
            sigma.push_back(diffusion(t0, x0()));           // t-only on this branch, for 3.4
        }
        sigma.push_back(diffusion(grid.back(), x0()));
        return ext::make_shared<ExactStepTable>(std::move(stdDeviation), std::move(drift),
                                                std::move(sigma));
    }
```

And the exact branch of `evolve()` computes its drift and exponent through the shared helpers
instead of inline:

```cpp
            Real var = variance(t0, x0, dt);
            Rate r = riskFreeRate_->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true).rate();
            Rate q = dividendYield_->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true).rate();
            Real drift = lognormalDrift(r, q, dt, var);
            return apply(x0, lognormalIncrement(std::sqrt(var), dw, drift));
```

**The generator.** `ql/methods/montecarlo/pathgenerator.hpp`:

```cpp
    template <class GSG>
    class PathGenerator : public Observer {
      public:
        ...
        //! \name Observer interface
        //@{
        void update() override { table_.reset(); }
        //@}
      private:
        const sample_type& next(bool antithetic) const;
        bool brownianBridge_;
        GSG generator_;
        Size dimension_;
        TimeGrid timeGrid_;
        ext::shared_ptr<StochasticProcess1D> process_;
        mutable ext::shared_ptr<const StochasticProcess1D::StepTable1D> table_;
        mutable sample_type next_;
        mutable std::vector<Real> temp_;
        BrownianBridge bb_;
    };

    // both constructors, after their QL_REQUIRE:
        registerWith(process_);

    template <class GSG>
    const typename PathGenerator<GSG>::sample_type&
    PathGenerator<GSG>::next(bool antithetic) const {
        // the sequence and the Brownian bridge, unchanged
        ...
        if (!table_)
            table_ = process_->stepTable1D(timeGrid_);

        Path& path = next_.value;
        table_->evolvePath(process_->x0(), increments, antithetic, &path.front());

        return next_;
    }
```

What to notice:

- The table is built on the first `next()`, not in the constructor. `McSimulation` builds the
  generator inside `calculate()`, when every handle is linked; a direct user who constructs one on
  an empty handle fails on the first `next()`, as today.
- `update()` drops the table and nothing else; the next `next()` rebuilds it. An engine never
  triggers that within one `calculate()`, so it pays one table per generator.
- The antithetic path is unchanged: `evolvePath` negates the increment, as `next()` does today.
- The generator calls the table once per path, not once per step. A virtual call per step costs
  about 18 Ir a step in the call and in the member reloads it forces, which leaves the flat
  discrete repricing 11% above a step inlined into the loop, measured; `ExactStepTable`'s own
  `evolvePath` closes that gap. A table that overrides only `evolve(i, x0, dw)` keeps the default
  loop, and `ForwardingStepTable` overrides it to call the process directly, as `next()` does
  today.
- `ExactStepTable::evolve` inlines `apply()`'s body, `x0 · exp(dx)`; `lognormalDrift` is the drift
  `evolve()` computes, `(r − q) · dt − 0.5 · var`, and `lognormalIncrement` its exponent,
  `√var · dw + drift`, with `√var` stored once. Every expression has the shape it has today, so
  the four bitwise pins of section 2 hold by construction.
- The gate is checked once per grid, in `stepTable1D()`, the branch in `exactStepTable()` through
  `usesExactStep()`. A subclass that overrides `variance()` or `apply()` lands on the forwarding
  table and on its own `evolve()`; one that leaves the step intact overrides `stepTable1D()` and
  returns `exactStepTable(grid)` (E). `<typeinfo>` is a C++98 header, and RTTI is already
  required (section 4).
- `usesExactStep()` is the condition `evolve()`, `expectation()`, `stdDeviation()` and
  `variance()` each spell out today after their own `localVolatility()` call; the four can call it
  instead. It is the one name the Black-Scholes process adds to the public interface. Step 1 of
  section 5 ships without it: there `exactStepTable()` reads `isStrikeIndependent_` and
  `forceDiscretization_` after `localVolatility()`, as `evolve()` does, and the inspector arrives
  with D, whose second builder and E's subclass read it.
- Step 1 of section 5 also ships the table without its σ column: no `sigma_`, no `diffusion(i)`
  override, the base class's `Null<Real>()`. D adds both for the pricers of 3.4.
- `MultiPathGenerator` changes the same way in step 5 of section 5, with
  `StochasticProcess::StepTable`, an `Array` it owns in place of the per-step result (3.7), and
  the same `Observer` base.

### B. 3.1: One Local-Volatility Evaluation per Euler Step

`ql/processes/blackscholesprocess.cpp`. Today the Euler branch of `evolve()` is one line that
hides two `diffusion()` calls, one inside `drift()` and one inside the discretization:

```cpp
    Real GeneralizedBlackScholesProcess::evolve(Time t0, Real x0,
                                                Time dt, Real dw) const {
        localVolatility(); // trigger update
        if (isStrikeIndependent_ && !forceDiscretization_) {
            // the exact branch, unchanged
            ...
        } else
            return apply(x0, discretization_->drift(*this, t0, x0, dt) +
                                 stdDeviation(t0, x0, dt) * dw);
    }
```

Written out, that branch is `apply(x0, drift(t0, x0) · dt + (diffusion(t0, x0) · √dt) · dw)`,
and `drift(t0, x0)` is `r − q − 0.5 · diffusion(t0, x0) · diffusion(t0, x0)`. The fused step
calls `diffusion()` once and keeps each of today's expressions as its own statement. The gate
that admits it is A's exact-type check plus one on the discretization, decided once per process
and kept in two flags next to the class's other `mutable` ones:

```cpp
    // ql/processes/blackscholesprocess.hpp, with updated_ and isStrikeIndependent_
        mutable bool stepGateDecided_ = false, ownsEulerStep_ = false;
```

```cpp
    namespace {

        // The continuous branch of InterestRate::impliedRate on the compound factor that
        // forwardRate(t1, t2, Continuous, NoFrequency, true) builds, without the InterestRate
        // and the DayCounter copy it carries. Same double: impliedRate's continuous case is
        // std::log(compound) / t, and its compound == 1.0 shortcut returns the 0.0 that
        // std::log(1.0) / t returns. Its two checks hold here: discount factors are positive
        // and t2 > t1.
        Real continuousForwardRate(const YieldTermStructure& curve, Time t1, Time t2) {
            return std::log(curve.discount(t1, true) / curve.discount(t2, true)) / (t2 - t1);
        }

    }

    Real GeneralizedBlackScholesProcess::evolve(Time t0, Real x0,
                                                Time dt, Real dw) const {
        localVolatility(); // trigger update
        if (isStrikeIndependent_ && !forceDiscretization_) {
            // the exact branch, unchanged
            ...
        }
        if (!stepGateDecided_) {
            // Once per process: the dynamic type is final after construction, and
            // discretization_ has no setter.
            ownsEulerStep_ = isPlainBlackScholes(*this)
                          && typeid(*discretization_) == typeid(EulerDiscretization);
            stepGateDecided_ = true;
        }
        if (ownsEulerStep_) {
            // The Euler step with one diffusion() call. Each statement is one of today's
            // expressions, in today's order of operations.
            Real sigma = diffusion(t0, x0);
            Time t1 = t0 + 0.0001;
            Real mu = continuousForwardRate(*riskFreeRate_, t0, t1)
                    - continuousForwardRate(*dividendYield_, t0, t1)
                    - 0.5 * sigma * sigma;                  // drift(t0, x0)
            Real driftDt = mu * dt;                         // EulerDiscretization::drift
            Real stdDev = sigma * std::sqrt(dt);            // EulerDiscretization::diffusion
            return apply(x0, driftDt + stdDev * dw);
        }
        return apply(x0, discretization_->drift(*this, t0, x0, dt) +
                             stdDeviation(t0, x0, dt) * dw);
    }
```

What to notice:

- `diffusion()` stays a virtual call, so whatever local volatility is linked, `LocalVolSurface`,
  `LocalVolCurve` or an external one, is evaluated as today, once.
- The gate is decided on the first `evolve()` and kept. Both of its inputs are fixed once
  construction has finished: the dynamic type, and `discretization_`, which `StochasticProcess1D`
  sets in its constructor and has no setter for, so `update()` leaves the flags alone. It cannot
  move to the constructor, where `typeid(*this)` names `GeneralizedBlackScholesProcess` whatever
  subclass is under construction. Per step the gate is one branch on a bool. A `typeid`
  comparison per step would be a pointer compare where the two `type_info` objects are merged and
  a `strcmp` of the mangled names where they are not, as on MSVC across a DLL boundary or under
  hidden visibility.
- `discretization_` is `StochasticProcess1D::discretization_`, and the check is for the exact
  type: `EndEulerDiscretization` evaluates at `t0 + dt`, and a subclass may override `drift()`
  or `diffusion()`.
- `drift()` is no longer called on this path, which is what the exact-type half of the gate is
  for: a subclass outside the five that overrides `drift()` reaches the last `return`, today's
  step.
- `continuousForwardRate` also serves `drift()`, `expectation()` and the exact branch, since each
  takes `.rate()` and drops the `InterestRate`; the sketch of A keeps the original calls there
  because they run once per grid.
- Under FMA contraction, one statement per expression keeps today's bits on a compiler that
  contracts within a statement only; GCC contracts across statements by default (3.1), which is
  why the change ships with the step test at the relative 1e-13 of section 1 rather than a
  bitwise lock.

### C. 3.3: The Local-Volatility Time Slice

Three files change under `ql/termstructures/volatility/equityfx/`: `localvoltermstructure.hpp`
and `.cpp` for the interface, `localvolsurface.hpp` and `.cpp` for the one term structure that
fills it, and `noexceptlocalvolsurface.hpp`, its one subclass. Today `LocalVolSurface::localVolImpl`
reads, for every `(t, strike)`, two discounts at `t`, the forward, and in its time derivative two
or four discounts at `t ± dt`, none of which depends on the strike (3.3).

**The interface.** `localvoltermstructure.hpp`:

```cpp
    class LocalVolTermStructure : public VolatilityTermStructure {
      public:
        ...
        //! \name Local Volatility
        //@{
        Volatility localVol(const Date& d, Real underlyingLevel,
                            bool extrapolate = false) const;
        Volatility localVol(Time t, Real underlyingLevel,
                            bool extrapolate = false) const;
        //! the terms of the local volatility at a time that do not depend on the level
        /*! A caller that evaluates one time at many levels builds a slice
            once and passes it to localVol(const Slice&, Real, bool). A slice
            holds what its term structure read when it was built, so the
            caller drops it when that term structure notifies; the term
            structure keeps no slice state. The base class holds the time
            only: a term structure that overrides neither sliceImpl() nor
            sliceLocalVolImpl() evaluates a slice exactly as it evaluates
            its time.
        */
        class Slice {
          public:
            Slice(const LocalVolTermStructure& termStructure, Time t)
            : termStructure_(&termStructure), t_(t) {}
            virtual ~Slice() = default;
            Time time() const { return t_; }
            const LocalVolTermStructure& termStructure() const { return *termStructure_; }
          private:
            const LocalVolTermStructure* termStructure_;
            Time t_;
        };
        ext::shared_ptr<const Slice> slice(Time t, bool extrapolate = false) const;
        Volatility localVol(const Slice& slice, Real underlyingLevel,
                            bool extrapolate = false) const;
        //@}
        ...
      protected:
        ...
        //! local vol calculation
        virtual Volatility localVolImpl(Time t, Real strike) const = 0;
        //! the slice for a time; the default holds the time only
        virtual ext::shared_ptr<const Slice> sliceImpl(Time t) const;
        //! local vol calculation from a slice; the default calls localVolImpl()
        virtual Volatility sliceLocalVolImpl(const Slice& slice, Real strike) const;
        //@}
    };
```

Its defaults, `localvoltermstructure.cpp`, beside the two `localVol` overloads that exist today:

```cpp
    ext::shared_ptr<const LocalVolTermStructure::Slice>
    LocalVolTermStructure::slice(Time t, bool extrapolate) const {
        checkRange(t, extrapolate);
        return sliceImpl(t);
    }

    Volatility LocalVolTermStructure::localVol(const Slice& slice,
                                               Real underlyingLevel,
                                               bool extrapolate) const {
        QL_REQUIRE(&slice.termStructure() == this,
                   "slice built by another local-volatility term structure");
        checkStrike(underlyingLevel, extrapolate);
        return sliceLocalVolImpl(slice, underlyingLevel);
    }

    ext::shared_ptr<const LocalVolTermStructure::Slice>
    LocalVolTermStructure::sliceImpl(Time t) const {
        return ext::make_shared<Slice>(*this, t);
    }

    Volatility LocalVolTermStructure::sliceLocalVolImpl(const Slice& slice,
                                                        Real strike) const {
        return localVolImpl(slice.time(), strike);
    }
```

**The surface.** `localvolsurface.hpp`:

```cpp
    class LocalVolSurface : public LocalVolTermStructure {
      public:
        ...
      protected:
        Volatility localVolImpl(Time, Real) const override;
        ext::shared_ptr<const Slice> sliceImpl(Time t) const override;
        Volatility sliceLocalVolImpl(const Slice&, Real) const override;
      private:
        class TimeSlice;
        Volatility localVolOnSlice(const TimeSlice&, Real strike) const;
        Handle<BlackVolTermStructure> blackTS_;
        Handle<YieldTermStructure> riskFreeTS_, dividendTS_;
        Handle<Quote> underlying_;
    };
```

`localvolsurface.cpp`. The slice holds the discounts and the forward as the doubles today's
statements produce, and the two products today's formula divides by, never a ratio:

```cpp
    class LocalVolSurface::TimeSlice : public LocalVolTermStructure::Slice {
      public:
        TimeSlice(const LocalVolSurface& surface, Time t)
        : Slice(surface, t) {
            dr = surface.riskFreeTS_->discount(t, true);
            DiscountFactor dq = surface.dividendTS_->discount(t, true);
            forwardValue = surface.underlying_->value()*dq/dr;
            oneSided = (t == 0.0);
            dt = oneSided ? 0.0001 : std::min<Time>(0.0001, t/2.0);
            tpt = t+dt;
            DiscountFactor drpt = surface.riskFreeTS_->discount(tpt, true);
            dqpt = surface.dividendTS_->discount(tpt, true);
            drptDq = drpt*dq;
            if (!oneSided) {
                tmt = t-dt;
                DiscountFactor drmt = surface.riskFreeTS_->discount(tmt, true);
                dqmt = surface.dividendTS_->discount(tmt, true);
                drmtDq = drmt*dq;
            }
        }
        DiscountFactor dr, dqpt, drptDq;
        DiscountFactor dqmt = Null<Real>(), drmtDq = Null<Real>();
        Real forwardValue;
        Time dt, tpt, tmt = Null<Real>();
        bool oneSided;
    };

    ext::shared_ptr<const LocalVolTermStructure::Slice>
    LocalVolSurface::sliceImpl(Time t) const {
        return ext::make_shared<TimeSlice>(*this, t);
    }

    Volatility LocalVolSurface::localVolImpl(Time t, Real underlyingLevel) const {
        // today's entry point: the same six discount() calls, through the same body
        return localVolOnSlice(TimeSlice(*this, t), underlyingLevel);
    }

    Volatility LocalVolSurface::sliceLocalVolImpl(const Slice& slice,
                                                  Real underlyingLevel) const {
        // localVol(const Slice&, ...) has checked that this surface built the slice
        return localVolOnSlice(static_cast<const TimeSlice&>(slice), underlyingLevel);
    }

    Volatility LocalVolSurface::localVolOnSlice(const TimeSlice& s,
                                                Real underlyingLevel) const {
        // strike derivatives: today's statements, the forward from the slice
        Real strike = underlyingLevel;
        Real y = std::log(strike/s.forwardValue);
        Real dy = ((std::fabs(y) > 0.001) ? Real(y*0.0001) : 0.000001);
        Real strikep = strike*std::exp(dy);
        Real strikem = strike/std::exp(dy);
        Real w  = blackTS_->blackVariance(s.time(), strike,  true);
        Real wp = blackTS_->blackVariance(s.time(), strikep, true);
        Real wm = blackTS_->blackVariance(s.time(), strikem, true);
        Real dwdy = (wp-wm)/(2.0*dy);
        Real d2wdy2 = (wp-2.0*w+wm)/(dy*dy);

        // time derivative: today's two branches, the discounts from the slice.
        // strike*s.dr*s.dqpt/s.drptDq parses as today's ((strike*dr)*dqpt)/(drpt*dq).
        Real dwdt;
        if (s.oneSided) {
            Real strikept = strike*s.dr*s.dqpt/s.drptDq;
            Real wpt = blackTS_->blackVariance(s.tpt, strikept, true);
            QL_ENSURE(wpt>=w,
                      "decreasing variance at strike " << strike
                      << " between time " << s.time() << " and time " << s.tpt);
            dwdt = (wpt-w)/s.dt;
        } else {
            Real strikept = strike*s.dr*s.dqpt/s.drptDq;
            Real strikemt = strike*s.dr*s.dqmt/s.drmtDq;
            Real wpt = blackTS_->blackVariance(s.tpt, strikept, true);
            Real wmt = blackTS_->blackVariance(s.tmt, strikemt, true);
            QL_ENSURE(wpt>=w, ...);
            QL_ENSURE(w>=wmt, ...);
            dwdt = (wpt-wmt)/(2.0*s.dt);
        }

        // the local variance from w, dwdy, d2wdy2 and dwdt: unchanged
        ...
    }
```

**The subclass.** `noexceptlocalvolsurface.hpp` wraps the slice path the way it wraps the time
path today, or a slice would throw where the time returns the overwrite:

```cpp
      protected:
        Volatility localVolImpl(Time t, Real s) const override { ... unchanged ... }
        Volatility sliceLocalVolImpl(const Slice& slice, Real s) const override {
            try {
                return LocalVolSurface::sliceLocalVolImpl(slice, s);
            } catch (Error&) {
                return illegalLocalVolOverwrite_;
            }
        }
```

**The test.** `test-suite/europeanoption.cpp`, beside `testLocalVolatility`, which already builds
a `BlackVarianceSurface` and a `BlackScholesMertonProcess` on it, though nothing there calls
`localVolatility()` yet, which would link a `LocalVolSurface`: on a grid of times from zero and
levels around the forward,
`localVol(*slice(t, true), x, true)` equals `localVol(t, x, true)` to the bit, and a slice handed
to a second surface throws. The two entry points share one body and one slice type, so the
equality holds by construction; the test guards a later edit that lets them drift, and the zero
time covers the one-sided branch.

What to notice:

- The five other local volatilities, `LocalConstantVol`, `LocalVolCurve`, `FixedLocalVolSurface`,
  `GridModelLocalVolSurface` and `AndreasenHugeLocalVolAdapter`, change nothing: their slice is
  the time, and `localVol(slice, x)` is `localVol(t, x)` through the default. The slice stays
  optional for a local volatility outside QuantLib the same way.
- The two virtuals have their own names rather than overloading `localVolImpl()`: an overload
  would be hidden in every subclass that overrides `localVolImpl(Time, Real)`, which is all of
  them, and GCC's `-Woverloaded-virtual` reports the hiding.
- `localVolImpl(Time, Real)` builds a `TimeSlice` on the stack and goes through the same body, so
  today's path makes the same six `discount()` calls and the same statements, bit-identical;
  that is the "goes through the slice code" of 3.3. No expression the slice holds contains an
  addition next to a product, so FMA contraction cannot touch it either; `tpt` is the literal
  `t+dt`.
- `slice()` checks the time once; `localVol(const Slice&, ...)` checks the strike per lookup and,
  by one pointer compare, that this term structure built the slice, which is what makes the
  `static_cast` in `LocalVolSurface` safe. Per lookup the slice path drops today's `checkRange(t)`
  as well.
- The slice is a snapshot: a `Quote`'s value and six discounts. The surface registers with the
  Black surface and both curves, and with the quote when built on a `Handle<Quote>`; the one the
  process builds takes the spot by value, and the process itself observes the quote, so either
  way a change notifies the process and the generator drops its table (E). The slice refers to
  its term structure by pointer; the table that holds
  the slice is held by the generator, which holds the process, which holds the handle.
- Per lookup it removes the six `discount()` calls and the forward, 3,168 to 1,670 Ir on the
  bilinear surface and 25,265 to 23,768 on the bicubic one after 3.1, measured by the prototype
  of 3.3. Per grid time it costs one allocation and those six calls.
- Nothing in QuantLib calls it yet. `GeneralizedBlackScholesProcess::diffusion()` keeps calling
  `localVol(t, x, true)`, which is what B's fused step and A's forwarding table reach. QuantLib's
  own engines take the slice when the Euler table of D holds one per grid time next to the rate
  pair, behind A's gate, since it bypasses the virtual `diffusion()`. Until then a cache outside
  QuantLib builds the slices itself, which is why the slice lands before D rather than with it.
- The surface slice of 3.3, unmeasured and not sketched, lands one level down: a slice on
  `BlackVolTermStructure` that `TimeSlice` would hold at `t`, `tpt` and `tmt`, and the five
  `blackVariance()` calls per lookup would take.

### D. 3.3 and 3.4: The Euler Table

Step 4 of section 5 puts the Euler branch on A's hook: a table that holds, per grid step, the
rate pair of 3.3 and `√dt`, per grid time one slice of C, and, where σ depends on the time alone,
the σ of 3.4 at every grid point. Its step is B's fused step with the slice in place of the
virtual `diffusion()`. One file changes: `GeneralizedBlackScholesProcess` gets a second protected
builder, and its `stepTable1D()` picks a builder by branch. `StochasticProcess1D`, the generators
and the local volatilities stay as A and C left them.

**The override.** `ql/processes/blackscholesprocess.hpp`, next to `exactStepTable()`:

```cpp
      protected:
        ext::shared_ptr<const StepTable1D> exactStepTable(const TimeGrid& grid) const;
        /*! the fused Euler step per grid step on the Euler branch under
            EulerDiscretization, with the rate pair and the local volatility's
            slice held per grid time; the forwarding table on the
            strike-independent branch and under any other discretization. For
            a subclass whose overrides leave the step intact and which
            overrides stepTable1D() to say so.
        */
        ext::shared_ptr<const StepTable1D> eulerStepTable(const TimeGrid& grid) const;
        ...
```

`ql/processes/blackscholesprocess.cpp`, next to `ExactStepTable`:

```cpp
    namespace {

        class EulerStepTable : public StochasticProcess1D::StepTable1D {
          public:
            EulerStepTable(ext::shared_ptr<const LocalVolTermStructure> localVol,
                           std::vector<ext::shared_ptr<const LocalVolTermStructure::Slice>> slice,
                           std::vector<Rate> rateDrift,
                           std::vector<Time> dt,
                           std::vector<Real> sqrtDt,
                           std::vector<Real> sigma)
            : localVol_(std::move(localVol)), slice_(std::move(slice)),
              rateDrift_(std::move(rateDrift)), dt_(std::move(dt)),
              sqrtDt_(std::move(sqrtDt)), sigma_(std::move(sigma)) {}
            Real evolve(Size i, Real x0, Real dw) const override {
                // B's fused step, statement for statement: the slice in place of the virtual
                // diffusion(), the rate pair in place of its two forward rates, apply()'s
                // body in place of apply(). The gate has made sure the step is this class's.
                Real sigma = localVol_->localVol(*slice_[i], x0, true);
                Real mu = rateDrift_[i] - 0.5 * sigma * sigma;  // drift(t0, x0)
                Real driftDt = mu * dt_[i];                      // EulerDiscretization::drift
                Real stdDev = sigma * sqrtDt_[i];                // EulerDiscretization::diffusion
                return x0 * std::exp(lognormalIncrement(stdDev, dw, driftDt));
            }
            Real diffusion(Size i) const override {
                return sigma_.empty() ? Null<Real>() : sigma_[i];
            }
          private:
            ext::shared_ptr<const LocalVolTermStructure> localVol_;
            std::vector<ext::shared_ptr<const LocalVolTermStructure::Slice>> slice_;
            std::vector<Rate> rateDrift_;
            std::vector<Time> dt_;
            std::vector<Real> sqrtDt_, sigma_;
        };

    }

    ext::shared_ptr<const StochasticProcess1D::StepTable1D>
    GeneralizedBlackScholesProcess::stepTable1D(const TimeGrid& grid) const {
        // A's gate, then the branch; each builder forwards off its own branch.
        if (!isPlainBlackScholes(*this))
            return StochasticProcess1D::stepTable1D(grid);
        return usesExactStep() ? exactStepTable(grid) : eulerStepTable(grid);
    }

    ext::shared_ptr<const StochasticProcess1D::StepTable1D>
    GeneralizedBlackScholesProcess::eulerStepTable(const TimeGrid& grid) const {
        // The exact branch, or a discretization with a step of its own: B's check, once per
        // grid instead of once per process.
        if (usesExactStep() || typeid(*discretization_) != typeid(EulerDiscretization))
            return StochasticProcess1D::stepTable1D(grid);
        // The link evolve() would evaluate: the LocalVolSurface localVolatility() built under
        // a smile, the LocalConstantVol or LocalVolCurve under a forced discretization, or the
        // external one. The table holds the link, not the handle.
        ext::shared_ptr<const LocalVolTermStructure> localVol = localVolatility().currentLink();
        const Size steps = grid.size() - 1;
        std::vector<ext::shared_ptr<const LocalVolTermStructure::Slice>> slice;
        std::vector<Rate> rateDrift;
        std::vector<Time> dt;
        std::vector<Real> sqrtDt, sigma;
        slice.reserve(steps);
        rateDrift.reserve(steps);
        dt.reserve(steps);
        sqrtDt.reserve(steps);
        for (Size i = 0; i < steps; ++i) {
            Time t0 = grid[i];
            // drift()'s rate pair at the point B's fused step evaluates it; t0 + 0.0001 stays
            // the literal sum, and r - q is one double because drift() subtracts left to right.
            Time t1 = t0 + 0.0001;
            rateDrift.push_back(continuousForwardRate(*riskFreeRate_, t0, t1)
                              - continuousForwardRate(*dividendYield_, t0, t1));
            dt.push_back(grid.dt(i));                       // what PathGenerator passes today
            sqrtDt.push_back(std::sqrt(grid.dt(i)));
            slice.push_back(localVol->slice(t0, true));     // C; the time only below a surface
        }
        if (isStrikeIndependent_) {
            // A forced discretization on a constant or curve volatility: sigma depends on t
            // alone, and the table holds it at every grid point for the pricers of 3.4.
            sigma.reserve(steps + 1);
            for (Size i = 0; i <= steps; ++i)
                sigma.push_back(diffusion(grid[i], x0()));
        }
        return ext::make_shared<EulerStepTable>(std::move(localVol), std::move(slice),
                                                std::move(rateDrift), std::move(dt),
                                                std::move(sqrtDt), std::move(sigma));
    }
```

And B's fused step ends in A's helper instead of the inline sum, the way the exact branch
does:

```cpp
            return apply(x0, lognormalIncrement(stdDev, dw, driftDt));
```

**The test.** Beside B's step test: on the smile setup of section 1 and on a flat setup with
`forceDiscretization`, `stepTable1D(grid)->evolve(i, x, dw)` against
`evolve(grid[i], x, grid.dt(i), dw)` at fixed points on every step, within the relative 1e-13 of
section 1, and bit for bit on a build that does not contract. `diffusion(i)` is `Null<Real>()`
under the smile and `diffusion(grid[i], x0())` under the forced discretization. The NPVs of
section 7's second item serve as the statistical check, and its third item is the measurement
this table makes possible: the smile setup through QuantLib's own generator with one
`localVolImpl` and cached rates.

What to notice:

- Per step the table makes one `localVol()` call on a slice, five `blackVariance()` calls
  under a surface, and one `exp`; B's fused step makes the same `localVol()` through the time,
  six `discount()` calls more, and the rate pair's four `discount()` and two `log` on top. The
  prototype of 3.3 puts that at 1,670 Ir bilinear and 23,768 bicubic against 3,168 and 25,265
  for the fused step with the rate pair cached, and the fused step alone lands near 4,600 and
  26,700 (3.1, derived). Per grid time the table pays one slice allocation, six `discount()`
  calls, four more and two `log` for the rate pair, and one `sqrt`.
- The step is B's statement for statement, so what holds for B holds here: bit-identical where
  nothing is contracted, within the 1e-13 of section 1 where GCC contracts across statements.
  `lognormalIncrement(stdDev, dw, driftDt)` is `stdDev * dw + driftDt`, the sum of today's
  `drift + stdDev * dw` with its operands swapped, which IEEE addition does not notice, and
  the one product a contracting compiler fuses either way. The slice path and the time path of
  `LocalVolSurface` share one body (C), so σ is the same double on both.
- The branch is decided once per grid, through `usesExactStep()` and B's `typeid` comparison
  on `discretization_`, and both builders forward off their branch, so neither can be made to
  return a table for a step that is not this class's. `stepTable1D()` picks the builder behind
  A's gate; a subclass picks it in its own override (E).
- The table holds the local volatility's link and its slices, which refer to that term
  structure by pointer, so the link outlives them. After a notification `localVolatility()`
  links a fresh `LocalVolSurface`, since `update()` clears `updated_`; the old one lives on in
  the table until the generator drops it, which it does on the same notification. A quote,
  curve or surface change reaches the process through the handles it registers with, and an
  external local volatility notifies the process as its `Observable`, so the table is a
  snapshot only between two notifications, as A's.
- `diffusion(i)` is filled only where σ depends on the time alone, a forced discretization on a
  `BlackConstantVol` or `BlackVarianceCurve`, which is the one case the Euler branch shares
  with 3.4's strike-independent finding. Under a smile it stays `Null<Real>()`: the σ the
  barrier pricer wants there is the one the generator computed at `(t_i, path[i])`, which the
  table would have to record per step in `evolve()`, mutable state that makes it a per-path
  object rather than a per-grid one. That side channel and the two pricer changes of 3.4 are not
  sketched.
- `exactStepTable()` is unchanged; `eulerStepTable()` sits beside it rather than inside it, so a
  subclass can opt into one branch's table without the other (E).

### E. What a Caller Sees

An engine user changes nothing. `MakeMCEuropeanEngine<PseudoRandom>(process)` with 252 steps on a
`BlackScholesMertonProcess` under a `BlackConstantVol` runs as follows: `McSimulation::calculate`
builds the `MonteCarloModel` and with it the `PathGenerator`, which registers with the process;
the first `next()` calls `stepTable1D(timeGrid_)`; the gate passes, `localVolatility()` links a
`LocalConstantVol` and sets the branch flag, and `exactStepTable()` builds 252 triples through the
same `variance()` and `forwardRate()` calls `evolve()` made per step. Every further path is 252
lookups and `exp` calls. Under a `BlackVarianceSurface` the same engine gets D's Euler table:
`usesExactStep()` is false, and `eulerStepTable()` builds 252 rate pairs through B's helper and
252 slices through `LocalVolSurface::sliceImpl`, six `discount()` calls each. Every further path
step is one `localVol()` on a slice, five `blackVariance()` calls, and one `exp`. B's fused step
still serves a direct `evolve()` caller and every process outside the gate. A direct
`PathGenerator` user, `test-suite/pathgenerator.cpp` or SWIG's
`GaussianPathGenerator`, sees the same; a quote relinked between two `next()` calls notifies the
generator, which drops the table and rebuilds it on the next path.

A subclass author outside QuantLib makes the one new call. The gate is on the exact type, so every
subclass starts on today's step, whether or not its overrides touch it; opting into the table is
one override:

```cpp
    // Overrides drift(), which the fused step of B and the tables of A and D all bypass. The
    // gate sees a type outside the five: stepTable1D() returns the forwarding table, every step
    // goes through this class's evolve(), which is today's, and its Euler branch calls this
    // drift().
    class ShiftedDriftProcess : public BlackScholesMertonProcess {
      public:
        using BlackScholesMertonProcess::BlackScholesMertonProcess;
        Real drift(Time t, Real x) const override {
            return BlackScholesMertonProcess::drift(t, x) + shift_;
        }
      private:
        Real shift_ = 0.0;
    };

    // Overrides time() only, which is not on the step. The gate still refuses the exact type,
    // so the class says itself that its step is the base class's on both branches: each
    // protected builder applies usesExactStep() and the discretization check itself, and
    // returns the forwarding table off its branch. Its own subclasses get the forwarding
    // table, since they might override part of the step.
    class VolDayCountProcess : public BlackScholesMertonProcess {
      public:
        using BlackScholesMertonProcess::BlackScholesMertonProcess;
        Time time(const Date& d) const override {
            return blackVolatility()->dayCounter().yearFraction(
                blackVolatility()->referenceDate(), d);
        }
        ext::shared_ptr<const StepTable1D> stepTable1D(const TimeGrid& grid) const override {
            if (typeid(*this) != typeid(VolDayCountProcess))
                return StochasticProcess1D::stepTable1D(grid);
            return usesExactStep() ? exactStepTable(grid) : eulerStepTable(grid);
        }
    };
```

What to notice:

- The first class needs no code and loses nothing; the second needs one override and gains
  both tables. Neither can get a table that ignores an override, which is the contract section 4
  asks the gate to keep. The second repeats the gate for its own type, because its override is
  inherited by every class below it.
- `exactStepTable()` applies `usesExactStep()` itself and falls back to the forwarding table on
  the Euler branch; `eulerStepTable()` does the same on the exact branch and under a
  discretization that is not exactly `EulerDiscretization`. So the override cannot pick a table
  on the wrong branch. A subclass that wants to know the branch asks
  `usesExactStep()`, the question a cache outside QuantLib had to answer by probing (section 1).
- The two builders are separate so that a subclass can opt into one branch alone.
  `ShiftedDriftProcess` overrides `drift()`, which only the Euler step runs through, so it may
  return `exactStepTable(grid)` from an override of its own: the exact table on the
  strike-independent branch, and on the Euler branch the forwarding table, which runs its
  `evolve()` and so its `drift()`.
- `ShiftedDriftProcess` under a smile pays today's two `diffusion()` calls per step, since B's
  fused step is behind the same gate. That is the price of the contract, and it is the price
  such a subclass pays today.
