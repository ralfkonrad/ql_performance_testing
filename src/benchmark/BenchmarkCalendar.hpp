// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BENCHMARKCALENDAR_HPP
#define BENCHMARKCALENDAR_HPP

#include <benchmark/benchmark.h>

namespace RKE::Benchmark {
    void BM_TestCalendar(benchmark::State& state);
}

#endif // BENCHMARKCALENDAR_HPP
