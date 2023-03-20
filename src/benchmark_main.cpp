#include <benchmark/benchmark.h>
#include "ql_calendar_testing.hpp"

BENCHMARK(BM_TestCalendar)->Unit(benchmark::kMillisecond)->Iterations(1000);

BENCHMARK_MAIN();
