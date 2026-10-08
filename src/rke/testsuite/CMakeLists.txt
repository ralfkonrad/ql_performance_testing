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

# Required for every upstream source compiled into one of our targets: neither linted nor
# warning-gated, since rke::warnings and CMAKE_COMPILE_WARNING_AS_ERROR would otherwise fail
# the build on an upstream line nothing in this repository may edit.
if (MSVC)
    set(RKE_NO_WARNINGS /w)
else ()
    set(RKE_NO_WARNINGS -w)
endif ()
set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        PROPERTIES
        SKIP_LINTING TRUE
        COMPILE_OPTIONS ${RKE_NO_WARNINGS}
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
