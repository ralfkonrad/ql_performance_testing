# The warning flags, as an INTERFACE target a target links by name. Only the flags: the
# policy on whether they are fatal is a directory property set once at the own-code root,
# src/CMakeLists.txt, because COMPILE_WARNING_AS_ERROR cannot travel along an INTERFACE link.
add_library(rke_warnings INTERFACE)

# Link the alias, never the bare name: CMake hands an unknown plain name to the linker and
# the typo surfaces as a missing -lrke_warnigns at link time, while an unknown ::-qualified
# name is an error at configure time.
add_library(rke::warnings ALIAS rke_warnings)

if (MSVC)
  # warning level 4
  target_compile_options(rke_warnings INTERFACE -W4)
else ()
  # lots of warnings
  target_compile_options(rke_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif ()
