# Extending `rke/ql/ext`

Read this when adding an instrument, payoff, pricing engine, or test under
`rke/`. Every recipe ends the same way: list the new files in the owning
`CMakeLists.txt`, and give the new number a reference.

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

Under `rke/ql/ext/instruments/`, derived from the fitting QuantLib base
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

Under `rke/ql/ext/pricingengines/<instrument>/`. A Monte Carlo engine derives
from both the instrument's `engine` and
`QuantLib::McSimulation<SingleVariate, RNG, S>`, and:

- `registerWith(process_)` in the constructor. Without it the instrument caches
  its first price and never recomputes — see "Lazy Evaluation" in
  [`AGENTS.md`](../AGENTS.md).
- `QL_REQUIRE` in the constructor for every combination the engine does not
  implement, rather than silently pricing something else. The existing engine
  rejects `isBiased == false` that way.
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

## 4. Registering the Files

`rke/ql/ext/CMakeLists.txt` keeps two lists, `RKE_QL_EXT_SOURCES` and
`RKE_QL_EXT_HEADER`. Add each new file to the right one, keeping the paths
alphabetical. Nothing is globbed, and an unlisted header compiles anyway, so the
mistake is invisible until someone looks for the file in an IDE.

## 5. Tests

Add the `.cpp` to `rke/testsuite/CMakeLists.txt` and follow
`rke/testsuite/BonusClassicOption.cpp`:

- `BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)` on the outside,
  a `BOOST_AUTO_TEST_SUITE` per instrument inside, cases within that.
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

The tolerance of the second kind needs a comment saying where it comes from. The
existing one records a measured residual of 3.8e-4 relative against a `1e-3`
bound, and why the remainder is model error rather than Monte Carlo noise. "Close
enough" is not a tolerance.
