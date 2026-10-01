# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_library(rke_warnings INTERFACE)

# Consumers link the alias: an unknown `::` name is a configure error, while a
# mistyped rke_warnings is taken for a plain library name and passed to the linker.
add_library(rke::warnings ALIAS rke_warnings)

if (MSVC)
  target_compile_options(rke_warnings INTERFACE -W4)
else ()
  target_compile_options(rke_warnings INTERFACE -Wall -Wextra -Wpedantic)

  # Clang 22 and later flag __COUNTER__, which every Boost.Test registration macro
  # expands at our call site, as a C2y extension under -Wpedantic. The probe is on
  # the positive spelling: GCC accepts any unknown -Wno-* flag silently.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wc2y-extensions RKE_HAS_WC2Y_EXTENSIONS)
  if (RKE_HAS_WC2Y_EXTENSIONS)
    target_compile_options(rke_warnings INTERFACE -Wno-c2y-extensions)
  endif ()
endif ()
