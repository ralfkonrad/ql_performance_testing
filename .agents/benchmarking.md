<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Benchmarking

Read this when adding, changing or running a benchmark. The harness is
google-benchmark, built from the `external/benchmark` submodule.

## 1. Adding One

Each workload is its own executable, `rke_benchmark_<workload>`, built in
`src/rke/benchmark/<workload>/` from one file, `Benchmark<Workload>.cpp`. That file
defines the benchmarks, registers each one directly below its function, and ends
in `BENCHMARK_MAIN()`. Nothing else includes it, so there is no header, and the
`BM_*` functions sit in an anonymous namespace inside `RKE::Benchmark`:

```cpp
namespace RKE::Benchmark {
    namespace {
        void BM_YourThing(benchmark::State& state) {
            // setup, then the measured loop
        }

        BENCHMARK(BM_YourThing)
            ->Name("YourThing")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(100);
    }
}

BENCHMARK_MAIN();
```

A new workload gets a new directory, added to `src/rke/benchmark/CMakeLists.txt`,
with a `CMakeLists.txt` that lists the file, links `benchmark::benchmark` and
`rke::warnings`, and adds a dry-run CTest named after the target. A function
without its `BENCHMARK(...)` builds and never runs.

`Iterations()` pins the count instead of letting google-benchmark scale until the
run is statistically stable. That keeps a pricing benchmark's wall time
predictable, at the cost of any variance estimate — ask for
`--benchmark_repetitions` when you need one.

## 2. The Two Things That Silently Measure Nothing

**A lazy instrument.** `Instrument::NPV()` returns a cached value, so a loop that
only calls `NPV()` measures one pricing and then several thousand cache reads.
Call `recalculate()` first, every iteration:

```cpp
for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    bonusClassicOption->recalculate();
    auto npv = bonusClassicOption->NPV();
    benchmark::DoNotOptimize(npv);
}
```

**A dead result.** Without `benchmark::DoNotOptimize`, a release build is free to
delete the whole computation. Wrap whatever the benchmark produces.

The `NOLINT` on the loop is required: `for (auto _ : state)` reads as a dead
store to `clang-analyzer-deadcode.DeadStores`, and warnings are errors here.

## 3. Setup Outside the Loop

Build the process, the term structures, the instrument and the engine before the
loop, and set the evaluation date there too. Only the work being measured belongs
inside. `BM_BonusClassicOption` is the pattern.

When a workload is also profiled, its setup and loop body live in `rke_common`
(`src/rke/common/`), and the benchmark calls them rather than holding a copy, so
the profile executable measures exactly the same work. Changing that setup moves
both. See [`profiling.md`](profiling.md).

## 4. Running

```bash
cmake --build --preset release --target rke_benchmark_bonusclassicoption
BIN=./build/release/src/rke/benchmark/bonusclassicoption/rke_benchmark_bonusclassicoption
$BIN
```

Never quote a number from a `debug` build — google-benchmark prints a warning
when its own library was built that way, but says nothing about yours.

Useful flags:

```bash
# one benchmark: a regex over the ->Name() plus the suffix google-benchmark appends
$BIN --benchmark_filter='^BonusClassicOption/'   # --benchmark_list_tests=true shows names

# variance across repetitions, with the aggregates only
$BIN --benchmark_repetitions=10 --benchmark_report_aggregates_only=true

# machine-readable, for comparing two revisions
$BIN --benchmark_out=before.json --benchmark_out_format=json
```

Spell flags out, or quote each one separately. zsh does not word-split an unquoted
`$FLAGS`, so a flag string in one variable reaches the binary as one
`--benchmark_filter` regex. That matches nothing, and the run writes an empty JSON
and still exits 0.

`external/benchmark/tools/compare.py` reads those JSON files and reports the
delta between two runs. Use it rather than eyeballing two console outputs. It
needs numpy and scipy, which no project environment provides:

```bash
uv run --no-project --with numpy --with scipy \
    python external/benchmark/tools/compare.py benchmarks before.json after.json
```

Where the time goes is [`profiling.md`](profiling.md), which profiles dedicated
executables, not these binaries.

## 5. CI Runs Them, Never Times Them

CTest runs every `rke_benchmark_<workload>` with `--benchmark_dry_run=true`, one
iteration of each registration, which only proves it does not throw. There is no
CI baseline to compare against, so do not add a timed benchmark run to CI
without being asked.

Consequence: every benchmark number is a local measurement. When you quote one,
name the machine, the compiler and the C++ standard it came from, and measure
both sides of a comparison in the same session.
