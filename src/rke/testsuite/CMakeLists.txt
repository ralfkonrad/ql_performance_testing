# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_testsuite
        testsuite.cpp
        CachedStepPathGenerator.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
)

target_link_libraries(rke_testsuite PRIVATE
        rke_ql_ext
        rke_common
        rke::warnings
)

add_test(
        # The rke_ prefix is what the test presets filter on; rename it and
        # `ctest --preset release` runs nothing.
        NAME rke_testsuite
        COMMAND rke_testsuite -l message
)
