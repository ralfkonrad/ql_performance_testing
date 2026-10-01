// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BenchmarkBonusClassicOption.hpp"
#include "ql_calendar_testing.hpp"
// Nothing from this header is registered; the comparisons are kept dormant, not left over.
#include "ql_xoshiro256starstarrng_benchmarking.hpp"
#include <benchmark/benchmark.h>

BENCHMARK(RKE::QL::Ext::BM_BonusClassicOption)
    ->Name("BonusClassicOption")
    ->Unit(benchmark::kMillisecond)
    // Pins the count instead of letting google-benchmark scale to statistical stability,
    // trading the variance estimate for a predictable wall time; measure variance with
    // --benchmark_repetitions.
    ->Iterations(100);

BENCHMARK(BM_TestCalendar)->Name("TARGET.isBusinessDay();")->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
