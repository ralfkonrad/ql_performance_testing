#include <benchmark/benchmark.h>
#include "ql_xoshiro256starstarrng_benchmarking.hpp"

BENCHMARK(BM_TestXoshiro256StarStarRngNextInt64);
BENCHMARK(BM_TestMersenneTwisterUniformRngNextInt32);
BENCHMARK(BM_TestXoshiro256StarStarRngNext);
BENCHMARK(BM_TestMersenneTwisterUniformRngNext);
BENCHMARK(BM_TestXoshiro256StarStarBoxMullerNext);
BENCHMARK(BM_TestMersenneTwisterBoxMullerRngNext);

BENCHMARK_MAIN();
