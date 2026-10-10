<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Precomputing Monte Carlo Step Terms Inside QuantLib

Which per-step computations of QuantLib's Monte Carlo path generation depend only on the time
grid, and how this repository's step caches would be built inside QuantLib rather than beside it.
Nothing here is implemented in QuantLib. Every QuantLib claim refers to the submodule at
`966a4cc10`; every figure not quoted from
[`profiling-bonusclassicoption-mc.md`](profiling-bonusclassicoption-mc.md) is unmeasured.

## 1. What Carries Over

`CachedStepPathGenerator` in `src/rke/ql/ext/methods/montecarlo/` takes each step from a cache
built once per `TimeGrid`, one per branch of `GeneralizedBlackScholesProcess::evolve`:

- **`BlackScholesStepCache`**, the exact step under a strike-independent volatility. On the flat
  BonusClassicOption workload one discrete repricing on the shared build drops from 34.18 G to
  2.68 G Ir, every NPV bit-identical (profiling doc, 4.1). The workload (profiling doc, 1):
  `BlackScholesMertonProcess`, spot 100, `FlatForward` 1% risk-free and 3% dividend yield,
  continuously compounded, `BlackConstantVol` 20%, `Actual360`, `NullCalendar`, 255 steps, 2^16
  Sobol paths with a Brownian bridge.
- **`LocalVolStepCache`**, the Euler step under a smile. On the smile markets one discrete
  repricing drops 58.6% on a bilinear and 51.3% on a bicubic `BlackVarianceSurface`. The NPVs are
  bit-identical where the compiler does not contract to FMAs, and within 1.9e-6 relative where it
  does (profiling doc, 4.5). The markets (profiling doc, 8.1): the same option, grid and paths;
  `ZeroCurve`s linear in the continuously compounded zero rate on today to 2Y; the surface
  σ(K) = 0.20 − 0.08 · ln(K / 100) on every expiry, bilinear or bicubic in time and strike;
  `Actual360`, `NullCalendar`, spot 100.

Three principles produce those results, and each holds inside QuantLib:

1. A term that depends only on `(t0, dt)` and the market is computed once per grid point, not once
   per path step.
2. It is computed through the same calls, in the same order and expression shape, so every path
   point is the same double and no regression lock moves. Only a whole left-associative
   subexpression can be cached; `t0 + dt` stays the literal sum, never `grid[i + 1]`.
3. A process whose step the cache cannot reproduce keeps its own `evolve()`. Nothing is rerouted
   silently.

Inside QuantLib two constraints change:

- **The branch is known.** `BlackScholesStepCache` infers it from the volatility type,
  `LocalVolStepCache` takes whatever `localVolatility()` links, and both probe `evolve()` at two
  points on every grid step on construction, because `isStrikeIndependent_`,
  `forceDiscretization_` and `hasExternalLocalVol_` are private to
  `GeneralizedBlackScholesProcess`. The process itself reads them.
- **The step can be shared.** Under FMA contraction the extension's one-expression Euler step
  cannot match `evolve()`, whose arithmetic is split across virtual calls, so `LocalVolStepCache`
  accepts its step within a relative 1e-13, 250 times the 4e-16 one step moves under contraction
  (profiling doc, 4.5). Inside QuantLib, `evolve()` and the cache can call the same step helper
  (section 4).

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
  calibration paths inner. `test-suite/pathgenerator.cpp` and `test-suite/brownianbridge.cpp`
  drive `PathGenerator` without an engine, `Examples/DiscreteHedging` a `MonteCarloModel`, all
  over a Black-Scholes process; the remaining direct `MultiPathGenerator` users in the test suite
  step other processes. QuantLib-SWIG wraps `PathGenerator` as `GaussianPathGenerator` in
  `SWIG/montecarlo.i`, read at `dc84dd9f`, outside this document's pin.
- **Precedents in QuantLib:** `LogNormalFwdRatePc` precomputes `fixedDrifts_` and `calculators_`
  per step in its constructor; `TrinomialTree` caches the step variance in `v2Cache`;
  `BrownianBridge` its weights per grid; `LongstaffSchwartzPathPricer` and `MCBarrierEngine` their
  discounts. Two memoize instead: `GsrProcessCore` in `std::map`s keyed on the exact
  `(t0, t0 + dt)`, its `y` on `t` alone, cleared only by an explicit `flushCache()`, since
  `GsrProcess` registers with nothing; `JointStochasticProcess` keys its correlation on `(t0, dt)`
  and clears it in `update()`.
- **Tests:** two sets pin `evolve()` tightly. `pathgenerator.cpp`'s `testPathGenerator` at 2e-8
  (2e-7 antithetic) and `testMultiPathGenerator` at 2e-7; `testCached` of Himalaya, Everest and
  Pagoda at 1e-8. All five run on `flatVol`, a `BlackConstantVol` on `Actual360`, and
  `testPathGenerator` pins `GeometricBrownianMotionProcess`, `OrnsteinUhlenbeckProcess` and
  `SquareRootProcess` the same way. Every other MC test compares within a tolerance orders of
  magnitude above a last-bit change, against its error estimate or a stored value. Every MC test
  over `GeneralizedBlackScholesProcess` uses `BlackConstantVol`, except `testMCVarianceSwap` on a
  `BlackVarianceCurve`; none forces the discretization or links an external local volatility, so
  **no QuantLib MC test reaches the Euler branch**, and none pins the exact step on a
  `BlackVarianceCurve`. Nor can a path's terminal value lock it across platforms: `localVolImpl`
  divides by `dy²`, so a last bit that contracts differently moves single bilinear paths by up to
  3% (profiling doc, 4.5).

## 3. Candidates

3.1 comes first: a production market, with an interpolated curve and a smile, takes the Euler
branch (profiling doc, 3), and 3.1 needs no new API. 3.2 to 3.4 complete the Black-Scholes process
in the order they build on each other; 3.5 to 3.7 follow by expected gain. Measured outside
QuantLib: 3.1 together with 3.3's rate pair (profiling doc, 4.5), 3.2 (4.1), and 3.3's local-vol
time slice by prototype (9.1). The rest is unmeasured. "Bit-identical" means without FMA
contraction; section 4 keeps it so under contraction.

### 3.1 One Local-Volatility Evaluation per Euler Step

- **Status:** implemented outside QuantLib, together with 3.3's rate pair, as `LocalVolStepCache`
  (profiling doc, 4.5). Proposed inside QuantLib with no API change.
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
  - Companion: the forward rate without an `InterestRate` (profiling doc, 4.2 and 9.3), 124 of
    536 Ir per call, bit-identical. Until the hook of section 4 caches the rate pair, 3.1 leaves it
    per step, so the companion pays here.
- **Branches:** the Euler branch: a smile, `forceDiscretization`, or an external local volatility.
  It works whatever local volatility is linked.
- **Evidence:** on the smile markets of section 1, the two `localVol` calls take 5,951 of 7,609 Ir
  per step on the bilinear surface and 50,146 of 51,804 on the bicubic one; the rate pair 1,130
  (profiling doc, 8.2). `LocalVolStepCache` removes one call and the rate pair and lands at 3,168
  and 25,265. Inside QuantLib, 3.1 alone keeps the rate pair. If both `localVol` calls cost the
  same, it lands near 4,600 bilinear and 26,700 bicubic; that split is derived, not measured.
- **Lock impact:** bit-identical without contraction: `diffusion` is a deterministic function of
  `(t0, x0)`, so the reused value is the double the second call would return. The drift stays
  `((r − q) − (0.5·σ)·σ)·dt` and the diffusion term `(σ·√dt)·dw`.
  - Under contraction the old step's arithmetic is split across `drift()`,
    `EulerDiscretization::drift()` and `EulerDiscretization::diffusion()`, each behind a virtual
    call; the new one computes it in `evolve()` and calls only the virtual `diffusion()`. A
    compiler that contracts only within an expression keeps the old bits when the new code writes
    each old expression as its own statement. GCC contracts across statements by default:
    `-ffp-contract=fast` is its default for C++ in every language mode, and only
    `-ffp-contract=on` stays within one expression (GCC manual, `-ffp-contract`). This repository
    sets neither flag.
  - No QuantLib test would notice either way, so the change comes with one. It pins single steps
    from fixed points within the relative 1e-13 of section 1, as `LocalVolStepPathGeneratorTests`
    do, and a smile price within a statistical tolerance. A path's terminal value cannot serve
    (section 2).
- **Upstream:** high. Local to one function, no new interface. It keeps calling the virtual
  `diffusion()`, so `VegaStressedBlackScholesProcess`'s override still applies. It no longer calls
  `drift()`. QuantLib's only override of `drift()` is in `ExtendedBlackScholesMertonProcess`, which
  overrides `evolve()` as well. 3.1 changes `evolve()` only; `stdDeviation()`, `variance()` and
  `expectation()` stay, which is what keeps that subclass unaffected, since its Euler and
  predictor-corrector cases call `stdDeviation()`. A subclass outside QuantLib that overrides
  `drift()` alone would lose it silently, which QuantLib's deprecation rules forbid, so the fused
  step runs behind the exact-type gate of section 4 and such a subclass keeps today's path.

### 3.2 The Exact Black-Scholes Step per Grid

- **Status:** implemented outside QuantLib (profiling doc, 4.1); proposed inside it on the hook of
  section 4.
- **Lives in:** `GeneralizedBlackScholesProcess`, overriding the hook; `StochasticProcessArray`
  composing its components' tables.
- **Finding:** the strike-independent branch runs, per step, `localVolatility()` twice, `variance`
  with two `blackVariance` (each `checkRange` → `maxTime` without `extrapolate`), and two
  `forwardRate(t0, t0 + dt, Continuous, NoFrequency, true)`. None depends on `x0` or `dw`. Inside
  QuantLib the hook reads the branch flags, so the volatility type check and the probe drop out;
  the exact-type gate of section 4 stays.
- **Branches:** strike-independent only: `BlackConstantVol`, `BlackVarianceCurve`, no forced
  discretization, no external local volatility.
- **Evidence:** `evolve` costs 1,938 Ir per step on the flat workload, 1,072 of them in the two
  `forwardRate` and 644 in `variance` (profiling doc, 2). How much of the 92.15% saving an engine
  sees depends on its steps per path: none for the 1-step European and barrier tests, the most for
  `MCLookbackEngine` and `MCDoubleBarrierEngine`, tested at 2,000 and 5,000 steps.
- **Lock impact:** bit-identical by construction, and the extension's four values reproduce
  (profiling doc, 4.1). QuantLib pins it through `testPathGenerator` and, via
  `StochasticProcessArray`, `testMultiPathGenerator` and the three `testCached`, all on
  `BlackConstantVol` (section 2). The `BlackVarianceCurve` branch has no bitwise pin there, only
  the extension's `testVarianceCurveAndZeroCurves`, so the hook's test adds one.
- **Upstream:** high if the hook is accepted. This is the change that justifies the hook.

### 3.3 Time Slices and the Rate Pair of the Euler Step

- **Status:** the rate pair implemented outside QuantLib in `LocalVolStepCache` (profiling doc,
  4.5); the time slices proposed as the profiling doc's 9.1 and 9.2, 9.1 measured by prototype.
- **Lives in:** the rate pair in `GeneralizedBlackScholesProcess`'s hook, since it needs the grid.
  The slices in `LocalVolTermStructure` and `LocalVolSurface` (9.1) and `BlackVarianceSurface`
  (9.2); they need no hook.
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
  - 9.1 adds a virtual on `LocalVolTermStructure` that returns the terms for a time, with a
    default that holds the time only, and a `localVol` overload that takes them. 9.2 does the same
    one level down. A slice is a value the caller builds per grid time and drops when the surface
    notifies; it holds the forward `underlying_->value() · dq / dr`, a snapshot of an observed
    `Quote`, and the term structure keeps no slice state. The hook's table holds one slice per
    grid time; `LocalVolStepCache` does the same without the hook (profiling doc, 9).
- **Branches:** the Euler branch. The slice is optional for every other local volatility, which
  keeps the default.
- **Evidence:** the copied-`localVolImpl` prototype does 9.1's computation: 3,168 → 1,670 Ir per
  step on the bilinear surface, 25,265 → 23,768 on the bicubic one (profiling doc, 9.1). 9.2 is
  unmeasured; its ceiling is the surface's share after 4.5, 33% of the bilinear step and 92% of the
  bicubic one (9.2). The rate pair is 1,130 Ir per step on the smile curves (8.2).
- **Lock impact:** bit-identical. `LocalVolSurface`'s own `localVolImpl` goes through the slice
  code. The slice holds whole subexpressions of today's formula: `strike·dr·dqpt / (drpt·dq)`
  parses as `((strike·dr)·dqpt) / (drpt·dq)`, so it holds `dr`, `dqpt` and the product `drpt·dq`,
  never their ratio. The bicubic slice builds the same spline from the same section; the bilinear
  one stays exact only if it keeps the four corner values and the existing formula (9.2). 3.1's
  step test covers all of it.
- **Upstream:** medium. 9.1 adds API to a base class, optional for every other local volatility.

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
    a third `localVolImpl` per step, the same as the continuous pricer's in this repository, 26.3%
    of the cached bilinear continuous run (profiling doc, 4.5). A `Path` carries values only, so
    reusing the generator's σ needs a side channel. The hook's table can be it, recording each
    step's σ for the pricer. The digital pricer's point is no generator point, and it reads up to
    the last grid point, so the table holds σ at every grid point, one more than the steps.
- **Branches:** both; the smile part for the barrier pricer only, and the barrier engine only on
  its unbiased `BarrierPathPricer`, since `BiasedBarrierPathPricer` makes no `diffusion()` call.
- **Evidence:** unmeasured in QuantLib. The 26.3% belongs to this repository's pricer.
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
  - QE and QEM, `HestonProcess`'s default, also compute `exp(−κ·dt)`, `k1`–`k4`, `A` and the
    second summand of `s2` per step; QE its `k0` too. The first summand of `s2` starts from
    `x0[1]`, and QEM's `k0` is path-dependent, so neither caches. `BatesProcess` and
    `GJRGARCHProcess` default to `FullTruncation`; the engines take the scheme from the process.
  - The truncation and reflection schemes cache the rate pair, `√dt` and `√(1 − ρ²)`.
  - `GJRGARCHProcess::evolve` recomputes `CumulativeNormalDistribution()(lambda_)`, an `exp`, four
    `sqrt` and some forty flops from parameters fixed at construction, on every step.
  - `BatesProcess` runs `InverseCumulativePoisson(lambda_·dt)(p)` per step, summing Poisson terms
    (`exp`, `pow`, a factorial each) from zero until the partial sum exceeds `p`, which can be as
    high as `1 − QL_EPSILON`. A per-step table of the partial sums reproduces the loop exactly up
    to a fixed length, with the loop as the fallback past it; `−lambda_·m_·dt` caches as one
    double.
  - `HestonSLVProcess` caches the rate pair, `exp(−κ·dt)`, `κ·θ·dt`, `√(1 − ρ²)` and the second
    summand of `s2`, the QE terms; the leverage function `localVol(t0, x0[0])` stays per path.
    `HestonSLVMCModel` loops steps outer, so a per-step hoist there pays even without the hook.
- **Branches:** all schemes except non-central chi-square and Broadie-Kaya, where a root solve per
  step dominates (section 6).
- **Evidence:** unmeasured. What remains per QE step is one `exp` and three `sqrt`, or one `exp`,
  one `sqrt`, an `erf` and a `log`, QEM adding a `log` to either branch, so the relative gain
  stays below the Black-Scholes 92%.
- **Lock impact:** bit-identical when cached as listed. QuantLib's Heston-family MC tests, in
  `hestonmodel`, `batesmodel`, `gjrgarchmodel`, `hestonslvmodel`, `hybridhestonhullwhiteprocess`,
  `asianoptions`, `forwardoption` and `fdmlinearop`, are all statistical (`testMcVsCached` at
  2.34 σ, GJR-GARCH at 0.15 against stored MC values) and would not notice a last-bit change, so
  the change adds a path pin for one QEM and one full-truncation path.
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
    `stdDeviation(dt)·dw` depend on the path; the standard deviation
    `σ·√((1 − exp(−2a·dt)) / (2a))` is the cached `sqrt`.
  - The hybrid, in its `BSMHullWhite` discretization, computes about 10 `discount`, 5 `log` and 15
    `exp` per step from `(t0, dt)` alone, and evaluates `alpha(t0)` and the OU variance twice each.
  - Neither `HullWhiteProcess` nor the hybrid registers with anything, and the hybrid's `update()`
    notifies no one. A per-calculate table does not need them to (section 4).
  - Only `HullWhiteForwardProcess` has an engine: `MCHullWhiteCapFloorEngine` builds one in
    `pathGenerator()` on every `calculate()`, and the hybrid wraps one. `HullWhiteProcess` has the
    same expectation without `M_T` and gets the same table, driven only by `PathGenerator`
    directly.
- **Branches:** all; the hybrid's Euler discretization drops one variance.
- **Evidence:** unmeasured. `forwardRate(t, t)` takes two `discount` 1e-4 apart and an
  `impliedRate`, the work of the Black-Scholes rate pair, plus one `checkRange` without
  `extrapolate`, which copies the `DayCounter` (profiling doc, 4.4) and which the Black-Scholes
  pair skips by passing `true`.
- **Lock impact:** bit-identical with `alpha(t0 + dt)`, `alpha(t0)·exp(−a·dt)` and `M_T` as three
  separate doubles, since the OU term is leftmost and path-dependent. Inside `M_T`, `t − s` is
  `(t0 + dt) − t0`, not `dt`. The hybrid keeps `m4` and `m5` separate. `test-suite/gsr.cpp` pins
  `HullWhiteForwardProcess::expectation` and `variance` against `GsrProcess` at 1e-8, so a step
  helper that `expectation()` shares with the table keeps its arithmetic.
  `MCHullWhiteCapFloorEngine` has no test: the change adds one. The hybrid's tests are
  statistical.
- **Upstream:** medium. No QuantLib model reaches either process: `HullWhite::Dynamics`, and with
  it `TrinomialTree`, run on an `OrnsteinUhlenbeckProcess`, `Gaussian1dModel` on `GsrProcess` or
  `MfStateProcess`, and `FdHestonHullWhiteVanillaEngine` reads `a()` and `sigma()` only.

### 3.7 Allocation-Free Multi-Asset Steps

- **Status:** proposed. Fixes the multivariate hook's signature from the start.
- **Lives in:** `StochasticProcessArray::evolve`, `MultiPathGenerator::next`, and every
  multi-factor `evolve()` returning an `Array`.
- **Finding:** `StochasticProcessArray::evolve` allocates `dz = sqrtCorrelation_ * dw` and the
  result on every step, `MultiPathGenerator` move-assigns that result, and Heston, Bates and
  GJR-GARCH allocate `retVal`. The correlation product depends on `dw` and cannot be cached. This is
  not precomputation: it is what remains around the arithmetic once 3.2 and 3.5 remove the curve
  calls. The multivariate step writes into a caller-owned `Array` instead.
- **Branches:** the seven `StochasticProcessArray` engines and the Heston family.
- **Evidence:** unmeasured. DHAT found no per-step allocation on the flat one-dimensional workload;
  under a bicubic smile the surface allocates on every lookup, which 9.2 addresses (profiling doc,
  6). A basket needs its own run.
- **Lock impact:** bit-identical: same arithmetic, other storage. `testMultiPathGenerator` and the
  three `testCached` pin it.
- **Upstream:** medium. The `Array`-returning `evolve()` stays.

## 4. API Inside QuantLib

| Option                                                                                                   | For                                                                                                                                                                                                   | Against                                                                                                                                                                                                                                                                                  |
| -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A.** A hook on the process takes a `TimeGrid` and returns a step table; the generators step through it | Reaches both generators and every process through one mechanism; no shared mutable state; built per generator, rebuilt on notification; the process knows its own branch; adds and deprecates nothing | A new virtual in both core base classes and a public table type, each kept five releases after a future deprecation; the generators become observers; a subclass gets the table only when its exact type is known                                                                        |
| **B.** Each process memoizes its terms inside `evolve()`, keyed on `(t0, dt)`, cleared in `update()`     | No API change; every caller benefits, `HestonSLVMCModel` included                                                                                                                                     | Grows without bound across grids, as `GsrProcessCore` does; goes stale for processes that observe nothing (Hull-White, the hybrid, the SLV leverage); a lookup per step; with an AD type for `QL_REAL`, records a memoized value on whichever tape was active when it was first computed |
| **C.** Port `CachedStepPathGenerator` and its traits into QuantLib                                       | Changes no existing behaviour                                                                                                                                                                         | Every concrete engine fixes its traits, so no engine benefits without new template parameters                                                                                                                                                                                            |

**Recommendation: A,** under the deprecation rules of the submodule's `.agents/deprecation.md`: a
public API is never changed, only deprecated with its replacement in place, and a feature
deprecated in release N goes in N + 5, as `News.md` at the pin shows for 1.39 and 1.44. The hook
deprecates nothing, so the rules bind what it adds, which stays supportable for years, and what it
may change, which is nothing a caller sees today.

- **Shape:** `StochasticProcess1D` gains a virtual that takes a `TimeGrid` and returns a step
  table with `evolve(i, x0, dw)` and, for 3.4, each grid point's σ where a pricer can reuse it. The
  default forwards to `evolve(grid[i], x0, grid.dt(i), dw)`, so every process and subclass that
  does not override it behaves exactly as today. `StochasticProcess` gains the multivariate
  counterpart, writing in place (3.7); the `Array`-returning `evolve()` stays the public step,
  undeprecated, the in-place overload defaults to it, and `MultiPathGenerator` moves to the
  overload, as the checklist asks of internal callers. `ql/experimental/` is no home for any of
  it: a virtual on `StochasticProcess1D` cannot live there, and `ql/processes/` cannot depend on
  it. The public surface therefore stays at what the generators need, two virtuals and one table
  type; anything more, once shipped, is kept five releases after its deprecation. `News.md` lists
  the change as a performance improvement, not among the deprecations.
- **Lifetime:** the generator builds the table lazily, on the first `next()`, and rebuilds it when
  the process notifies: `PathGenerator` and `MultiPathGenerator` gain an `Observer` base and
  register with the process, an `Observable` through `StochasticProcess`. A direct user who moves
  the market between two `next()` calls sees the new market, as today, so no behaviour changes
  and nothing goes into `News.md` for it; the table is a snapshot only between notifications.
  `GeneralizedBlackScholesProcess`, `HestonProcess`, `GJRGARCHProcess` and
  `StochasticProcessArray` already register with their inputs; `HullWhiteProcess`,
  `HullWhiteForwardProcess` and the hybrid, whose `update()` recomputes `endDiscount_` and
  notifies no one, register with their handles and forward `update()` when they get their hook,
  which QuantLib's `AGENTS.md` 5.2 asks of any price input. Building the table in the constructor
  would make an empty handle that is linked later fail, for a process whose constructor registers
  without dereferencing, as `GeneralizedBlackScholesProcess` does; the Hull-White constructors
  already dereference the curve. Engines are unaffected, because they build a new generator per
  `calculate()`.
- **Everything cached lives in the table,** GJR-GARCH's constants included, never in a
  constructor. Design opinion, not decidable from the source: with an AD type for `QL_REAL`, a
  table built per `calculate()` is recorded on the pricing tape, a memoized value on whichever
  tape was active when it was first computed, which is held against option B.
- **Floating point:** the process's own `evolve()` calls the same inline step helper as its table,
  so both compile the same expression. That removes the cause of 4.5's differences: the
  extension's step is one expression, `evolve()`'s is split across virtual calls, and a compiler
  that contracts to FMAs fuses the two differently (profiling doc, 4.5). The `release` and
  `profile` presets target baseline x86-64 without FMA; a tree configured with `-march=x86-64-v3`
  shows the difference on this machine (profiling doc, 4.5 and 11), and this repository's
  `macos-26` and `ubuntu-26.04-arm` CI jobs contract by default but filter CTest on `^rke_`, so
  QuantLib's suite does not run there. A bitwise step test proves it on such a tree, on a
  `ctest -R quantlib` step of those jobs, or on QuantLib's macOS CI, whose `macos-15` and
  `macos-26` runners are arm64.
- **Exact-type gate:** a subclass that overrides a virtual the step runs through, `evolve()`,
  `drift()`, `diffusion()`, `variance()`, `stdDeviation()` or `apply()`, would inherit a table that
  ignores the override, a silent change to a public contract, which the rules forbid; a documented
  contract plus a probe under `QL_EXTRA_SAFETY_CHECKS`, which only the weekly workflows enable,
  does not prevent it. So `GeneralizedBlackScholesProcess` builds a table only when `typeid(*this)`
  is one of the five classes `ql/processes/blackscholesprocess.hpp` declares, the four thin
  subclasses adding constructors only. Every other subclass keeps `evolve()` until it overrides
  the hook itself, calling a protected builder when its overrides leave the step intact.
  `ExtendedBlackScholesMertonProcess` and `VegaStressedBlackScholesProcess` then need no override,
  and a subclass outside QuantLib sees no change. The same gate guards 3.1, whose fused step no
  longer calls `drift()`: per step there, a vtable load and a `type_info` comparison, once per grid
  in the hook. `typeid` has no use in `ql/` yet, but RTTI is already required, since
  `dynamic_pointer_cast` appears in 219 of its files. The extension's probe, which guesses the
  branch at two points per step, adds nothing to an exact type and is dropped; the step tests pin
  the table against `evolve()` instead.

## 5. Sequence of Upstream Changes

Each step is a branch of `ralfkonrad/QuantLib` per
[`changing-quantlib.md`](../.agents/changing-quantlib.md), measured here before the next one starts.

1. 3.3's time slices, 9.1 and 9.2, the profiling doc's next step. They need no process API;
   `LocalVolStepCache` gains from 9.2 unchanged and from 9.1 once its `localVol` call takes the
   slice overload (profiling doc, 9).
2. 3.1 behind the exact-type gate, with its step test and the forward-rate helper. No process
   API either; QuantLib's own engines gain under a smile.
3. The hook with 3.2, the multivariate signature of 3.7, the gate and the generators' observation.
4. 3.3's rate pair and 3.4, on the hook.
5. 3.5, one process per branch.
6. 3.6, with the missing `MCHullWhiteCapFloorEngine` test.

## 6. Findings Dropped or Adjacent

| Finding                                                                          | Why it is not a candidate                                                                                                                                                                          |
| -------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `OrnsteinUhlenbeckProcess`: two `exp` and a `sqrt` per step, all from `dt`       | Small; `HullWhiteProcess`, `HullWhiteForwardProcess` and `G2Process` wrap it, so 3.6 covers it; alone, only `testPathGenerator` and `testMultiPathGenerator` drive it                              |
| `G2Process`: two `forwardRate`, 11 `exp`, 4 `sqrt`, 4 allocations per step       | No engine drives it through a path generator; one test does                                                                                                                                        |
| `MfStateProcess`: the whole `stdDeviation` depends on `(t, dt)`                  | No Monte Carlo caller; Gaussian1d integration only                                                                                                                                                 |
| `CoxIngersollRossProcess`, `GeometricBrownianMotionProcess`, `SquareRootProcess` | CIR caches `exp(−a·dt)` and the state-free summand of `s2`, as QE does; GBM and the square-root process `√dt` only; the rest depends on the state. `testPathGenerator` locks all three (section 2) |
| `GsrProcess`                                                                     | Already memoizes in `GsrProcessCore`; a table would only replace four map lookups per step                                                                                                         |
| `JointStochasticProcess`                                                         | Already memoizes its correlation; no concrete subclass in QuantLib                                                                                                                                 |
| Heston non-central chi-square and Broadie-Kaya schemes                           | A root solve per step dominates whatever is cached                                                                                                                                                 |
| `MCVarianceSwapEngine`'s per-node `diffusion()`                                  | Its nodes are `SegmentIntegral` sums, not grid times, so no table reproduces them                                                                                                                  |
| Forward rate without `InterestRate` (profiling doc, 4.2 and 9.3)                 | Kept as 3.1's companion; once the hook caches the rates it pays only outside path generation                                                                                                       |
| `TermStructure::maxTime` copying the `DayCounter` (profiling doc, 4.4)           | Leaves the per-path loop with 3.2; a term-structure change, not a step cache                                                                                                                       |
| Sobol, `InverseCumulativeNormal`, `BrownianBridge`, least-squares regression     | Nothing grid-only is left to cache; folding the bridge's `1/√dt` into the step would move bits                                                                                                     |
| Paths in log space                                                               | Moves both locks, and the local volatility needs the price level (profiling doc, 5)                                                                                                                |

## 7. Measurements That Decide the Ranking

Each is a callgrind count from the `profile` preset, one repricing at 2^16 paths, against the same
build without the change. Wall clock on this machine is secondary (profiling doc, 1).

1. **3.3's time slices:** `--market smile-bilinear` and `--market smile-bicubic` against 4.5's
   cached figures, 52.69 G and 422.01 G discrete (profiling doc, 10).
2. **3.1:** the same markets with `--path-generation uncached`, which runs QuantLib's own
   `evolve()`, against 4.5's uncached 127.22 G and 865.79 G. The criterion: the NPVs stay
   bit-identical on this build, as the prototypes of profiling doc 8.2 do.
3. **3.2:** `--market flat --path-generation uncached` against the hook branch. The criterion:
   plain `QuantLib::SingleVariate` lands within 1% of the extension's cached count, with
   bit-identical NPVs. The extension's explicit generator choice alone costs 0.6% (profiling doc,
   4.1).
4. **3.4:** an `MCBarrierEngine` workload under a smile with a continuous-monitoring step count,
   callgrind before and after the table. Until then the repository pricer's 26.3% (profiling doc,
   4.5) is a proxy.
5. **3.5:** a Heston QEM workload with daily steps; a new `src/rke/profile/heston/` executable.
   Conventions as the BonusClassicOption setup: `Actual360`, `NullCalendar`, continuously
   compounded flat curves, daily steps over the Actual360 year fraction.
6. **3.6:** a Hull-White cap/floor priced by `MCHullWhiteCapFloorEngine`, which reaches
   `HullWhiteForwardProcess` only, and `HullWhiteProcess` through `PathGenerator` directly; a new
   profile executable with the same conventions.
7. **3.7:** DHAT on `MCEuropeanBasketEngine`, allocations per step before and after.
