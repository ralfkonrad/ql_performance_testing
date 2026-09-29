# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_ql_ext_testsuite
        testsuite.cpp
        BonusClassicOption.cpp
        TestSuiteFixture.hpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.hpp
)

set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/external/QuantLib/test-suite/utilities.cpp
        PROPERTIES SKIP_LINTING TRUE
)

target_link_libraries(rke_ql_ext_testsuite PRIVATE
        rke_ql_ext
        rke::warnings
)

add_test(
        NAME rke_ql_ext_testsuite
        COMMAND rke_ql_ext_testsuite -l message
)
