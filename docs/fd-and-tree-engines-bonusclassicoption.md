<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Finite-Difference and Tree Engines for the BonusClassicOption

How to price `BonusClassicOption` with a finite-difference engine and with a binomial
tree next to `MCBonusClassicEngine`, which QuantLib classes each one is built from, and
which tests make the numbers checkable. The finite-difference engine of section 3 is
implemented; the tree engine of section 4 is not.

## 1. The Product and the One Fact Every Engine Needs

At maturity `T` the holder receives `max(S_T, B)` if the underlying never traded at or
below the barrier `H` during the life of the certificate, and `S_T` otherwise. The payoff
is the asset plus a down-and-out put struck at the bonus level `B` with barrier `H` and no
rebate. `BonusClassicPayoff(H, B)` returns `S` at or below `H` and `max(S, B)` above it.

On knock-out the holder still receives `S_T`. The value of a knocked-out certificate at
time `t` is therefore the asset leg `S · exp(-q (T - t))`, not zero, and that is the
boundary value every PDE and every tree node below the barrier has to carry. Under
continuous monitoring the certificate equals `S_0 · exp(-q T) + DOP(B, H)` exactly, by
linearity.

`MCBonusClassicEngine` prices two products, selected by `isBiased`:

| `isBiased` | Monitoring                                                     | Lock in the test suite |
| ---------- | -------------------------------------------------------------- | ---------------------: |
| `true`     | on the grid `TimeGrid(T, max(floor(timeStepsPerYear · T), 1))` |     106.96041418042263 |
| `false`    | continuous, through the Brownian-bridge survival probability   |     105.88329042929441 |

`T` is `process->time(exercise->lastDate())`, the day counter of the risk-free curve.
The test market is the one in `src/rke/testsuite/BonusClassicOption.cpp`: evaluation
date 22 Jun 2025, maturity 22 Nov 2025 (`T = 153/360` under `Actual360`), spot 100,
`H = 90`, `B = 120`, risk-free rate 1%, dividend yield 3%, volatility 20%, flat curves,
`NullCalendar` volatility, `BlackScholesMertonProcess`; the MC grid has 42 steps from
`mcTimeStepsPerYear = 100`. Nothing under `src/rke` or `.agents` uses finite differences
or lattices yet.

## 2. Routes Considered

| Route                                                    | Prices                  | Verdict                                                                                                                                                                                              |
| -------------------------------------------------------- | ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A. Compose QuantLib's barrier engines on the replication | continuous only         | Rejected. The only available check is the replication the engine itself computes, so it tests QuantLib, not us, and it cannot price the product the MC biased engine prices.                         |
| B. Native Fdm engine                                     | continuous and discrete | Chosen. Same blocks as `FdBlackScholesBarrierEngine`; `FdmDiscountDirichletBoundary` fed with the dividend curve yields the asset-leg boundary without a new class. First engine with Greeks.        |
| C. Binomial tree engine                                  | every lattice step      | Chosen as a second engine, to benchmark lattices against FD and MC. `BinomialBarrierEngine` is bound to `BarrierOption::arguments`, so its `calculate()` is copied, with our own `DiscretizedAsset`. |

Every QuantLib signature and behaviour this plan relies on was read in the pinned
submodule, commit `966a4cc10`; a later pointer move re-checks section 3.3 and the
Greeks in section 4.

QuantLib's own yardsticks against Haug's tabulated barrier prices, in
`test-suite/barrieroption.cpp`: `FdBlackScholesBarrierEngine` at `tGrid 200, xGrid 400`
within `5e-3` absolute; `BinomialBarrierEngine<CoxRossRubinstein, DiscretizedBarrierOption>`
at 400 steps within `1.1e-2`, the Derman-Kani variant within `4e-2`.

## 3. `FdBlackScholesBonusClassicEngine`

Files: `src/rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.{hpp,cpp}`,
guard `FDBLACKSCHOLESBONUSCLASSICENGINE_HPP`, both listed in `src/rke/ql/ext/CMakeLists.txt`
ahead of the `MCBonusClassicEngine` entries. The template to follow is
`FdBlackScholesBarrierEngine::calculate()`, minus its dividend handler, local-volatility
switch and in-barrier parity block.

### 3.1 Interface

```cpp
class FdBlackScholesBonusClassicEngine : public BonusClassicOption::engine {
  public:
    explicit FdBlackScholesBonusClassicEngine(
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
        QuantLib::Size monitoringStepsPerYear = QuantLib::Null<QuantLib::Size>(),
        QuantLib::Size tGrid = 100,
        QuantLib::Size xGrid = 100,
        QuantLib::Size dampingSteps = 0,
        const QuantLib::FdmSchemeDesc& schemeDesc = QuantLib::FdmSchemeDesc::Douglas());

    void calculate() const override;
    //! the monitoring grid, by the MC rule; QL_FAIL in continuous mode
    [[nodiscard]] QuantLib::TimeGrid timeGrid() const;
    [[nodiscard]] bool monitorsContinuously() const;
};

class FdmBonusClassicKnockOutCondition : public QuantLib::StepCondition<QuantLib::Array> {
  public:
    //! \pre one node of \p mesher lies on log(barrier); QL_REQUIRE checks this
    FdmBonusClassicKnockOutCondition(const QuantLib::ext::shared_ptr<QuantLib::FdmMesher>& mesher,
                                     std::vector<QuantLib::Time> monitoringTimes,
                                     QuantLib::Real barrier,
                                     QuantLib::Time maturity,
                                     QuantLib::ext::shared_ptr<QuantLib::YieldTermStructure> dividendYield);
    void applyTo(QuantLib::Array& a, QuantLib::Time t) const override;
};
```

`monitoringStepsPerYear` is the one monitoring parameter: `Null<Size>()` means continuous
monitoring, any other value selects discrete monitoring on the MC engine's grid rule, so
both engines price the same product. One parameter leaves no contradictory state, which a
`bool` plus a step count would allow, and `Null<>` is this library's "not given" idiom.
The documentation block says that a value corresponds to `isBiased = true` with the same
`timeStepsPerYear`.

### 3.2 `calculate()`

In order: cast the payoff to `BonusClassicPayoff` and `QL_REQUIRE` it, require a positive
bonus level, European exercise, `spot > 0` and `!triggered(spot)` ("barrier touched"), then
`maturity = process_->time(exercise->lastDate())`. The two modes differ in two parts:

| Part           | Continuous                                                                                                                                                             | Discrete                                                                                                                                                                                                                                                                                                                    |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Mesher         | `FdmBlackScholesMesher(xGrid, process, T, B, xMinConstraint)`, `xMinConstraint` the largest log-spot whose exponential does not exceed `H`: the first node sits on `H` | `xMin`, `xMax` from `locations()` of `FdmBlackScholesMesher(xGrid, process, T, B)`, then `Concentrating1dMesher(xMin, xMax, xGrid, {(log(H), 0.1, true), (log(B), 0.1, false)})`, the tuple overload, in an `FdmMesherComposite`: one node on `H` to a Brent solve at machine precision, density at `H` and at the kink `B` |
| Step condition | none                                                                                                                                                                   | `FdmBonusClassicKnockOutCondition` at the points `1..n-1` of `timeGrid()`, inside an `FdmStepConditionComposite`                                                                                                                                                                                                            |

The parts both modes share: the asset leg on the lowest node `x_0`,
`FdmDiscountDirichletBoundary(mesher, dividendYield.currentLink(), T, exp(x_0), 0, Lower)`,
i.e. `exp(x_0) · qTS.discount(T) / qTS.discount(t)`; `FdmLogInnerValue(payoff, mesher, 0)`
with the `BonusClassicPayoff` itself, which already pays `S` at or below `H` and so
monitors maturity in discrete mode, and reduces to `max(S, B)` on a grid that starts at
`H`; `FdmSolverDesc{mesher, bcSet, condition, calculator, maturity, tGrid, dampingSteps}`;
`FdmBlackScholesSolver(Handle(process_), B, desc, schemeDesc_)`; then `value`, `delta`,
`gamma`, `theta` into `results_`, `value` first: `valueAt`, `deltaAt` and `gammaAt` run
the rollback, `thetaAt` reads its snapshot without running it. `B` is also the strike
`FdmBlackScholesOp` reads its variance slice at; under a smile that choice is the engine's,
and the tests use a flat volatility.

The step condition stores `exp(mesher->locations(0))` the way `FdmDividendHandler` does,
finds the node `k` on `H` once in its constructor, by `close_enough` since the mesher
places it by a Brent solve, and on a monitoring time `t` sets, with
`A(S, t) = S · qTS.discount(T) / qTS.discount(t)` the asset leg,

```text
a[i] = A(S_i, t)                               for i < k
a[k] = w · A(H, t) + (1 - w) · a[k],   w = dminus(k) / (dminus(k) + dplus(k))
```

and leaves `i > k` alone. The node on `H` gets the average of its cell, knocked out below
`H` and alive above, which is what `FdmLogInnerValue` does with the payoff's jump at
maturity. A jump has to sit mid-cell for second order: a node on the jump that takes one
side's value moves the effective barrier half a cell, an `O(dx)` error, see section 3.3.
The inclusive barrier is a convention on a null set; the PDE cannot tell `<=` from `<`,
and the convention lives in the payoff, `triggered()` and the MC pricer. The overwrite at
`k` is not idempotent, so the condition acts once per monitoring time: a damping rollback
that ends on one hands it to the main rollback, which applies its first stopping time
again.

Between monitoring dates the whole grid, the knocked-out region included, evolves under
the PDE, which is what discrete monitoring means. At the lowest node QuantLib's operator
drops the diffusion term and goes one-sided, which the asset leg does not solve, hence the
Dirichlet value in both modes. QuantLib imposes it after each implicit solve, with the
next node already coupled to the unconstrained value, so the continuous price converges
at first order in time: 4.3e-6 relative off the replication at `tGrid 200`, 1.1e-6 at
`tGrid 800`, both at `xGrid 400`.

### 3.3 QuantLib Behaviour the Design Relies On

- `FiniteDifferenceModel::rollbackImpl` walks a uniform grid `dt = T / tGrid`, sub-steps
  to every stopping time and calls `applyTo(a, stoppingTime)` there, but it also calls
  `applyTo(a, next)` after every ordinary step. The condition has to gate on its own
  monitoring times, with exact equality, as `FdmDividendHandler::applyTo` does; the `t`
  passed at a hit is the stored stopping time itself. Monitoring times need not lie on the
  time grid.
- `t` counts from the valuation date in the process's time measure. Boundary conditions
  are set inside the scheme step, the step condition runs after it.
- `Fdm1DimSolver` takes its theta snapshot at `0.99 · min(1/365, first stopping time)` and
  returns `Null<Real>()` for theta if a stopping time is zero. Never monitor at `t = 0`.
  The MC grid's first point, about 0.0101, is above `1/365`.
- Maturity is left out of the stopping times. `FdmLogInnerValue` is a cell-averaging
  calculator, and a node on `H` carries the cell average at `T`; a condition fired at `T`
  would overwrite it with `H`.
- The mesher's default range at `eps = 1e-4` and scale 1.5 is about `S ∈ [48, 207]` for
  the test market, so `B = 120` lies inside. Nothing forces a node on `H` unless the
  `Concentrating1dMesher` is asked for it; `FdmBlackScholesMesher` never passes
  `requireCPoint`, which is why discrete mode builds the mesher itself. On a uniform grid
  the effective barrier sits anywhere within one cell above `H`, and with `dV/dH ≈ -1`
  (the two MC locks differ by 1.08 for a barrier shift of 1.05) that is up to `3e-3`
  relative at `xGrid 400`.
- `std::exp(std::log(H))` exceeds `H` for about a third of all levels, 85 among them. A
  continuous grid starting at `std::log(H)` then pays the bonus on its first node at
  maturity, 2.1e-4 relative off at barrier 85, so the engine steps the start down with
  `std::nextafter` until it maps back at or below `H`.
- A node on `H` is not enough on its own. Measured with a scratch Crank-Nicolson solver
  on the payoff monitored at maturity alone, which has a closed form
  (`S e^{-qT} + Put(B) - Put(H) - (B - H) · digital put at H`, 110.8768 in the test
  market), uniform log grid with a node on `H`, `tGrid 200`, two Rannacher steps:

  | `xGrid` | node on `H` takes the knocked-out value | node on `H` takes the cell average |
  | ------: | --------------------------------------: | ---------------------------------: |
  |     200 |                               `-2.4e-3` |                            `-5e-6` |
  |     400 |                               `-1.2e-3` |                            `+2e-6` |
  |     800 |                               `-6.0e-4` |                            `+6e-7` |

  Relative errors; the left column halves with `dx`, the right one is second order. The
  left column is what a plain `S_i <= H` overwrite produces, and it is larger than the
  `5e-4` the FD-versus-MC case expects, so the weighted overwrite in section 3.2 is the
  design, not a fallback.

- Discrete mode re-creates a jump at `H` on every monitoring date, and
  `FdmBackwardSolver` damps only the first `dampingSteps` after maturity. Douglas with
  `theta = 0.5` is Crank-Nicolson in one dimension, and the price shows it. Discrete mode
  in the test market, `xGrid × tGrid`:

  | Grid          |   Douglas |    TrBDF2 |
  | ------------- | --------: | --------: |
  | `400 × 200`   | 106.94848 | 106.95083 |
  | `800 × 400`   | 106.95421 | 106.95309 |
  | `1600 × 800`  | 106.95490 | 106.95365 |
  | `3200 × 1600` | 106.95312 | 106.95378 |
  | `6400 × 3200` | 106.95349 | 106.95380 |

  Douglas moves non-monotonically, and two damping steps at maturity do not remove it.
  TrBDF2 is second order and L-stable: its differences shrink by about four per doubling.
  The discrete tests price with TrBDF2; the engine keeps QuantLib's `Douglas` default and
  warns. A Rannacher restart after every monitoring date would need the QuantLib change in
  section 3.5, and the price does not need it.

### 3.4 Documentation Block

Per "Documentation Comments" in `.agents/extending-rke-ql-ext.md`: the brief, the PDE
with its boundary value and
the step-condition formula, the conventions (time from the process, discounting on the
risk-free curve, the asset leg on the dividend curve), `\param monitoringStepsPerYear` with
the `Null` switch, `\ingroup barrierengines`, one `\test` line per test in section 5, and
five `\warning` entries: discrete mode is second order only because a node is forced on
`H` and takes its cell average there; Crank-Nicolson converges erratically in time under
discrete monitoring, TrBDF2 does not; the barrier has to lie inside
`FdmBlackScholesMesher`'s range in discrete mode; `qTS->discount(t)` reads `t` in the
dividend curve's day counter, the same approximation QuantLib's engines make and exact
when both curves share a day counter; neither discrete dividends nor local volatility are
supported. The instrument header's
`\warning no default engine is set` and its `\test` line gain the new engines.

### 3.5 Changes to QuantLib

A `private` QuantLib member may be widened to `protected` or `public` where that
serves this plan. Such a change lives on a topic branch of `ralfkonrad/QuantLib` and
follows `.agents/changing-quantlib.md` for the worktree, the formatting of changed
lines only and the pointer move. That guide and the hard constraint in `AGENTS.md`
name performance work as the only reason to edit the submodule, so the commit that
moves the pointer also widens that sentence to "performance, or access an extension in
`src/rke` needs"; `.agents/maintaining-agent-docs.md` applies to that edit.

Every API the plan uses is public, so the first implementation needs no such change.
One place would benefit, and only once a measured residual demands it:

- `Fdm1DimSolver` keeps `solverDesc_`, `schemeDesc_`, `op_`, `thetaCondition_`,
  `conditions_`, `x_`, `initialValues_`, `resultValues_` and `interpolation_` private,
  and its `performCalculations()` rolls back from maturity to zero in one
  `FdmBackwardSolver::rollback` call, so the Rannacher damping steps run once, at
  maturity. Wade, Khaliq, Yousuf, Vigo-Aguiar and Deininger (2007), "On smoothing of
  the Crank-Nicolson scheme and higher order schemes for pricing barrier options",
  Journal of Computational and Applied Mathematics 204, restart the damping after
  every monitoring date. With those members `protected`, an
  `FdmBonusClassicSolver : Fdm1DimSolver` in `src/rke/ql/ext` overrides
  `performCalculations()` and rolls back one monitoring
  interval at a time, `rollback(rhs, t_k, t_{k-1}, steps_k, dampingSteps)`, applying
  the overwrite of section 3.2 itself between the calls. The overwrite then leaves
  the step-condition composite, which keeps the theta snapshot only: the averaging at
  the node on `H` is not idempotent, so it must run exactly once per date, and a
  stopping time at an interval end would run it twice. `FdmBlackScholesSolver` builds
  its `Fdm1DimSolver` privately, so the engine then constructs `FdmBlackScholesOp`,
  which is public, and the subclass itself, and repeats the three one-line
  conversions from `d/dx` to delta and gamma that `FdmBlackScholesSolver` makes.

Where an access change does not help: `BinomialBarrierEngine` and
`DiscretizedBarrierOption` are bound to `BarrierOption::arguments` by type, not by
access; `FdmBlackScholesMesher` has no `requireCPoint` parameter, which would be an
API addition rather than a visibility change, and its `locations()` already gives
the range discrete mode needs.

## 4. `BinomialBonusClassicEngine<Tree>` and `DiscretizedBonusClassicOption`

Files: `src/rke/ql/ext/pricingengines/bonusclassic/BinomialBonusClassicEngine.hpp`, a
header-only template like the MC engine, and `DiscretizedBonusClassicOption.{hpp,cpp}`;
include guards from the bare file names; CMake lists alphabetical. The template to copy
is `BinomialBarrierEngine::calculate()`, with the Greeks taken from the newer
`BinomialVanillaEngine`: delta and gamma guarded against zero node spacing with
`Null<Real>()`, and theta from `blackScholesTheta` in `ql/pricingengines/greeks.hpp`. The
barrier engine's `(p2m - p0) / grid[2]` theta assumes the middle node at step two equals
the spot, which holds for CRR and Trigeorgis only.

### 4.1 Interface

```cpp
class DiscretizedBonusClassicOption : public QuantLib::DiscretizedAsset {
  public:
    DiscretizedBonusClassicOption(const BonusClassicOption::arguments& arguments,
                                  QuantLib::Rate dividendYield, // the flattened yield the tree uses
                                  QuantLib::Time maturity);
    void reset(QuantLib::Size size) override;
    [[nodiscard]] std::vector<QuantLib::Time> mandatoryTimes() const override; // {maturity}

  protected:
    void postAdjustValuesImpl() override;
};

template <class Tree> // the tree type, as in BinomialBarrierEngine; T below is maturity
class BinomialBonusClassicEngine : public BonusClassicOption::engine {
  public:
    BinomialBonusClassicEngine(
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
        QuantLib::Size timeSteps,
        QuantLib::Size maxTimeSteps = 0); // 0: max(1000, 5 * timeSteps), as QuantLib
    void calculate() const override;
};
```

### 4.2 Behaviour

`postAdjustValuesImpl()` reads the per-node spot `S_j` from `method()->grid(time())`, the
lattice's `BlackScholesLattice::grid(Time)`. At maturity, `isOnTime(maturity)`, it sets
`values_[j] = payoff(S_j)`. At every other lattice time it sets
`values_[j] = S_j · exp(-q (T - t))` where `S_j <= H`, the same inclusive test as the
payoff and `engine::triggered()`, and leaves the nodes above `H` to the rolled-back
value. `TreeLattice::partialRollback` adjusts after every step but the last of a
`rollback(to)` call, and `rollback` adjusts at `to` itself, so every lattice step is
monitored exactly once, `t = 0` included, where `!triggered(s0)` guarantees a no-op. That
is the tree's stand-in for continuous monitoring. `reset(size)` zero-fills `values_` and
calls `adjustValues()`, as `DiscretizedBarrierOption::reset` does.

`calculate()`: cast the payoff, require European exercise, `s0 > 0` and `!triggered(s0)`;
flatten the risk-free rate and the dividend yield as continuous zero rates at the maturity
date in each curve's own day counter, and the volatility as
`blackVol(exercise->lastDate(), s0)`, the date overload, all as `BinomialBarrierEngine`
does; `maturity` is the risk-free day counter's year fraction, the same number as
`process->time()`. Apply Boyle-Lau under the same rule as `BinomialBarrierEngine` in the
pinned QuantLib, `std::is_base_of_v<CoxRossRubinstein, Tree>` and
`maxTimeSteps > timeSteps`: the first `floor(i² · v² · T / ln²(s0 / H))` above
`timeSteps`, capped at `maxTimeSteps`. Round the step count up to odd for `LeisenReimer`
and `Joshi4` before building anything: those trees take `steps + 1` for an even `steps`
while QuantLib's engines build `BlackScholesLattice` and its `TimeGrid` with the even
count, a `dt` mismatch. Then `Tree tree(bs, T, steps, B)`, where `B` is the strike only
`LeisenReimer` and `Joshi4` centre on, `BlackScholesLattice<Tree>(tree, r, T, steps)`,
`DiscretizedBonusClassicOption option(arguments_, q, T)`, `option.initialize(lattice, T)`,
rollback to the second, first and zeroth time of the lattice's `TimeGrid`, Greeks,
results.

Decisions:

- One monitoring mode. A date schedule would have to be snapped onto the lattice
  (`TimeGrid::index` fails for an off-grid time), the node layout gives an `O(dS)` barrier
  error either way, and the FD engine covers both modes.
- No Derman-Kani variant; the template parameter is the tree type only.
- The asset takes the flattened `q`, not the process. Under a sloped dividend curve the
  tree and the asset leg have to agree, and the tree only sees the flattened process.

Boyle-Lau in the test market (`v² T / ln²(s0 / H) = 1.5314`): 100 steps become 124, 200
become 220, 400 become 442, 1000 become 1035. The layer `m` steps below spot then lands
at or just below `H` (89.9938 at 442 steps) and is knocked out by `<=` and `<` alike.
Without Boyle-Lau, 400 steps put the first knocked-out layer at 89.50, an effective
barrier 0.55% low.

## 5. Tests

All in `src/rke/testsuite/BonusClassicOption.cpp`, inside `BonusClassicOptionTests`, each
with the `// NOLINT(misc-use-internal-linkage)` the existing cases carry.

First a test-local refactor without behaviour change: `replicationPrice(process,
OptionData, exerciseDate, barrier)`, the asset leg `x0 · qTS.discount(T)` plus an
`AnalyticBarrierEngine` down-and-out put struck at `B`, and `bgkShiftedBarrier(barrier,
vol, dt)` with `beta = 0.5826`, both in the file's anonymous namespace. The two existing
replication tests switch to them. Not in `src/rke/common`: that library must not depend
on `test-suite/utilities`.

Every FD case uses `tGrid 200, xGrid 400`, QuantLib's own sizes, and discrete mode uses
`mcTimeStepsPerYear` and `FdmSchemeDesc::TrBDF2()`, so FD and MC monitor the same 42
points; the FD-versus-MC case asserts `timeGrid().size() - 1 == 42` and both grids equal
point by point. The tree cases use `treeTimeSteps = 400` with
`CoxRossRubinstein`.

| Case                                            | Compares                                                                             | Tolerance                                               |
| ----------------------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------- |
| `testBonusClassicOptionFdContinuousValuation`   | regression lock, continuous FD                                                       | `1e-8`                                                  |
| `testBonusClassicOptionFdContinuousReplication` | continuous FD vs `replicationPrice` at `H`, for barriers 90 and 85                   | `1.5e-5`; measured `4.3e-6` and `3.2e-6` relative       |
| `testBonusClassicOptionFdValuation`             | regression lock, discrete FD                                                         | `1e-8`                                                  |
| `testBonusClassicOptionFdReplication`           | discrete FD vs `replicationPrice` at the BGK-shifted barrier, `dt` from `timeGrid()` | `1e-3`; measured `4.7e-4`, `4.5e-4` of it the BGK error |
| `testBonusClassicOptionFdVersusMc`              | discrete FD vs `MCBonusClassicEngine<LowDiscrepancy>` biased on the same grid        | `3e-4`; measured `9.0e-5`                               |
| `testBonusClassicOptionFdMonitoringOrder`       | continuous FD below discrete FD; `timeGrid()` fails in continuous mode               | `BOOST_CHECK_LT`                                        |
| `testBonusClassicOptionBinomialValuation`       | regression lock, CRR 400 steps, 442 after Boyle-Lau                                  | `1e-8`                                                  |
| `testBonusClassicOptionBinomialReplication`     | tree vs `replicationPrice` at `H`                                                    | about 3× the measured residual; expect `3e-4` relative  |
| `testBonusClassicOptionBinomialBoyleLau`        | the same with `maxTimeSteps = timeSteps`: the Boyle-Lau residual is the smaller one  | `BOOST_CHECK_LT`, both residuals reported               |

Each tolerance comment records the measured residual and why it is discretisation error
rather than noise, as "The Valuation Test Is a Regression Lock" in `AGENTS.md` and "Two
Kinds of Price Test" in the extending guide demand. A lock is re-derived, never pasted,
after any change to grid, scheme, node placement, flattening or Boyle-Lau. The
FD-versus-MC residual is the sum of two deterministic errors, which FD at `6400 × 3200`
separates: the FD discretisation at `400 × 200`, `2.8e-5` relative, and the MC lock's own
distance from the discrete price, `6.2e-5`, fixed by its seed.

## 6. Branches and Commits

Two implementation branches off `master`, each in its own worktree with a draft pull
request, one concern per commit:

- `bonusclassic-fd-engine`: the replication helpers; continuous mode with its lock and
  replication test; discrete mode with the step condition, `timeGrid()` and its four tests;
  the documentation paragraph.
- `bonusclassic-tree-engine`: the discretized asset, the engine and its three tests; the
  documentation paragraph.

The documentation paragraph amends "A New Pricing Engine" in
`.agents/extending-rke-ql-ext.md` with one short list per engine kind: for FD, derive
from the instrument's engine, assemble mesher, boundary, step condition and solver, gate a
step condition on `t` because the model calls it after every step, put a jump mid-cell or
average the node it sits on, and expose the monitoring times as `timeGrid()` since they
define the product; for a tree, a `DiscretizedAsset` subclass with `reset`,
`postAdjustValuesImpl` and `mandatoryTimes`, an engine that flattens rate, yield and
volatility itself and hands the flattened yield to the asset, monitoring per lattice step,
Boyle-Lau for CRR only. No new file under `.agents/`, so its README and the `AGENTS.md`
table stay as they are.

Adding both engines to `src/rke/common/BonusClassicOptionSetup` and the benchmark is a
follow-up, not part of either branch. Should section 3.5 be exercised, the QuantLib
change is pushed to `ralfkonrad/QuantLib` first, and the pointer bump is its own commit
on `bonusclassic-fd-engine`, apart from the solver that uses it.

## 7. Validation

Per implementation branch, before the pull request leaves draft:

```bash
cmake --preset release && cmake --build --preset release
ctest --preset release
build/release/src/rke/testsuite/rke_testsuite -l message --run_test=RkeQLExtTestSuite/BonusClassicOptionTests
cmake --preset clang-tidy && cmake --build --preset clang-tidy
clang-format -i <touched files under src/>
git submodule status
```

clang-tidy checks expected to fire, and how the code pre-empts them:
`readability-function-cognitive-complexity` on a two-mode `calculate()`, so the mesher,
the Boyle-Lau count and the flattening live in helpers; the narrowing-conversion checks on
`Size(steps · T)` and the Boyle-Lau product, so each takes an explicit `static_cast` of
`std::floor`; `cppcoreguidelines-prefer-member-initializer` for `maxTimeSteps_`;
`cppcoreguidelines-avoid-const-or-ref-data-members`, so no `const` members and the payoff
stored by value; `modernize-pass-by-value` and `performance-unnecessary-value-param`, so
`shared_ptr` and `vector` arguments are taken by value and moved; and the
`modernize-return-braced-init-list` NOLINT on the `TimeGrid` return, as the MC engine has.
On the FD engine also `readability-trailing-comma` on a braced list split over lines,
`boost-use-ranges` and `llvm-use-ranges` on iterator-pair algorithms, answered with
`boost::range`, and `modernize-use-default-member-init` on members the constructor body
sets.

## 8. Open Risks

- `FdmLogInnerValue` averages a cell by Simpson's rule, stopping after 64 to 128
  subintervals at an absolute accuracy of `5e-5 · (f(a) + f(b))`, so its value on the
  node across the jump at `H` is not exact. Under TrBDF2 the discrete price still moves
  by only `2.7e-5` absolute between `3200 × 1600` and `6400 × 3200`, so the effect stays
  below that.
- The FD boundary and step condition read `t` with the dividend curve's day counter, the
  risk-free curve's time measure elsewhere; equal whenever both day counters agree, which
  holds in every test.
- Boyle-Lau raises the tree's step count to `max(1000, 5 · timeSteps)` by default. A
  benchmark against FD or MC fixes `maxTimeSteps = timeSteps` or reports the effective
  count.
- The tree flattens the dividend curve, the MC engine does not; under a sloped curve the
  two disagree. The tests use flat curves and the engine's `\warning` says so.
- Theta is not tested: the tree's is the Black-Scholes PDE identity through
  `blackScholesTheta`, the FD engine's a difference quotient over the snapshot at
  `0.99 / 365`, the same time in both modes since the first monitoring time, about
  `0.0101`, is larger. The two definitions need not agree to the tolerance of a test.
