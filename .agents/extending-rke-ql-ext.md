<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Extending `src/rke/ql/ext`

Read this when adding or documenting an instrument, payoff, pricing engine, or
test under `src/rke/`. Every recipe ends the same way: list the new files in the
owning `CMakeLists.txt`, document the header, and give the new number a
reference.

`BonusClassicOption` and `MCBonusClassicEngine` are the worked example for all of
it; copy their shape rather than inventing a second one.

## 1. A New Payoff

Derive from the QuantLib payoff that already carries the data you need —
`BonusClassicPayoff` derives from `StrikedTypePayoff` and reuses `strike()` as
its barrier rather than storing it twice. Then:

1. `name()` returning the payoff's own string.
2. `operator()(Real price)` for the terminal payoff.
3. `accept(AcyclicVisitor&)`: `dynamic_cast` to `Visitor<YourPayoff>*`, dispatch
   if it matches, otherwise delegate to the base. Skipping the delegation
   silently breaks every existing visitor.

## 2. A New Instrument

Under `src/rke/ql/ext/instruments/`, derived from the fitting QuantLib base
(`OneAssetOption` for the existing one):

1. Forward-declare the nested `class arguments;` and `class engine;` in the
   instrument.
2. `setupArguments()`: call the base, `dynamic_cast` the pointer to your
   `arguments`, `QL_REQUIRE` it, then copy your fields across.
3. `arguments`: initialise every field to `Null<Real>()` in the constructor, and
   in `validate()` call the base and `QL_REQUIRE` each field against its `Null`.
   That sentinel pair is what turns a forgotten field into an error message
   instead of a silent zero.
4. `engine`: `GenericEngine<arguments, results>`, holding whatever predicates the
   engines share (`triggered()` in the example).

## 3. A New Pricing Engine

Under `src/rke/ql/ext/pricingengines/<instrument>/`. A Monte Carlo engine derives
from both the instrument's `engine` and `QuantLib::McSimulation<MC, RNG, S>`, and
exposes `MC` as a template-template parameter so the caller picks the path
generator. `CachedStepSingleVariate`, the default, builds paths with
`CachedStepPathGenerator`, which caches the exact step of a
`GeneralizedBlackScholesProcess` per grid and refuses a process whose step is not
exact, at construction and never by falling back. `QuantLib::SingleVariate` builds
them with `QuantLib::PathGenerator` for every other process. Both produce the same
doubles. Then:

- `registerWith(process_)` in the constructor. Without it the instrument caches
  its first price and never recomputes — see "Lazy Evaluation" in
  [`AGENTS.md`](../AGENTS.md).
- `QL_REQUIRE` in the constructor for every combination the engine does not
  implement, rather than silently pricing something else. Where a flag selects
  between implemented variants, `pathPricer()` picks the pricer from it, as the
  existing engine does from `isBiased`, following `MCBarrierEngine`.
- `calculate()`: validate the state (`spot > 0.0`, barrier not already
  triggered), delegate to `McSimulation::calculate()` with the tolerance, sample
  count and sample cap, then set `results_.value` from the sample accumulator,
  and `results_.errorEstimate` only under
  `if constexpr (RNG::allowsErrorEstimate)`.
- `timeGrid()`: `QL_FAIL` on an unusable configuration instead of falling back to
  a default. The grid is what defines barrier monitoring, so an implicit grid is
  an implicit product.
- `pathPricer()`: `dynamic_pointer_cast` the payoff out of `arguments_` and
  `QL_REQUIRE` it.

A template engine is header-only, and still has to be listed — in
`RKE_QL_EXT_HEADER`.

A finite-difference engine derives from the instrument's `engine` alone and, in
`calculate()`, assembles mesher, boundary set, step conditions and inner-value
calculator into an `FdmSolverDesc` for `FdmBlackScholesSolver`, as
`FdBlackScholesBonusClassicEngine` does after QuantLib's
`FdBlackScholesBarrierEngine`. It registers with its process like any engine, and:

- Reads `valueAt()` before `thetaAt()`. The other accessors run the rollback;
  `thetaAt()` does not, and dereferences a null solver if called first.
- Gates a step condition on its own times, compared exactly. The model calls
  `applyTo()` after every time step, and at a stopping time passes the stored
  double.
- Puts a node on every jump of the payoff or the knock-out, and gives that node
  its cell's average: a node taking one side's value moves the barrier by half a
  cell, a first-order error. `FdmLogInnerValue` averages at maturity.
- Checks that a node meant to sit on a level maps back to the right side of it:
  `std::exp(std::log(H))` exceeds `H` for about a third of all levels.
- Expects a knock-out on monitoring dates to make Crank-Nicolson, the default
  `Douglas` scheme, converge erratically in time; `FdmSchemeDesc::TrBDF2()`
  converges at second order there.
- Exposes monitoring times as `timeGrid()`, as the Monte Carlo engine does: they
  define the product.

A tree engine pairs a `QuantLib::DiscretizedAsset` subclass with an engine that
derives from the instrument's `engine`, as `DiscretizedBonusClassicOption` and
`BinomialBonusClassicEngine` do after QuantLib's `BinomialBarrierEngine`. Then:

- The asset implements `reset` (zero-fill, then `adjustValues()`),
  `postAdjustValuesImpl` and `mandatoryTimes`. The lattice adjusts it after every
  step of a rollback, so a barrier test there monitors every lattice step,
  `t = 0` included.
- The engine flattens rate, yield and volatility itself and hands the flattened
  yield to the asset, so that an asset leg agrees with the tree.
- Boyle-Lau applies to `CoxRossRubinstein` and derived trees only. Keep the
  candidate count in `Real` until it is capped: casting it to `Size` first, as
  QuantLib does, is undefined once it leaves `Size`'s range.
- `LeisenReimer` and `Joshi4` build `steps + 1` for an even count. Round up to
  odd before the tree, the lattice and its `TimeGrid` are built, or their `dt`
  differ.
- Delta and gamma take the `Null<Real>()` guards of `BinomialVanillaEngine`.
- `timeGrid()` reports the lattice grid, after Boyle-Lau and rounding: its steps
  are the monitoring times.

## 4. Registering the Files

`src/rke/ql/ext/CMakeLists.txt` keeps two lists, `RKE_QL_EXT_SOURCES` and
`RKE_QL_EXT_HEADER`. Add each new file to the right one, keeping the paths
alphabetical. Nothing is globbed, and an unlisted header compiles anyway, so the
mistake is invisible until someone looks for the file in an IDE.

## 5. Tests

Add the `.cpp` to `src/rke/testsuite/CMakeLists.txt` and follow
`src/rke/testsuite/BonusClassicOption.cpp`:

- Everything inside `namespace RKE::TestSuite`, then
  `BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)`, a
  `BOOST_AUTO_TEST_SUITE` per instrument inside it, cases within that.
- The fixture already restores `Settings` and asserts that `IndexManager`
  histories are empty. Do not add a second `SavedSettings`; do clear any fixings
  the case sets.
- `BOOST_TEST_MESSAGE` as the first line of each case, matching the `-l message`
  the CTest entry passes.
- `flatRate()` and `flatVol()` come from `<test-suite/utilities.hpp>` — the
  QuantLib submodule's own test helper, already compiled into the target with
  `SKIP_LINTING`.
- State the conventions: the existing tests fix `Actual360`, a `NullCalendar`
  flat vol and `Date(22, Jun, 2025)`. A price without its day count and calendar
  is not checkable.

### Two Kinds of Price Test

New pricing code needs both, and they are not interchangeable:

1. **A regression lock.** A tight tolerance around the engine's own output, valid
   only because a low-discrepancy sequence is deterministic for a fixed seed,
   sample count and grid. Label it as a lock in a comment, as
   `testBonusClassicOptionValuation` does, so nobody later mistakes it for a
   validated price.
2. **An independent check.** Replication against an analytic engine, or a
   published reference. `testBonusClassicOptionReplication` decomposes the payoff
   into an asset leg plus a down-and-out put, prices the put with
   `AnalyticBarrierEngine`, and corrects that engine's continuous-monitoring
   assumption with the Broadie-Glasserman-Kou shift — Broadie, Glasserman and Kou
   (1997), _A continuity correction for discrete barrier options_, Mathematical
   Finance 7(4), 325-349.
   `testBonusClassicOptionContinuousReplication` checks the continuously
   monitored pricer the same way, at the barrier itself: both sides monitor
   continuously, so no shift applies.

The tolerance of the second kind needs a comment saying where it comes from. The
existing one records a measured residual of 3.8e-4 relative against a `1e-3`
bound, and why the remainder is model error rather than Monte Carlo noise. "Close
enough" is not a tolerance.

## 6. Documentation Comments

Headers carry QuantLib's Doxygen markup, so a reader moving between this tree and
`external/QuantLib` sees one style. There is no Doxyfile: the comments are for
people reading the header, and that is the bar they have to clear. Copy the shape
of the headers in `src/rke/ql/ext/` and of their QuantLib counterparts
(`barrieroption.hpp`, `mcbarrierengine.hpp`).

- **Where.** Doc comments go in the header, above the declaration. `.cpp` files
  and function bodies keep plain `//` comments that say _why_ the code does
  something; leave those in place when adding docs.
- **Class.** A `//!` one-line brief, then a `/*! ... */` block covering:
  - the payoff or model, as a formula in `\f$ ... \f$` or `\f[ ... \f]`, with
    its edge cases (an inclusive barrier, say);
  - the conventions behind the price: the time measure and day counter, the
    discount curve, how often a barrier is monitored;
  - `\warning` for every limitation or unchecked input;
  - `\ingroup` with QuantLib's group (`instruments`, `barrierengines`, ...);
  - `\test` for each test that backs the class, naming its kind: a regression
    lock or an independent check, as under "Two Kinds of Price Test".
- **Members.** `//!` above an accessor whose meaning is not in its name. Where
  an argument has a non-obvious meaning, such as a `Null<>()` that switches
  behaviour, an ignored flag, or a unit, document it with a `\param` block
  above the constructor. `\pre` for each precondition a `QL_REQUIRE` enforces.
  Group overrides of a base interface under `//! \name ...` with `//@{` and
  `//@}`.
- **Macros.** `/*! \def NAME \brief ... */`.
- **Read the code, not the product.** Describe what the implementation does,
  from the payoff's `operator()`, the path pricer and the engine's `timeGrid()`.
  Where it departs from the market product (discrete instead of continuous
  monitoring, say), say so, not what a term sheet would say.
- **Keep it in step.** A change to an engine's behaviour updates its doc block
  in the same commit, just as it re-derives the regression lock.
- **Wrap by hand at about 80 columns.** clang-format reflows a comment line that
  passes 100 columns and leaves the rest of the paragraph ragged. Check the
  comment again after formatting.
