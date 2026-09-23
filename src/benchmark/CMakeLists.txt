add_executable(
        ql_performance_testing

        benchmark_main.cpp

        BenchmarkBonusClassicOption.cpp
        BenchmarkBonusClassicOption.hpp

        ql_calendar_testing.cpp
        ql_calendar_testing.hpp

        ql_xoshiro256starstarrng_benchmarking.cpp
        ql_xoshiro256starstarrng_benchmarking.hpp
)

target_link_libraries(
        ql_performance_testing PRIVATE
        rke_ql_ext
        benchmark::benchmark
        rke::warnings
)
