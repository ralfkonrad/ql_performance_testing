# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(
        rke_benchmark_bonusclassicoption

        BenchmarkBonusClassicOption.cpp
)

target_link_libraries(
        rke_benchmark_bonusclassicoption PRIVATE
        rke_common
        benchmark::benchmark
        rke::warnings
)

add_test(
        # A smoke test, not a measurement: one iteration of each registered benchmark, so CI
        # catches a benchmark that throws without timing anything on a shared runner.
        NAME rke_benchmark_bonusclassicoption
        COMMAND rke_benchmark_bonusclassicoption --benchmark_dry_run=true
)
