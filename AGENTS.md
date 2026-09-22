# AGENTS.md — AI Agent Guide for `ql_performance_testing`

> How AI coding agents should work in this repository.

A QuantLib playground: a small extension library (`rke/ql/ext`) with its own
Boost.Test suite, plus google-benchmark benchmarks that measure it and QuantLib
itself. QuantLib and google-benchmark are git submodules under `external/`.

## 1. Hard Constraints

- **`external/` is two git submodules and is off limits.** `external/QuantLib`
  tracks the `ralfkonrad/QuantLib` fork, `external/benchmark` tracks
  `google/benchmark`. Never edit a file there, never reformat one
  (`.clang-format-ignore` excludes `external/**`), and never move a submodule
  pointer as a side effect of another change. `CMakeLists.txt` sets
  `CMAKE_CXX_CLANG_TIDY` _after_ `add_subdirectory(external)` so that it cannot
  reach the submodules; keep that order.
- **Warnings are build failures, and a target opts in by name.**
  `cmake/WarningLevels.cmake` defines `rke_target_warnings(<target>)`, which links
  the `rke_warnings` interface library (`-Wall -Wextra -Wpedantic`, `-W4` under
  MSVC) and sets `COMPILE_WARNING_AS_ERROR` from `RKE_COMPILE_WARNING_AS_ERROR`,
  default `ON`. Call it for every target you add under `rke/` or `benchmark/`, or
  it is built unwarned. Never make the flags directory-scoped again: that is what
  used to put them one `add_subdirectory` away from the submodules.
- **C++17 is the baseline, not a floor to build on.** `CMakeLists.txt` defaults
  `CMAKE_CXX_STANDARD` to 17 and fails below it. CI compiles each platform at 17,
  20 and 23, so a post-C++17 construct passes two thirds of the matrix and fails
  the rest.
- **Every `.cpp` _and_ `.hpp` is listed by hand in CMake.** Nothing is globbed:
  `rke/ql/ext/CMakeLists.txt` keeps separate `RKE_QL_EXT_SOURCES` and
  `RKE_QL_EXT_HEADER` lists, the second reaching `rke_ql_ext` through
  `target_sources(... FILE_SET HEADERS)`, whose `BASE_DIRS` is also what puts the
  repository root on the include path. `rke/testsuite/CMakeLists.txt` and
  `benchmark/CMakeLists.txt` name every file. An unlisted header still compiles,
  so nothing will tell you it is missing.
- **An upstream source compiled into one of our targets must not be linted.**
  `rke/testsuite/CMakeLists.txt` pulls in
  `external/QuantLib/test-suite/utilities.cpp` and sets `SKIP_LINTING TRUE` on
  it. Any further upstream file added this way needs the same property.

## 2. Task Guides

Task-specific detail lives in [`.agents/`](.agents/README.md). Read one when the
task calls for it:

| Read this                                                                | When                                                                                                                  |
| ------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| [`.agents/build-and-test.md`](.agents/build-and-test.md)                 | You need build options, targets, single test invocations, local clang-tidy/clang-format runs, or the CI workflow map. |
| [`.agents/extending-rke-ql-ext.md`](.agents/extending-rke-ql-ext.md)     | You add an instrument, payoff, pricing engine, or test to `rke/`.                                                     |
| [`.agents/benchmarking.md`](.agents/benchmarking.md)                     | You add, change, or run a benchmark.                                                                                  |
| [`.agents/maintaining-agent-docs.md`](.agents/maintaining-agent-docs.md) | You edit this file or anything in `.agents/`.                                                                         |

## 3. Coding Conventions

### 3.1 Formatting and Includes

Source of truth: `.clang-format`.

- 4-space indent, 100-column limit, namespace indentation on, `T* p` and `T& x`,
  at most two consecutive blank lines.
- One merged, sorted include block in the order `"local"` → `<rke/...>` →
  `<ql/...>` → `<boost/...>` → standard headers.
- Formatting is **not** checked on pull requests — the workflow that applies
  clang-format is dispatch-only. Run it yourself; CI pins clang-format 20, so a
  newer local binary may still reformat more than CI would.

### 3.2 Naming and Namespaces

- Our code lives in `namespace RKE::QL::External`. The two older `ql_*`
  benchmark translation units are at global scope; leave them there.
- Headers qualify `QuantLib::` in full. Implementation files put
  `using namespace QuantLib;` after the includes — which is why
  `google-build-using-namespace` is off in `.clang-tidy`.
- Types `PascalCase`, functions `lowerCamelCase`, data members with a trailing
  underscore, benchmark entry points `BM_PascalCase`.
- File names in `rke/` and `benchmark/Benchmark*` are `PascalCase`, the
  directories under them lowercase. Include guards are the bare file name
  uppercased (`BONUSCLASSICOPTION_HPP`), not a path; `llvm-header-guard` is off
  for that reason.
- `[[nodiscard]]` on value accessors.

### 3.3 QuantLib Idioms

- Ownership through `QuantLib::ext::shared_ptr`, `ext::make_shared`,
  `ext::dynamic_pointer_cast`, not the `std::` spellings.
- Errors through `QL_REQUIRE`, `QL_ENSURE`, `QL_FAIL`, `QL_ASSERT`, never a raw
  `throw`. `NOT_IMPLEMENTED_FAILURE()` from `rke/ql/ext/Error.hpp` for an
  override that is not implemented.
- `Null<Real>()` and friends are the "not given" sentinel, checked in
  `arguments::validate()`.

### 3.4 clang-tidy

`.clang-tidy` enables `*` and then disables a short, explicit list. A check that
fires and is not on that list is a real finding: fix it, or silence it with a
targeted `// NOLINT(<check>)` carrying a reason. Adding a check to the disabled
list is its own commit. Every entry in that list needs a trailing comma — the
file says so, because clang-tidy splits on commas.

## 4. Build and Test — Quick Start

```bash
git submodule update --init --recursive
cmake --workflow release      # configure, build and test in one step
```

The three steps separately, when you want only one of them:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The test presets filter on `^rke_`, so that runs the extension tests only.
QuantLib's own suite is still built (`QL_BUILD_TEST_SUITE=ON`) and still
reachable with `ctest --preset release -R quantlib` — a command-line `-R`
overrides the preset's filter. Options, targets, single test cases and the CI map
are in
[`.agents/build-and-test.md`](.agents/build-and-test.md).

## 5. Pitfalls

### 5.1 The Valuation Test Is a Regression Lock, Not a Reference Price

`testBonusClassicOptionValuation` pins `MCBonusClassicEngine` to
`106.96041418042263` at `1e-8`. The low-discrepancy sequence is deterministic for
a fixed seed, sample count and time grid, so that number is the engine's own
output and nothing more. Any change to the engine, the grid or the sample count
moves it: re-derive it and say in the commit why it moved. Never widen the
tolerance or paste a fresh number to make the test green.

The externally checkable claim is `testBonusClassicOptionReplication` — asset leg
plus a down-and-out put priced by `AnalyticBarrierEngine`, with the
Broadie-Glasserman-Kou continuity correction for discrete monitoring, at `1e-3`.
New pricing code needs a test of that second kind as well, with a named reference
and a tolerance justified in a comment.

### 5.2 The Fixture Owns the Global State

`TestSuiteFixture` holds a `QuantLib::SavedSettings` and asserts on teardown that
`IndexManager` histories are empty. So no test case needs its own
`SavedSettings`, assigning `Settings::instance().evaluationDate()` inside a case
is fine, and a case that sets fixings must clear them itself. The assertion is on
the names `IndexManager` holds, not on the fixings under them, because that is all
its public interface offers — and `getHistory()` inserts an empty entry although it
is `const`, so a case that only reads a fixing has to clear it too.

### 5.3 Lazy Evaluation

`Instrument::NPV()` returns the result cached by the last
`performCalculations()` and re-runs the engine only after an observed dependency
notified a change. An engine must `registerWith()` everything its price depends
on — `MCBonusClassicEngine` registers with its process — and a value copied at
construction will never invalidate the cache. In a benchmark loop this bites at
once; see [`.agents/benchmarking.md`](.agents/benchmarking.md).

### 5.4 The google-benchmark Loop Trips the Static Analyzer

`for (auto _ : state)` reads as a dead store, so every such loop carries
`// NOLINT(clang-analyzer-deadcode.DeadStores)`. Keep it on new ones.

### 5.5 Quant Claims Need Their Conventions Spelled Out

Day count, calendar, business-day convention, compounding and the discounting
curve are never implied. The existing tests state them (`Actual360`, a
`NullCalendar` flat vol, an explicit evaluation date); a new one has to as well,
or the number it checks means nothing.

## 6. Validation Checklist

Before finishing a change:

- [ ] `cmake --build --preset release` is clean — warnings are errors.
- [ ] `ctest --preset release` passes.
- [ ] New behaviour has a test; new pricing code has both a regression lock and
      an independent check, as under "The Valuation Test Is a Regression Lock".
- [ ] Every new `.cpp` and `.hpp` is listed in the owning `CMakeLists.txt`, and
      every new target calls `rke_target_warnings`.
- [ ] New benchmarks are registered in `benchmark/benchmark_main.cpp`.
- [ ] Errors go through the `QL_*` macros, ownership through `ext::shared_ptr`.
- [ ] Numerical tolerances are justified, and the conventions behind a price are
      stated, as under "Quant Claims Need Their Conventions Spelled Out".
- [ ] `clang-format` has been run over the touched files.
- [ ] `git submodule status` shows no pointer moved unintentionally.
