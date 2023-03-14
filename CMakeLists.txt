cmake_minimum_required(VERSION 3.22.0)

# Default build type for single-config generators (set this before project() command)
# For multi-config generators, such as Visual Studio, use: cmake --build . --config=<CONFIG>
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE
            STRING "Build type for single-config generators" FORCE)
endif ()

project(ql_performance_testing VERSION 0.0.1)

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

add_subdirectory(external)

include(WarningLevels)