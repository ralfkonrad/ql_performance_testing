# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(
        rke_benchmark_bonusclassicoption

        BenchmarkBonusClassicOption.cpp
)

target_link_libraries(
        rke_benchmark_bonusclassicoption PRIVATE
        rke_common
        rke_cli
        benchmark::benchmark
        rke::warnings
)

add_test(
        # A smoke test, not a measurement: one iteration of each registered benchmark, at 2^8
        # paths instead of 2^16 on the same time grid, so CI catches a benchmark that throws in
        # seconds and without timing anything on a shared runner.
        NAME rke_benchmark_bonusclassicoption
        COMMAND rke_benchmark_bonusclassicoption --benchmark_dry_run=true --samples=256
)
