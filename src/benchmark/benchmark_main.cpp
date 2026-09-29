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

BENCHMARK_MAIN();
