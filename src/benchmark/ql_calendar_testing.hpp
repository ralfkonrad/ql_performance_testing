// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef QL_PERFORMANCE_TESTING_QL_CALENDAR_TESTING_HPP
#define QL_PERFORMANCE_TESTING_QL_CALENDAR_TESTING_HPP

#include <benchmark/benchmark.h>

// Global scope is legacy; new benchmarks go in RKE::Benchmark.
void BM_TestCalendar(benchmark::State& state);

#endif // QL_PERFORMANCE_TESTING_QL_CALENDAR_TESTING_HPP
