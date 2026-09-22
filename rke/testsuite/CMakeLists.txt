add_executable(rke_ql_ext_testsuite
        testsuite.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
        ../../external/QuantLib/test-suite/utilities.cpp
        ../../external/QuantLib/test-suite/utilities.hpp
)

# Upstream sources compiled into our target: never lint or auto-fix them.
set_source_files_properties(
        ../../external/QuantLib/test-suite/utilities.cpp
        PROPERTIES SKIP_LINTING TRUE
)

target_link_libraries(rke_ql_ext_testsuite PUBLIC
        rke_ql_ext
)

add_test(
        NAME rke_ql_ext_testsuite
        COMMAND rke_ql_ext_testsuite -l message
)
