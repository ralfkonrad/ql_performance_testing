#include <benchmark/benchmark.h>
#include "ql_xoshiro256starstarrng_benchmarking.hpp"

const auto iterations = 1000000000;

BENCHMARK(BM_TestXoshiro256StarStarRngNextInt64)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(iterations);

BENCHMARK(BM_TestMersenneTwisterUniformRngNextInt32)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(iterations);

BENCHMARK(BM_TestXoshiro256StarStarRngNext)->Unit(benchmark::kNanosecond)->Iterations(iterations);

BENCHMARK(BM_TestMersenneTwisterUniformRngNext)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(iterations);

BENCHMARK(BM_TestXoshiro256StarStarBoxMullerNext)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(iterations / 10);

BENCHMARK(BM_TestMersenneTwisterBoxMullerRngNext)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(iterations / 10);

BENCHMARK_MAIN();
