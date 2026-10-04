<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Finite-Difference and Tree Engines for the BonusClassicOption

How to price `BonusClassicOption` with a finite-difference engine and with a binomial
tree next to `MCBonusClassicEngine`, which QuantLib classes each one is built from, and
which tests make the numbers checkable. Nothing here is implemented.

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
    FdmBonusClassicKnockOutCondition(QuantLib::ext::shared_ptr<QuantLib::FdmMesher> mesher,
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
`maturity = process_->time(exercise->lastDate())`. The two modes differ in three parts:

| Part           | Continuous                                                                                                                        | Discrete                                                                                                                                                                                                     |
| -------------- | --------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Mesher         | `FdmBlackScholesMesher(xGrid, process, T, B, xMinConstraint = log(H))`: the first node sits on `H`                                | the range of `FdmBlackScholesMesher(xGrid, process, T, B)`, then `Concentrating1dMesher(xMin, xMax, xGrid, {log(H), 0.1}, requireCPoint = true)` in an `FdmMesherComposite`, so one node sits exactly on `H` |
| Boundary       | `FdmDiscountDirichletBoundary(mesher, dividendYield.currentLink(), T, H, 0, Lower)`, i.e. `H · qTS.discount(T) / qTS.discount(t)` | none; the operator uses one-sided stencils at the ends, as `FdBlackScholesVanillaEngine` runs                                                                                                                |
| Step condition | none                                                                                                                              | `FdmBonusClassicKnockOutCondition` at the points `1..n-1` of `timeGrid()`, inside an `FdmStepConditionComposite`                                                                                             |

The parts both modes share: `FdmLogInnerValue(payoff, mesher, 0)` with the
`BonusClassicPayoff` itself, which already pays `S` at or below `H` and so monitors
maturity in discrete mode, and reduces to `max(S, B)` on a grid that starts at `H`;
`FdmSolverDesc{mesher, bcSet, condition, calculator, maturity, tGrid, dampingSteps}`;
`FdmBlackScholesSolver(Handle(process_), B, desc, schemeDesc_)`; then `value`, `delta`,
`gamma`, `theta` into `results_`, in that order, because `thetaAt` does not trigger the
calculation.

The step condition stores `exp(mesher->locations(0))` the way `FdmDividendHandler` does,
and on a monitoring time sets `a[i] = S_i · qTS.discount(T) / qTS.discount(t)` for every
node with `S_i <= H`. Between monitoring dates the knocked-out region evolves under the
PDE; `S · exp(-q (T - t))` is an exact solution of it, so the overwrite is needed at the
monitoring times only.

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
  the test market, so `B = 120` lies inside. Nothing forces a node on `H` unless
  `requireCPoint` is set: on a uniform grid the effective barrier sits anywhere within one
  cell above `H`, and with `dV/dH ≈ -1` (the two MC locks differ by 1.08 for a barrier
  shift of 1.05) that is about `3e-3` relative at `xGrid 400`, above the tolerance the
  replication test needs. With the node on `H` the error is `O(dx²)` and the inclusive
  barrier is represented exactly. The fallback, should concentrating the grid at `H` hurt
  the kink at `B`, is a uniform grid and a weighted overwrite of the cell that straddles
  `H`.

### 3.4 Documentation Block

Per `.agents/extending-rke-ql-ext.md` §6: the brief, the PDE with its boundary value and
the step-condition formula, the conventions (time from the process, discounting on the
risk-free curve, the asset leg on the dividend curve), `\param monitoringStepsPerYear` with
the `Null` switch, `\ingroup barrierengines`, one `\test` line per test in section 5, and
three `\warning` entries: discrete mode is second order only because a node is forced on
`H`; `qTS->discount(t)` reads `t` in the dividend curve's day counter, the same
approximation QuantLib's engines make and exact when both curves share a day counter;
neither discrete dividends nor local volatility are supported. The instrument header's
`\warning no default engine is set` and its `\test` line gain the new engines.

## 4. `BinomialBonusClassicEngine<T>` and `DiscretizedBonusClassicOption`

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

template <class T>
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

`postAdjustValuesImpl()` reads the per-node spot from `method()->grid(time())`. At
maturity it sets `values_[j] = payoff(grid[j])`. At every other lattice time it sets
`values_[j] = grid[j] · exp(-q (T - t))` where `grid[j] <= H`, the same inclusive test as
the payoff and `engine::triggered()`, and leaves the nodes above `H` to the rolled-back
value. `TreeLattice::partialRollback` adjusts after every step, so this is monitoring on
every lattice step, the tree's stand-in for continuous monitoring.

`calculate()`: cast the payoff, require European exercise, `s0 > 0` and `!triggered(s0)`;
flatten the risk-free rate and the dividend yield as continuous zero rates at maturity in
each curve's own day counter, and the volatility as `blackVol(T, s0)`, the convention of
QuantLib's lattice engines; `maturity` is the risk-free day counter's year fraction, the
same number as `process->time()`. Apply Boyle-Lau when `T` derives from
`CoxRossRubinstein` and `maxTimeSteps > timeSteps`: the first
`floor(i² · v² · T / ln²(s0 / H))` above `timeSteps`, capped at `maxTimeSteps`. Round the
step count up to odd for `LeisenReimer` and `Joshi4` before building anything: QuantLib's
engines build the lattice with the even count while those trees use the odd one, a `dt`
mismatch. Then `T tree(bs, T, steps, B)`, `BlackScholesLattice<T>(tree, r, T, steps)`,
`DiscretizedBonusClassicOption option(arguments_, q, T)`, `option.initialize(lattice, T)`,
rollback to `grid[2]`, `grid[1]` and `0`, Greeks, results.

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
`mcTimeStepsPerYear`, so FD and MC monitor the same 42 points; the FD-versus-MC case
asserts `timeGrid().size() - 1 == 42`. The tree cases use `treeTimeSteps = 400` with
`CoxRossRubinstein`.

| Case                                            | Compares                                                                             | Tolerance                                              |
| ----------------------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------ |
| `testBonusClassicOptionFdContinuousValuation`   | regression lock, continuous FD                                                       | `1e-8`, number measured                                |
| `testBonusClassicOptionFdContinuousReplication` | continuous FD vs `replicationPrice` at `H`                                           | about 3× the measured residual; expect `1e-4` relative |
| `testBonusClassicOptionFdValuation`             | regression lock, discrete FD                                                         | `1e-8`                                                 |
| `testBonusClassicOptionFdReplication`           | discrete FD vs `replicationPrice` at the BGK-shifted barrier, `dt` from `timeGrid()` | `1e-3`, the BGK error dominates                        |
| `testBonusClassicOptionFdVersusMc`              | discrete FD vs `MCBonusClassicEngine<LowDiscrepancy>` biased on the same grid        | measured; expect `5e-4`                                |
| `testBonusClassicOptionFdMonitoringOrder`       | continuous FD below discrete FD                                                      | `BOOST_CHECK_LT`                                       |
| `testBonusClassicOptionBinomialValuation`       | regression lock, CRR 400 steps, 442 after Boyle-Lau                                  | `1e-8`                                                 |
| `testBonusClassicOptionBinomialReplication`     | tree vs `replicationPrice` at `H`                                                    | about 3× the measured residual; expect `3e-4` relative |
| `testBonusClassicOptionBinomialBoyleLau`        | the same with `maxTimeSteps = timeSteps`: the Boyle-Lau residual is the smaller one  | `BOOST_CHECK_LT`, both residuals reported              |

Each tolerance comment records the measured residual and why it is discretisation error
rather than noise, as `AGENTS.md` §5.1 and the extending guide demand. A lock is
re-derived, never pasted, after any change to grid, scheme, node placement, flattening or
Boyle-Lau.

## 6. Branches and Commits

Two implementation branches off `master`, each in its own worktree with a draft pull
request, one concern per commit:

- `bonusclassic-fd-engine`: the replication helpers; continuous mode with its lock and
  replication test; discrete mode with the step condition, `timeGrid()` and its four tests;
  the documentation paragraph.
- `bonusclassic-tree-engine`: the discretized asset, the engine and its three tests; the
  documentation paragraph.

The documentation paragraph amends `.agents/extending-rke-ql-ext.md` §3 with one short
list per engine kind: for FD, derive from the instrument's engine, assemble mesher,
boundary, step condition and solver, gate a step condition on `t` because the model calls
it after every step, and expose the monitoring times as `timeGrid()` since they define the
product; for a tree, a `DiscretizedAsset` subclass with `reset`, `postAdjustValuesImpl` and
`mandatoryTimes`, an engine that flattens rate, yield and volatility itself and hands the
flattened yield to the asset, monitoring per lattice step, Boyle-Lau for CRR only. No new
file under `.agents/`, so its README and the `AGENTS.md` table stay as they are.

Adding both engines to `src/rke/common/BonusClassicOptionSetup` and the benchmark is a
follow-up, not part of either branch.

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

## 8. Open Risks

- Concentrating the FD grid at `H` with density 0.1 thins it near the kink at `B`. If the
  continuous and discrete residuals disagree with the second-order expectation, switch to
  the uniform grid and the weighted overwrite of the straddling cell.
- The FD boundary and step condition read `t` with the dividend curve's day counter, the
  risk-free curve's time measure elsewhere; equal whenever both day counters agree, which
  holds in every test.
- Boyle-Lau raises the tree's step count to `max(1000, 5 · timeSteps)` by default. A
  benchmark against FD or MC fixes `maxTimeSteps = timeSteps` or reports the effective
  count.
- The tree flattens the dividend curve, the MC engine does not; under a sloped curve the
  two disagree. The tests use flat curves and the engine's `\warning` says so.
- Theta is not tested: the tree's depends on `blackScholesTheta`, the FD engine's differs
  slightly between modes through the snapshot time.
