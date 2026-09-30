# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# Must precede add_subdirectory: under CMP0077 this normal variable overrides the
# submodule's own option(... ON), which would otherwise pull in GoogleTest.
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(benchmark)

add_subdirectory(QuantLib)
