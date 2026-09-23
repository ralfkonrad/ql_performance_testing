#include "BenchmarkBonusClassicOption.hpp"
#include "ql_calendar_testing.hpp"
#include "ql_xoshiro256starstarrng_benchmarking.hpp"
#include <benchmark/benchmark.h>

BENCHMARK(RKE::QL::External::BM_BonusClassicOption)
    ->Name("BonusClassicOption")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(100);

// One iteration walks every date QuantLib knows, so the count is left to
// google-benchmark rather than pinned the way BonusClassicOption is.
BENCHMARK(BM_TestCalendar)->Name("TARGET.isBusinessDay();")->Unit(benchmark::kMillisecond);

// BENCHMARK(BM_Xoshiro256StarStarNextInt64)->Name("xoshiro256StarStar.nextInt64();");
// BENCHMARK(BM_MersenneTwisterNextInt32)->Name("mersenneTwister.nextInt32();");
//
// BENCHMARK(BM_Xoshiro256StarStarNext)->Name("xoshiro256StarStar.next();");
// BENCHMARK(BM_MersenneTwisterNext)->Name("mersenneTwister.next();");
//
// BENCHMARK(BM_Xoshiro256StarStarZigguratGaussianNext)
//     ->Name("xoshiro256StarStarZigguratGaussianNext.next();");
//
// BENCHMARK(BM_Xoshiro256StarStarBoxMullerGaussianNext)
//     ->Name("xoshiro256StarStarBoxMullerGaussian.next();");
// BENCHMARK(BM_MersenneTwisterBoxMullerGaussianNext)
//     ->Name("mersenneTwisterBoxMullerGaussian.next();");
//
// BENCHMARK(BM_InverseCumulativeRngNext)->Name("inverseCumulativeRng.next();");
//
// BENCHMARK(BM_Xoshiro256StarStarCLGaussianNext)->Name("xoshiro256StarStarCLGaussian.next();");
// BENCHMARK(BM_MersenneTwisterCLGaussianNext)->Name("mersenneTwisterCLGaussian.next();");

BENCHMARK_MAIN();
