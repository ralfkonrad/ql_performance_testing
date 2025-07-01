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
        ql_performance_testing
        rke_ql_ext
)

target_include_directories(
        ql_performance_testing PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
