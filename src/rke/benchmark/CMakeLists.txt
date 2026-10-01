# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(
        rke_benchmark

        benchmark_main.cpp

        # A new BM_* function runs only once BENCHMARK(...) registers it in
        # benchmark_main.cpp; adding the file pair here is not enough.
        BenchmarkBonusClassicOption.cpp
        BenchmarkBonusClassicOption.hpp

        BenchmarkCalendar.cpp
        BenchmarkCalendar.hpp

        BenchmarkXoshiro256StarStarRng.cpp
        BenchmarkXoshiro256StarStarRng.hpp
)

target_link_libraries(
        rke_benchmark PRIVATE
        rke_ql_ext
        benchmark::benchmark
        rke::warnings
)

add_test(
        # A smoke test, not a measurement: one iteration of each registered benchmark, so CI
        # catches a benchmark that throws without timing anything on a shared runner.
        NAME rke_benchmark
        COMMAND rke_benchmark --benchmark_dry_run=true
)
