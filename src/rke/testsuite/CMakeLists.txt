add_executable(rke_ql_ext_testsuite
        testsuite.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.hpp
)

# Upstream sources compiled into our target: never lint or auto-fix them.
set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        PROPERTIES SKIP_LINTING TRUE
)

# Boost.Test is header-only here, through <boost/test/included/unit_test.hpp>, so it needs
# no component of its own; the include directories arrive with ql_library.
target_link_libraries(rke_ql_ext_testsuite PRIVATE
        rke_ql_ext
        rke::warnings
)

add_test(
        NAME rke_ql_ext_testsuite
        COMMAND rke_ql_ext_testsuite -l message
)
