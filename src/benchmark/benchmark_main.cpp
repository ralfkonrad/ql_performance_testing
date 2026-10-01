// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BenchmarkBonusClassicOption.hpp"
#include "BenchmarkCalendar.hpp"
// Nothing from this header is registered; the comparisons are kept dormant, not left over.
#include "BenchmarkXoshiro256StarStarRng.hpp"
#include <benchmark/benchmark.h>

BENCHMARK(RKE::Benchmark::BM_BonusClassicOption)
    ->Name("BonusClassicOption")
    ->Unit(benchmark::kMillisecond)
    // Pins the count instead of letting google-benchmark scale to statistical stability,
    // trading the variance estimate for a predictable wall time; measure variance with
    // --benchmark_repetitions.
    ->Iterations(100);

BENCHMARK(RKE::Benchmark::BM_TestCalendar)
    ->Name("TARGET.isBusinessDay();")
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
