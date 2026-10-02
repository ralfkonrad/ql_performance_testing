# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(
        rke_benchmark_rng

        BenchmarkXoshiro256StarStarRng.cpp
)

target_link_libraries(
        rke_benchmark_rng PRIVATE
        ql_library
        benchmark::benchmark
        rke::warnings
)

add_test(
        # A smoke test, not a measurement, as for rke_benchmark_bonusclassicoption.
        NAME rke_benchmark_rng
        COMMAND rke_benchmark_rng --benchmark_dry_run=true
)
