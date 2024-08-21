# set the default warning level
if (MSVC)
  # warning level 4
  add_compile_options(-W4)
else ()
  # lots of warnings
  add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

# Specify whether to treat warnings on compile as errors.
if (QL_COMPILE_WARNING_AS_ERROR)
  set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif ()
