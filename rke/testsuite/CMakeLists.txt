add_executable(rke_ql_ext_testsuite
        testsuite.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
        ../../external/QuantLib/test-suite/utilities.cpp
        ../../external/QuantLib/test-suite/utilities.hpp)

target_link_libraries(rke_ql_ext_testsuite PUBLIC
        rke_ql_ext)
