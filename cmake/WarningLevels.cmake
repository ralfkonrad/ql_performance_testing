# set the default warning level
if (MSVC)
  # warning level 4
  add_compile_options(-W4)
else ()
  # lots of warnings
  add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

# Specify whether to treat warnings on compile as errors.
if (RKE_COMPILE_WARNING_AS_ERROR)
  # all compiler warnings as errors
  if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    # since v3.24 cmake can set all compiler warnings as errors itself
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
  else ()
    # or set them manually
    if (MSVC)
      add_compile_options(-WX)
    else ()
      add_compile_options(-Werror)
    endif ()
  endif ()
endif ()

