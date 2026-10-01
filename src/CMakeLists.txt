# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# Set here and not at the top level: a directory property only travels down, and
# external/ is a sibling of src/.
set(CMAKE_COMPILE_WARNING_AS_ERROR ${RKE_COMPILE_WARNING_AS_ERROR})

# Same reason: set above this directory, clang-tidy would lint both submodules.
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

add_subdirectory(rke)
