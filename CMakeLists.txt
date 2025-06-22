cmake_minimum_required(VERSION 3.25.0)

# For MSVC RUNTIME LIBRARY, need CMP0091=NEW and cmake 3.15+
cmake_policy(SET CMP0091 NEW)

project(ql_performance_testing VERSION 0.0.1)

# Options
option(CMAKE_COMPILE_WARNING_AS_ERROR "Compile warnings as errors" ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Path for package-local cmake modules
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif ()

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Please specify CMAKE_CXX_STANDARD of 17 or higher")
endif ()

# Boost static libs ON for MSVC
if (NOT DEFINED Boost_USE_STATIC_LIBS)
    set(Boost_USE_STATIC_LIBS ${MSVC})
endif ()

# Boost static runtime ON for MSVC
if (NOT DEFINED Boost_USE_STATIC_RUNTIME)
    set(Boost_USE_STATIC_RUNTIME ${MSVC})
endif ()

# The FindBoost module is removed in cmake 3.30, finding the upstream BoostConfig.cmake
if (POLICY CMP0167)
    find_package(Boost CONFIG REQUIRED)
else ()
    find_package(Boost REQUIRED)
endif ()

add_subdirectory(external SYSTEM)

include_directories(SYSTEM
        external/benchmark/include
        external/QuantLib
        ${Boost_INCLUDE_DIRS})

link_libraries(
        ql_library
        benchmark::benchmark)

include(WarningLevels)

add_subdirectory(rke)

add_subdirectory(benchmark)
