<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling the BonusClassicOption Finite-Difference Engine

Where `FdBlackScholesBonusClassicEngine` spends its instructions under
`rke_profile_bonusclassicoption --engine fd`, and which changes, in this repository or in
QuantLib, would reduce them. Nothing here is implemented. The Monte Carlo engine has its own
profile, [`profiling-bonusclassicoption-mc.md`](profiling-bonusclassicoption-mc.md); two of the
QuantLib changes below are the time slices it already proposes.

## 1. What Was Measured

| Item     | Value                                                                                                                                                                                                                        |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Revision | `9ab84d9`, QuantLib submodule at `966a4cc10`                                                                                                                                                                                 |
| Build    | `profile` preset: `-O3 -g -fno-omit-frame-pointer`, Homebrew clang 23.1.3, C++17, QuantLib linked statically                                                                                                                 |
| Machine  | WSL2 laptop, no hardware counters                                                                                                                                                                                            |
| Tools    | valgrind 3.27.1 (callgrind, DHAT), google-benchmark from the submodule                                                                                                                                                       |
| Runs     | `rke_profile_bonusclassicoption {discrete,continuous} 1 --engine fd --market {flat,smile-bilinear,smile-bicubic}`, one pricing each, since callgrind counts exactly. With the probabilities on, a scratch harness, section 9 |

The workload is `makeBonusClassicOptionSetup` in `src/rke/common/BonusClassicOptionSetup.cpp`
with `Engine::FiniteDifference`:

- **Option:** barrier 70, bonus level 120, maturity 22 Jun 2026, evaluation date 22 Jun 2025.
- **Markets:** `flat` is a `BlackScholesMertonProcess` at spot 100 on `FlatForward` curves,
  risk-free rate 1% and dividend yield 3%, continuously compounded, with `BlackConstantVol` at
  20%. The smiles are the `ZeroCurve`s and the `BlackVarianceSurface` of "The Smile Market" in
  the Monte Carlo profile, bilinear or bicubic. `Actual360` and `NullCalendar` throughout.
- **Engine:** `fdTimeGrid` 200 time steps by `fdSpaceGrid` 400 nodes,
  `FdmSchemeDesc::TrBDF2()`, no damping steps. Discrete monitoring on the Monte Carlo grid: 255
  steps from 252 per year over the Actual360 year fraction 365/360, so 254 monitoring times
  strictly between 0 and `T`. `localVol = true` on the smiles, `false` on `flat`.

These prices are the baseline every candidate below is checked against. The probabilities come from
the harness, which prices the same double with them on:

| Market           | Discrete NPV         | Continuous NPV       | Discrete hit, bonus                        |
| ---------------- | -------------------- | -------------------- | ------------------------------------------ |
| `flat`           | `115.49216768219017` | `115.18467783448349` | `0.10067819529402655, 0.76511902713673263` |
| `smile-bilinear` | `112.88289152083952` | `112.83008499918665` | `0.14559930378592043, 0.71513649517176647` |
| `smile-bicubic`  | `113.36439170760717` | `112.99355875330676` | `0.13374794886472458, 0.72854248579196523` |

Continuous `flat` reports hit `0.1078037328826649` and bonus `0.75802360066539842`.

Instruction counts (Ir) of `FdBlackScholesBonusClassicEngine::calculate()`, one pricing, are
the evidence, and every share below is of that count. Wall-clock times are secondary, section 7.

## 2. Where the Instructions Go

| Market, mode         |    Pricing | Steps | Rollback per step |          `setTime` |     `localVolImpl` |
| -------------------- | ---------: | ----: | ----------------: | -----------------: | -----------------: |
| `flat`, discrete     |    80.44 M |   455 |           158.7 K |    27.97 M (34.8%) |                  – |
| `flat`, continuous   |    35.79 M |   201 |           159.2 K |    12.68 M (35.4%) |                  – |
| bilinear, discrete   |   630.11 M |   455 |         1,366.3 K |   577.37 M (91.6%) |   532.66 M (84.5%) |
| bilinear, continuous |   278.69 M |   201 |         1,367.5 K |   255.63 M (91.7%) |   235.65 M (84.6%) |
| bicubic, discrete    | 4,894.02 M |   455 |        10,737.5 K | 4,841.29 M (98.9%) | 4,796.82 M (98.0%) |
| bicubic, continuous  | 2,060.34 M |   201 |        10,231.3 K | 2,037.49 M (98.9%) | 2,017.89 M (97.9%) |

`setTime` is `FdmBlackScholesOp::setTime`, called once per TR-BDF2 step. The engine runs in two
regimes:

- **`localVol = false`**, a strike-independent volatility: `setTime` reads one Black variance
  per step, and the step's own linear algebra dominates, section 2.1.
- **`localVol = true`**, a smile: `setTime` asks `LocalVolSurface` for a volatility at each of
  the 400 nodes, 182,000 calls per discrete pricing and 80,400 per continuous one, and that is
  85–98% of the pricing, section 2.2.

The cost per step does not depend on the monitoring mode: 158.7 K against 159.2 K on `flat`,
1,366.3 K against 1,367.5 K on the bilinear smile. Discrete monitoring costs 2.25 times
continuous on `flat` because it takes 455 steps instead of 201, section 3, and builds a
`Concentrating1dMesher`, 4.14 M.

### 2.1 One Step on the Flat Market

Discrete, the rollback divided by its 455 steps:

| Part                                      | Ir per step | Contents                                                                                                                      |
| ----------------------------------------- | ----------: | ----------------------------------------------------------------------------------------------------------------------------- |
| `FdmBlackScholesOp::setTime`              |      61,473 | 43,690 for the `TripleBandLinearOp` that `dxxMap_.mult` constructs, 10,066 for `axpyb`, the rest rates, variance, temporaries |
| `TripleBandLinearOp::solve_splitting` × 3 |      38,549 | the Thomas algorithm, 12,850 per call: two in the Craig-Sneyd stage, one in the BDF2 stage                                    |
| `TripleBandLinearOp::apply` × 3           |      33,099 | 11,033 per call, all in the Craig-Sneyd stage                                                                                 |
| rest                                      |      25,560 | `Array` temporaries, boundary conditions, the knock-out                                                                       |
| all                                       |     158,681 |                                                                                                                               |

### 2.2 One Local Volatility

Per `LocalVolSurface::localVolImpl` call, discrete:

| Callee              | Bilinear | Bicubic |
| ------------------- | -------: | ------: |
| `localVolImpl`      |    2,927 |  26,356 |
| `blackVariance` × 5 |    1,031 |  24,460 |
| `discount` × 6      |   ≈1,090 |  ≈1,090 |

The six `discount` calls and the forward value depend on `t` only, the same for all 400 nodes
of a step. On the bicubic surface `BicubicSplineImpl` takes 4,851 Ir per lookup and 90.2% of the
pricing.

### 2.3 The Probabilities

`calculateProbabilities = true` adds two rollbacks, each with its own mesher, operator and
solver:

| Market, mode       | Price only | With probabilities | Ratio |
| ------------------ | ---------: | -----------------: | ----: |
| `flat`, discrete   |    80.44 M |           240.63 M |  2.99 |
| `flat`, continuous |    35.79 M |           106.22 M |  2.97 |
| bilinear, discrete |   630.11 M |         1,888.62 M |  3.00 |
| bicubic, discrete  | 4,894.02 M |        14,221.27 M |  2.91 |

The three rollbacks share nothing. On the bicubic surface `BicubicSplineImpl` counts 12,783.75 M,
3.5% less than three times the price-only run; the cause was not investigated.

## 3. Why Discrete Monitoring Takes 455 Steps

`FiniteDifferenceModel::rollbackImpl` steps `dt = T / tGrid` and splits a step at every stopping
time inside it. The stopping times are the 254 monitoring times and the theta snapshot that
`Fdm1DimSolver` adds at `0.99 · min(1/365, first stopping time)`. With 200 steps against 254
monitoring times most steps hold one or two, and 200 steps become 455, each a full TR-BDF2 step
whatever its length.

`fdsteps.py` replays that loop in IEEE doubles and reproduces callgrind's 455 at `tGrid` 200:

| `tGrid` | Steps | Below 1e-10 |
| ------: | ----: | ----------: |
|     200 |   455 |           4 |
|     255 |   505 |         249 |
|     510 |   762 |         251 |
|     765 | 1,020 |         254 |
|   1,020 | 1,274 |         253 |

A multiple of the monitoring step count makes it worse. `TimeGrid(T, 255)` puts the monitoring
times at `i · (T / 255)`, the rollback reaches its step ends by repeated `t -= dt`, and the two
disagree by an ulp: each disagreement costs a TR-BDF2 step about 1e-16 long. At `tGrid` 200, 4
of the 455 steps are already of that kind.

## 4. Candidates

Grouped by where they pay: in every regime, under a smile only, and mostly on a
strike-independent volatility. Saved Ir per discrete pricing, the price alone except for 4.2:

| #   | Candidate                                        | Lives in                     |         `flat` |          bilinear |             bicubic | Locks              |
| --- | ------------------------------------------------ | ---------------------------- | -------------: | ----------------: | ------------------: | ------------------ |
| 4.1 | Roll back one monitoring interval at a time      | this repository              | 31.6 M (39.3%) |   271.9 M (43.1%) |   2,136.8 M (43.7%) | discrete ones move |
| 4.2 | One operator for the three probability rollbacks | this repository              | 64.5 M (26.8%) | 1,163.0 M (61.6%) |   9,384.8 M (66.0%) | unchanged          |
| 4.3 | A time slice for the variance surface            | QuantLib                     |              – | ≤ 187.6 M (29.8%) | ≤ 4,414.4 M (90.2%) | unchanged          |
| 4.4 | A time slice for the local volatility            | QuantLib                     |              – |   ≈ 198 M (31.4%) |      ≈ 198 M (4.0%) | unchanged          |
| 4.5 | Set the operator in place                        | QuantLib                     | 19.9 M (24.7%) |     20.2 M (3.2%) |       20.0 M (0.4%) | unchanged          |
| 4.6 | No Craig-Sneyd corrector in one dimension        | QuantLib, or this repository | 14.1 M (17.5%) |     14.1 M (2.2%) |       14.1 M (0.3%) | unchanged          |
| 4.7 | Closed-form cell averages                        | this repository              |   3.2 M (4.0%) |      3.2 M (0.5%) |        3.2 M (0.1%) | all move, slightly |
| 4.8 | Two small QuantLib fixes                         | QuantLib                     | ≤ 7.4 M (9.2%) |    ≤ 7.4 M (1.2%) |      ≤ 7.4 M (0.2%) | unchanged          |

4.2's row is with the probabilities on. "Unchanged" is by construction and still has to be
confirmed by a prototype; none was built. The savings do not simply add: 4.1 removes steps, and
every per-step candidate then saves its share of fewer of them.

### 4.1 Roll Back One Monitoring Interval at a Time

- **Status:** estimated, as 199 fewer steps at the measured cost per step; no prototype.
- **Lives in:** this repository. The engine rolls back interval by interval and applies the
  knock-out between the calls, so the monitoring times leave the stopping times and only the
  theta snapshot stays. "Changes to QuantLib" in
  [`fd-and-tree-engines-bonusclassicoption.md`](fd-and-tree-engines-bonusclassicoption.md)
  sketches this as an `FdmBonusClassicSolver : Fdm1DimSolver`, which needs `Fdm1DimSolver`'s
  members `protected`. Without that change the engine builds `FdmBlackScholesOp` itself and
  repeats `Fdm1DimSolver`'s spline and theta snapshot.
- **Finding:** section 3. With `max(1, round(tGrid · Δt_k / T))` steps on interval `k`,
  `tGrid` 200 gives 255 steps, 256 with the theta snapshot's split, instead of 455.
- **Evidence:** 199 steps at 158.7 K, 1,366.3 K and 10,737.5 K Ir each: 31.6 M (39.3%) on
  `flat`, 271.9 M (43.1%) bilinear and 2,136.8 M (43.7%) bicubic per discrete pricing.
  Continuous monitoring has no monitoring times and gains nothing.
- **Lock impact:** every discretely monitored FD lock moves, price, probabilities and smiles.
  The rollback trades 455 uneven steps for 255 even ones, so its time-discretisation error
  moves too: the replication against the Broadie-Glasserman-Kou reference and the comparison
  with `MCBonusClassicEngine` have to be re-measured, not only the locks re-derived.
- **Decision needed:** what `tGrid` means. Below the number of monitoring intervals every
  interval takes one step and `tGrid` changes nothing; above it, `tGrid` is spread over the
  intervals.
- **Not the way:** a `tGrid` that is a multiple of the monitoring step count, section 3.
  Snapping a stopping time within an ulp of a step end in `FiniteDifferenceModel` would make
  such a multiple work, but changes every QuantLib engine with stopping times and does nothing
  at `tGrid` 200.

### 4.2 One Operator for the Three Probability Rollbacks

- **Status:** derived from the probabilities-on runs; no prototype.
- **Lives in:** this repository.
- **Finding:** the three rollbacks build three meshers from the same arguments and three
  operators from the same process, and run the same step sequence: `Fdm1DimSolver` adds its
  theta snapshot to all three. Two of the three meshers and two of the three `setTime` calls
  per step repeat the first to the bit. The boundary conditions leave the operator alone:
  `FdmDirichletBoundary::applyBeforeApplying` is empty.
- **Change:** build the mesher once, and step the three arrays in lockstep through three
  schemes on one operator, behind a wrapper whose `setTime` skips a repeated `(t1, t2)`.
  `FiniteDifferenceModel` runs one array to the end, so this needs the engine's own step loop,
  the one 4.1 introduces.
- **Evidence:** two thirds of `setTime` and of `Concentrating1dMesher` in the probabilities-on
  runs: 64.5 M of 240.63 M (26.8%) on `flat`, 1,163.0 M of 1,888.62 M (61.6%) bilinear,
  9,384.8 M of 14,221.27 M (66.0%) bicubic.
- **Lock impact:** unchanged; every array sees the coefficients it is stepped with today.

### 4.3 A Time Slice for the Variance Surface

- **Status:** unmeasured here. The same change as "A Time Slice for the Black Variance
  Surface" in the Monte Carlo profile.
- **Lives in:** QuantLib, `BlackVarianceSurface` and its interpolations, reached through 4.4's
  slice.
- **Finding:** `LocalVolSurface` asks the surface for three strikes at `t` and one each at
  `t ± dt`, for each of the 400 nodes: 2,000 lookups per step at three distinct times.
  `BicubicSplineImpl::value` builds a `CubicInterpolation` across strikes on every lookup,
  although for a fixed time it is the same spline; `BilinearInterpolationImpl::value` repeats
  the time `locate`.
- **Evidence:** the surface's share is the ceiling: 4,414.40 M (90.2%) bicubic, 4,851 Ir per
  lookup, of which a slice leaves one spline evaluation; 187.55 M (29.8%) bilinear, of which it
  saves only the time `locate` and weights.
- **Alternative:** a cache of the last three times inside `BicubicSplineImpl`, transparent to
  every caller, at the price of mutable state behind a `const` method, which makes concurrent
  reads of one surface unsafe.
- **Lock impact:** bicubic bit-identical without FMA contraction, the same spline from the same
  section; bilinear only if it keeps the four corner values and the formula. Only the smile
  locks run through it.

### 4.4 A Time Slice for the Local Volatility

- **Status:** unmeasured here. The same change as "A Time Slice for the Local Volatility" in the
  Monte Carlo profile, plus `FdmBlackScholesOp::setTime` taking one slice per step.
- **Lives in:** QuantLib, `LocalVolTermStructure`, `LocalVolSurface` and `FdmBlackScholesOp`.
- **Finding:** each `localVolImpl` call computes `dr`, `dq`, the forward value and four more
  discounts at `t ± dt`, all of them the same for the 400 nodes of a step.
- **Evidence:** `discount` takes 199.82 M per discrete pricing on either surface, 31.7%
  bilinear and 4.1% bicubic, all but about 1.5 M of it inside `localVolImpl`. A slice keeps six
  calls per step, about 0.5 M, which leaves about 198 M saved.
- **Lock impact:** bit-identical without FMA contraction, as in the Monte Carlo profile.

### 4.5 Set the Operator in Place

- **Status:** unmeasured; the figure is the cost the change removes.
- **Lives in:** QuantLib, `FdmBlackScholesOp::setTime` and `TripleBandLinearOp`.
- **Finding:** `setTime` assembles `mapT_` as
  `mapT_.axpyb(drift, dxMap_, dxxMap_.mult(0.5 * v), Array(1, -r))`. `mult` returns a new
  `TripleBandLinearOp` from the `(direction, mesher)` constructor, which allocates six arrays
  and recomputes every node's neighbour indices through `FdmLinearOpLayout::neighbourhood`,
  although they never change.
- **Change:** an `axpyb` overload that takes `y`'s scale as an array and writes `mapT_` in
  place, keeping each coefficient's operations in their order, `y_diag[i] = dxx_diag[i] · s[i]`
  and then `diag[i] = y_diag[i] + a[i] · x_diag[i] + b`; or a `mult` built on the copy
  constructor, which copies the indices instead of recomputing them.
- **Evidence:** the constructor costs 43,690 Ir and 13 allocations per call, 455 calls per
  discrete pricing: 19.9 M (24.7%) on `flat`, 20.2 M (3.2%) bilinear. It accounts for 5,954 of
  the `flat` pricing's 35,646 allocations and 10.3 MB of its 29.6 MB.
- **Lock impact:** bit-identical without FMA contraction. With it, one fused loop may fuse a
  different product than two loops do, a last-bit difference per coefficient; to be confirmed
  on a contracting build.
- **Upstream:** high. Fourteen more operator files under
  `ql/methods/finitedifferences/operators/` call `mult` or `multR`; how many of them do so per
  step was not checked.

### 4.6 No Craig-Sneyd Corrector in One Dimension

- **Status:** unmeasured; the figure is callgrind's line count for the code it removes.
- **Lives in:** QuantLib, `CraigSneydScheme::step` or `FdmBackwardSolver::rollback`. Or this
  repository, once the engine runs its own step loop, 4.1, with `TrBDF2Scheme<DouglasScheme>`.
- **Finding:** `FdmBackwardSolver` hard-wires TR-BDF2's trapezoidal stage to
  `CraigSneydScheme`. In one dimension `apply_mixed` returns zeros, so the corrector
  `y0 + μ · dt · 0` is the predictor `y0`, and the second sweep solves the same system with the
  same right-hand side again: one `apply_direction`, one Thomas solve, a zero array and three
  `Array` operations per step for a result already in hand. `DouglasScheme` at θ = 0.5 runs
  exactly the first sweep.
- **Change:** return after the first sweep when `map_->size() == 1`, or have
  `FdmBackwardSolver` pick `DouglasScheme` for a one-dimensional operator.
- **Evidence:** 14.07 M per discrete pricing for the corrector and the second sweep: 17.5% on
  `flat`, 2.2% bilinear, 0.3% bicubic.
- **Lock impact:** bit-identical by construction: `y0 + 0.0` is `y0`, and the second sweep
  repeats the first operation by operation.
- **Upstream:** high. Every one-dimensional engine on TR-BDF2 pays it.

### 4.7 Closed-Form Cell Averages

- **Status:** unmeasured; the figure is the cost the change removes.
- **Lives in:** this repository, an `FdmInnerValueCalculator` for the certificate and the two
  indicators.
- **Finding:** `FdmLogInnerValue` averages the payoff over each cell with `SimpsonIntegral`.
  In `S = e^x` the certificate at maturity is `S` at or below `H`, `B` between `H` and `B`, and
  `S` from `B` up; the indicators are constants. Each cell average has a closed form, the
  average of `e^x` over `[a, b]` being `(e^b − e^a) / (b − a)`.
- **Evidence:** 3.18 M per pricing: 4.0% `flat` discrete, 9.0% `flat` continuous, 0.5%
  bilinear; three times that with the probabilities on.
- **Lock impact:** every lock moves by the quadrature error it removes, unmeasured. It also
  makes exact the barrier node's share below `H`, which the `IndicatorPayoff` comment says
  holds up to that error.

### 4.8 Two Small QuantLib Fixes

- **`TripleBandLinearOp::apply`'s loop bound:** the loop tests `i < mesher_->layout()->size()`
  on every element, which the compiler cannot hoist past the stores into the result. Callgrind
  attributes 3.29 M of `apply`'s 15.06 M to that pointer chain, 6 Ir per element: 4.1% on
  `flat`. `axpyb` and `solve_splitting` already hoist it. Bit-identical.
- **The concentrating mesher's ODE without vectors:** `Concentrating1dMesher` integrates with
  `AdaptiveRungeKutta`'s one-dimensional overload, which wraps the scalar function in
  `std::vector`s: 22,201 of the `flat` discrete pricing's 35,646 allocations. The mesher costs
  4.14 M (5.1%), discrete only, and a scalar path saves an unmeasured part of it. Bit-identical
  if the arithmetic stays scalar for scalar.

## 5. Findings Dropped

| Finding                                                             | Measured here                                          | Why it is dropped                                                                                                                                                   |
| ------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Reuse the tridiagonal factorisation across steps                    | at most `solve_splitting`'s 17.54 M (21.8%), `flat`    | Needs the same operator and step size on every step: flat curves, a volatility constant in time, and 4.1's even steps. A zero curve or a smile changes it per step. |
| A `tGrid` that is a multiple of the monitoring step count           | 505 steps at 255, against 455 at 200                   | Section 3.                                                                                                                                                          |
| A binary search in `FdmBonusClassicKnockOutCondition::applyTo`      | `applyTo` 0.76 M (0.9%), its `discount` calls included | Too small.                                                                                                                                                          |
| Drop the theta snapshot's extra step and spline                     | 1 of 455 steps (0.2%); both splines 0.11 M             | Too small, and `thetaAt` needs them.                                                                                                                                |
| Cache the mesher or the local variances across `calculate()`        | –                                                      | Pays only on an unchanged market, which is the benchmark loop; `FdmBlackScholesMesher`'s range moves with the spot.                                                 |
| Skip the `FdmBlackScholesMesher` discrete mode builds for its range | 0.28 M (0.3%) with the rest of `makeSolver`            | Too small.                                                                                                                                                          |

## 6. What the Profile Rules Out

- **Allocation:** one `flat` discrete pricing allocates 35,646 blocks and 29.6 MB (DHAT,
  2-iteration minus 1-iteration run), peak heap 267 KB. 22,201 blocks are the mesher's ODE,
  4.8, and 12,779 the operator's `TripleBandLinearOp`s and the rollback's `Array`s, 4.5 and 4.6. `malloc`,
  `free` and `operator new` take 2.83 M Ir (3.5%) themselves. Allocation falls with 4.5, 4.6
  and 4.8 and is not a candidate of its own.
- **Greeks:** `valueAt`, `deltaAt`, `gammaAt` and `thetaAt` read the solver's two splines,
  0.11 M together.
- **Monitoring mode:** it changes the number of steps, nothing per step, section 2.
- **Not covered:** cachegrind and `perf` were not run.

## 7. Wall Clock (Secondary)

`rke_benchmark_bonusclassicoption --benchmark_filter='^BonusClassicOptionFd'
--benchmark_repetitions=5` from the `release` preset, QuantLib static, one session with nothing
else running. Mean per pricing, with its cv:

| Benchmark                                     |    Mean |    cv |
| --------------------------------------------- | ------: | ----: |
| `BonusClassicOptionFd`                        | 8.63 ms | 1.00% |
| `BonusClassicOptionFdContinuous`              | 3.77 ms | 1.51% |
| `BonusClassicOptionFdSmileBilinear`           | 40.2 ms | 1.08% |
| `BonusClassicOptionFdSmileBilinearContinuous` | 17.6 ms | 0.78% |
| `BonusClassicOptionFdSmileBicubic`            |  343 ms | 2.47% |
| `BonusClassicOptionFdSmileBicubicContinuous`  |  148 ms | 1.52% |

- **Mode ratio:** discrete over continuous is 2.29, 2.28 and 2.32 in time against 2.25, 2.26
  and 2.38 in Ir, which come from the `profile` build.
- **Throughput:** the `flat` pricings retire about 9.3–9.5 G Ir per second, the smiles
  13.9–15.8 G. Without hardware counters the reason stays open.

## 8. Next Step

1. 4.5 and 4.6 on a `ralfkonrad/QuantLib` branch, as `.agents/changing-quantlib.md` describes:
   bit-identical by construction, small diffs, and one callgrind pair on `flat` measures each.
2. 4.3 and 4.4 together with the Monte Carlo profile's two time slices: one QuantLib branch
   serves both engines, measured with `--market smile-bilinear` and `--market smile-bicubic`.
3. 4.1 in this repository once `tGrid`'s meaning is decided, then 4.2 on its step loop.

## 9. Raw Profiles

These are local and untracked. The recipes that produce them are in `.agents/profiling.md`.
Under `build/profile/prof/rke_profile_bonusclassicoption/fd/` of the tree that measured them,
the `master` worktree:

- `callgrind-{flat,smile-bilinear,smile-bicubic}-{discrete,continuous}.out`, each with
  `-inclusive.txt`, `-self.txt` and the unthresholded `-inclusive-all.txt`
- `callgrind-flat-discrete-inclusive-annotated.txt`, source-annotated, which gives 4.6's line
  counts
- `callgrind-probabilities-{flat-discrete,flat-continuous,smile-bilinear-discrete,smile-bicubic-discrete}.out`,
  each with `-inclusive-all.txt` and the harness's output in `.log`
- `probharness.cpp`, the harness: `makeBonusClassicOptionSetup` with the probabilities on and
  one `reprice()`, compiled by hand with the `profile` flags against that tree's
  `librke_common.a`, `librke_ql_ext.a` and `libQuantLib.a`
- `dhat-flat-discrete-{1,2}.json`, the allocation runs of section 6
- `fdsteps.py`, section 3's replay, and `byfn_max.py`, which takes each function's largest
  inclusive row from `callgrind_annotate`
- `benchmark-fd-release.txt`, section 7
