# Build and Test

Companion to [`AGENTS.md`](../AGENTS.md), which carries only the four commands
needed most often. This file holds the options, targets, targeted test
invocations, lint runs, and the CI map.

## 1. Prerequisites

- The submodules: `git submodule update --init --recursive`. Nothing configures
  without them — `external/CMakeLists.txt` adds both as subdirectories.
- Boost, resolved by `find_package(Boost CONFIG REQUIRED)`. On Linux and macOS
  CI installs it with Homebrew; on Windows the setup action installs the
  SourceForge MSVC binaries and exports `Boost_DIR`.
- CMake 4.0 or newer — `CMakeLists.txt` sets that floor.
- Ninja — both presets use it as the generator.

## 2. Presets

`CMakePresets.json` defines exactly two usable presets, `debug` and `release`.
Both inherit a hidden `default` that sets

- `RKE_COMPILE_WARNING_AS_ERROR=ON`
- `QL_BUILD_TEST_SUITE=ON`
- `QL_BUILD_EXAMPLES=OFF`
- `QL_ENABLE_PARALLEL_UNIT_TEST_RUNNER=OFF`

and puts the binary tree in `build/<presetName>`. `build/` and
`CMakeUserPresets.json` are both git-ignored, so local overrides belong in the
latter rather than in `CMakePresets.json`.

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

`workflowPresets` chains the same three under one name, which is what to reach
for when you want all of them:

```bash
cmake --workflow release
```

Add `-DCMAKE_CXX_STANDARD=20` (or `23`) to the configure step to reproduce the
other CI legs. A workflow preset takes no such flag, so the standards other than
the default need the three steps separately.

Both presets set `CMAKE_EXPORT_COMPILE_COMMANDS`, so each binary dir carries the
`compile_commands.json` clangd wants.

## 3. Options

From `CMakeLists.txt`:

- `RKE_COMPILE_WARNING_AS_ERROR` — default `ON`. Reaches a target through
  `rke_target_warnings()`, not through a directory-scoped flag.
- `RKE_USE_CLANG_TIDY` — default `OFF`. Set at configure time; the
  `CMAKE_CXX_CLANG_TIDY` assignment sits after `add_subdirectory(external)`, so
  only our own targets are analysed.
- `RKE_CLANG_TIDY` — the binary, default `clang-tidy`.
- `RKE_CLANG_TIDY_OPTIONS` — extra arguments, e.g. `--fix`.
- `CMAKE_CXX_STANDARD` — default `17`, configure fails below it.
- `Boost_USE_STATIC_LIBS`, `Boost_USE_STATIC_RUNTIME` — default to `ON` under
  MSVC only.

QuantLib's own `QL_*` options are also available, since it is configured as a
subdirectory.

## 4. Targets

Paths are relative to the preset's binary dir, `build/release`, which mirrors the
source tree.

| Target                   | Kind                  | Lands at                             |
| ------------------------ | --------------------- | ------------------------------------ |
| `rke_ql_ext`             | static library        | `rke/ql/ext/librke_ql_ext.a`         |
| `rke_ql_ext_testsuite`   | Boost.Test executable | `rke/testsuite/rke_ql_ext_testsuite` |
| `ql_performance_testing` | google-benchmark exe  | `benchmark/ql_performance_testing`   |

`cmake --build --preset release` also builds QuantLib and QuantLib's own test
suite. To skip that, build one target: `--target rke_ql_ext_testsuite`.

## 5. Running Tests

CTest knows two tests, `quantlib_test_suite` and `rke_ql_ext_testsuite`. The
first is QuantLib's full suite and dominates the runtime, so the hidden
`default` test preset carries `filter.include.name` `^rke_` and both visible
presets inherit it:

```bash
# the extension tests only
ctest --preset release

# QuantLib's own suite: a command-line -R overrides the preset's filter
ctest --preset release -R quantlib
```

Boost.Test arguments go to the executable directly:

```bash
BIN=build/release/rke/testsuite/rke_ql_ext_testsuite

# everything, with the per-case messages the CTest entry also asks for
$BIN -l message

# one suite
$BIN -l message --run_test=RkeQLExtTestSuite/BonusClassicOptionTests

# one case
$BIN -l message \
    --run_test=RkeQLExtTestSuite/BonusClassicOptionTests/testBonusClassicPayoff

# what the binary actually contains, when a path above stops matching
$BIN --list_content
```

`RkeQLExtTestSuite` is the outer `BOOST_FIXTURE_TEST_SUITE` in
`rke/testsuite/BonusClassicOption.cpp`, not the `BOOST_TEST_MODULE` name in
`rke/testsuite/testsuite.cpp`.

## 6. Linting Locally

clang-tidy, the same way the workflow does it, on a separate build tree so the
main one keeps its cached objects:

```bash
cmake -S . -B ./build/tidy -G Ninja \
    -DRKE_USE_CLANG_TIDY=ON -DRKE_CLANG_TIDY_OPTIONS=--fix \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++
cmake --build ./build/tidy -j 1 -v
```

`-j 1` is not a suggestion: `--fix` rewrites headers shared between translation
units, and parallel jobs corrupt each other's edits.

clang-format is not wired into the build. Run it over the touched files only, and
never over `external/` — `.clang-format-ignore` excludes it, and CI pins version
20, past the 18 that file needs:

```bash
clang-format -i rke/ql/ext/instruments/BonusClassicOption.cpp
```

## 7. CI Map

Everything is under `.github/workflows/`.

| Workflow                     | Triggers                                                                 | Notes                                               |
| ---------------------------- | ------------------------------------------------------------------------ | --------------------------------------------------- |
| `cmake-and-ctest.yml`        | push to `master`, every pull request, nightly `33 5 * * *` UTC, dispatch | The only workflow that gates a pull request.        |
| `clang-format-lint.yml`      | dispatch only                                                            | Opens a pull request with the fixes.                |
| `clang-tidy.yml`             | dispatch only                                                            | Opens a pull request with the fixes.                |
| `codeql.yml`                 | dispatch only                                                            | Autobuild repeats the whole Ubuntu build, uncached. |
| `delete_workflow_caches.yml` | pull request closed, branch deleted, dispatch                            | Keeps the shared 10 GB cache quota clear.           |
| `prune_ccache_entries.yml`   | nightly `33 8 * * *` UTC, dispatch                                       | Thins `master`'s compiler caches; `dry-run` input.  |

The matrix is macOS/clang, Ubuntu/clang, Ubuntu/gcc and Windows/MSVC, each at
C++17, 20 and 23, release only — twelve legs. `ci-gate` collapses them into the
single status the branch ruleset requires; it runs `if: always()`, so a failing
leg cannot slip through as a skipped check.

Compiler caches (ccache, sccache on Windows) are keyed on
os-arch-compiler-standard and are written back **only** from `master`. A topic
branch restores `master`'s entry but never writes one, so its build times are not
comparable with master's.

Every `master` run writes a new timestamped entry per key rather than replacing
one, so `prune_ccache_entries.yml` keeps the newest per key and deletes the rest.
It also deletes the survivor once nothing has refreshed that key for 30 days: a
key only stops being refreshed when the leg writing it is gone, and its entry
then holds quota no build can restore from. Dispatch it with `dry-run` to see
what it would remove.

`clang-format-lint.yml` and `clang-tidy.yml` open a pull request with whatever
they changed, which is why neither has a push or `pull_request` trigger. **Ask
before dispatching one**; never start a run on your own initiative. Both also
need the repository setting "Allow GitHub Actions to create and approve pull
requests", which no `permissions:` block can grant — while it is off, their last
step fails however the workflow is triggered.

## 8. Source of Truth

When something above looks stale, verify against `CMakeLists.txt`,
`CMakePresets.json`, `cmake/WarningLevels.cmake`, `rke/*/CMakeLists.txt`,
`benchmark/CMakeLists.txt`, `.clang-format`, `.clang-tidy`,
`.github/workflows/*.yml` and `.github/actions/setup/action.yml`.
