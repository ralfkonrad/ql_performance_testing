# SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_library(rke_warnings INTERFACE)

add_library(rke::warnings ALIAS rke_warnings)

if (MSVC)
  target_compile_options(rke_warnings INTERFACE -W4)
else ()
  target_compile_options(rke_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif ()
