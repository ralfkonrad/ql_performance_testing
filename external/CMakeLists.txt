# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_subdirectory(QuantLib)

# Must precede add_subdirectory: under CMP0077 this normal variable overrides the
# submodule's own option(... ON), which would otherwise pull in GoogleTest.
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(benchmark)

# Header-only, which CLI11_PRECOMPILED leaves at OFF. Tests, examples and install switch off on
# their own below a parent project; the docs target would not, wherever Doxygen is installed.
set(CLI11_BUILD_DOCS OFF)
add_subdirectory(CLI11)
