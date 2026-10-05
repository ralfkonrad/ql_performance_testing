# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_testsuite
        testsuite.cpp
        CachedStepPathGenerator.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.hpp
)

# Required for every upstream source compiled into one of our targets.
set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        PROPERTIES SKIP_LINTING TRUE
)

target_link_libraries(rke_testsuite PRIVATE
        rke_ql_ext
        rke::warnings
)

add_test(
        # The rke_ prefix is what the test presets filter on; rename it and
        # `ctest --preset release` runs nothing.
        NAME rke_testsuite
        COMMAND rke_testsuite -l message
)
