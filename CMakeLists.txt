# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 4.0)

project(ql_performance_testing VERSION 0.0.1 LANGUAGES CXX)

option(RKE_COMPILE_WARNING_AS_ERROR "Compile warnings as errors" ON)
option(RKE_USE_CLANG_TIDY "Use clang-tidy when building" OFF)

# Selects the static runtime (/MT); must stay in step with Boost_USE_STATIC_RUNTIME
# further down, as the two disagreeing is an MSVC link failure.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(WarningLevels)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif ()

# 17 is the floor CI actually compiles, alongside 20 and 23.
if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Please specify CMAKE_CXX_STANDARD of 17 or higher")
endif ()

if (NOT DEFINED Boost_USE_STATIC_LIBS)
    set(Boost_USE_STATIC_LIBS ${MSVC})
endif ()

if (NOT DEFINED Boost_USE_STATIC_RUNTIME)
    set(Boost_USE_STATIC_RUNTIME ${MSVC})
endif ()

find_package(Boost CONFIG REQUIRED)

include(CTest)

# SYSTEM is what keeps -Wall -Wextra -Wpedantic and warnings-as-errors off the
# submodule headers our translation units include.
add_subdirectory(external SYSTEM)

add_subdirectory(src)

