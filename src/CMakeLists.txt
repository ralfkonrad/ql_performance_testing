# The own-code root. Everything we write lives below here and nothing else does, which makes
# external/ a sibling rather than a descendant: a directory property set here reaches every
# target of ours and no submodule, whatever order the top level adds them in.

# Initialises COMPILE_WARNING_AS_ERROR on every target created below. The property, rather
# than a hand-written -Werror in rke::warnings, because CMake spells it for each compiler it
# knows and `cmake --compile-no-warning-as-error` overrides it; a literal flag ignores both.
set(CMAKE_COMPILE_WARNING_AS_ERROR ${RKE_COMPILE_WARNING_AS_ERROR})

# Here rather than at the top level for the same reason: clang-tidy analyses the targets
# below this directory, which is exactly our own code, without depending on where
# add_subdirectory(external) sits.
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

add_subdirectory(benchmark)
