# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(
        rke_benchmark_calendar

        BenchmarkCalendar.cpp
)

target_link_libraries(
        rke_benchmark_calendar PRIVATE
        ql_library
        benchmark::benchmark
        rke::warnings
)

add_test(
        # A smoke test, not a measurement, as for rke_benchmark_bonusclassicoption.
        NAME rke_benchmark_calendar
        COMMAND rke_benchmark_calendar --benchmark_dry_run=true
)
