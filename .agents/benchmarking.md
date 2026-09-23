# Benchmarking

Read this when adding, changing, or running a benchmark. The harness is
google-benchmark, built from the `external/benchmark` submodule.

## 1. Adding One

A benchmark is a free function taking `benchmark::State&`, declared in a header
and defined in the matching `.cpp`:

```cpp
namespace RKE::QL::External {
    void BM_YourThing(benchmark::State& state);
}
```

Three places have to agree, or the benchmark builds and never runs:

1. The `.cpp` and `.hpp` listed in `src/benchmark/CMakeLists.txt`.
2. The header included in `src/benchmark/benchmark_main.cpp`.
3. A `BENCHMARK(...)` registration in that same file.

```cpp
BENCHMARK(RKE::QL::External::BM_YourThing)
    ->Name("YourThing")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(100);
```

`Iterations()` pins the count instead of letting google-benchmark scale until the
run is statistically stable. That keeps a pricing benchmark's wall time
predictable, at the cost of any variance estimate — ask for
`--benchmark_repetitions` when you need one.

The `ql_*` files in `src/benchmark/` predate the namespace and register at global
scope. Leave them as they are; new files use `RKE::QL::External`.

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

## 4. Running

```bash
cmake --build --preset release --target ql_performance_testing
./build/release/src/benchmark/ql_performance_testing
```

Never quote a number from a `debug` build — google-benchmark prints a warning
when its own library was built that way, but says nothing about yours.

Useful flags:

```bash
# one benchmark, by the ->Name() given at registration
./build/release/src/benchmark/ql_performance_testing --benchmark_filter=BonusClassicOption

# variance across repetitions, with the aggregates only
./build/release/src/benchmark/ql_performance_testing \
    --benchmark_repetitions=10 --benchmark_report_aggregates_only=true

# machine-readable, for comparing two revisions
./build/release/src/benchmark/ql_performance_testing \
    --benchmark_out=before.json --benchmark_out_format=json
```

`external/benchmark/tools/compare.py` reads those JSON files and reports the
delta between two runs. Use it rather than eyeballing two console outputs.

## 5. Commented-Out Registrations Are Deliberate

`benchmark_main.cpp` keeps the `Xoshiro256StarStar` versus `MersenneTwister`
comparisons commented out, and `cmake-and-ctest.yml` builds the executable
without ever running it. Neither is an oversight: the RNG comparisons are
re-enabled when that question comes up again, and there is no CI baseline to
compare against. Do not "clean up" the commented block, and do not add a
benchmark run to the matrix without being asked.

Consequence: every benchmark number is a local measurement. When you quote one,
name the machine, the compiler and the C++ standard it came from, and measure
both sides of a comparison in the same session.
