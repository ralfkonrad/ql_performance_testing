# The own-code root. Everything we write lives below here and nothing else does, which makes
# external/ a sibling rather than a descendant: a directory property set here reaches every
# target of ours and no submodule, whatever order the top level adds them in.

# Initialises COMPILE_WARNING_AS_ERROR on every target created below. The property, rather
# than a hand-written -Werror in rke::warnings, because CMake spells it for each compiler it
# knows and `cmake --compile-no-warning-as-error` overrides it; a literal flag ignores both.
set(CMAKE_COMPILE_WARNING_AS_ERROR ${RKE_COMPILE_WARNING_AS_ERROR})

add_subdirectory(rke)

add_subdirectory(benchmark)
