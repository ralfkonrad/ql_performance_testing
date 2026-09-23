# CMake 4 is the floor: it turns every policy through 4.0 NEW without restating them one at
# a time, CMP0167 above all, which retires the FindBoost module for the upstream
# BoostConfig.cmake. Every image the matrix runs on ships 4.4.3.
cmake_minimum_required(VERSION 4.0)

# LANGUAGES CXX: both submodules declare CXX only, so nothing here needs a C compiler and
# probing for one only slows configure down.
project(ql_performance_testing VERSION 0.0.1 LANGUAGES CXX)

# Options
# Read by rke_target_warnings(), which each of our targets calls for itself.
option(RKE_COMPILE_WARNING_AS_ERROR "Compile warnings as errors" ON)
option(RKE_USE_CLANG_TIDY "Use clang-tidy when building" OFF)

# Honoured because CMP0091 is NEW, which the floor above guarantees.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Path for package-local cmake modules
list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Defines rke_warnings and rke_target_warnings. Position no longer matters for safety: the
# warnings apply to the targets that link them, not to everything declared afterwards.
include(WarningLevels)

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

find_package(Boost CONFIG REQUIRED)

# Calls enable_testing() itself, under BUILD_TESTING.
include(CTest)

# SYSTEM: both submodules export their own include directories, and the directory property
# demotes those to -isystem for every target created under it. Nothing here has to name a
# path inside external/ itself.
add_subdirectory(external SYSTEM)

# Set after add_subdirectory(external) so that clang-tidy analyses our own targets only.
if (RKE_USE_CLANG_TIDY)
    if (NOT DEFINED RKE_CLANG_TIDY)
        set(RKE_CLANG_TIDY clang-tidy)
    endif ()
    if (RKE_CLANG_TIDY_OPTIONS)
        set(CMAKE_CXX_CLANG_TIDY "${RKE_CLANG_TIDY};${RKE_CLANG_TIDY_OPTIONS}")
    else ()
        set(CMAKE_CXX_CLANG_TIDY "${RKE_CLANG_TIDY}")
    endif ()
endif ()

add_subdirectory(src)

