# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# QuantLib defaults to a shared library on UNIX, where calls between its own functions go
# through the PLT. Static is the default here; -DBUILD_SHARED_LIBS=ON still overrides it.
if (NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF)
endif ()
add_subdirectory(QuantLib)

# Must precede add_subdirectory: under CMP0077 this normal variable overrides the
# submodule's own option(... ON), which would otherwise pull in GoogleTest.
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(benchmark)

# Header-only, which CLI11_PRECOMPILED leaves at OFF. Tests, examples and install switch off on
# their own below a parent project; the docs target would not, wherever Doxygen is installed.
set(CLI11_BUILD_DOCS OFF)
add_subdirectory(CLI11)
