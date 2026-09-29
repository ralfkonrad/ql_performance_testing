#include "BenchmarkBonusClassicOption.hpp"
#include "ql_calendar_testing.hpp"
#include "ql_xoshiro256starstarrng_benchmarking.hpp"
#include <benchmark/benchmark.h>

BENCHMARK(RKE::QL::External::BM_BonusClassicOption)
    ->Name("BonusClassicOption")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(100);

BENCHMARK(BM_TestCalendar)->Name("TARGET.isBusinessDay();")->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
