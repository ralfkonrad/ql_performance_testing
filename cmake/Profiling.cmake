# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# Debug info and frame pointers on top of the build type's optimisation, so a sampling
# profiler can unwind and symbolise the stack. Directory-scoped on purpose: included before
# add_subdirectory(external), it reaches QuantLib, where the stacks worth reading live.
if (NOT RKE_PROFILING)
  return()
endif ()

if (MSVC)
  # x64 unwinds from tables, so debug info is all a profiler needs. Embedded (/Z7) for the
  # same reason as at the top level: ccache and sccache refuse /Zi.
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
  add_link_options(/DEBUG)
else ()
  add_compile_options(-g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer)
endif ()
