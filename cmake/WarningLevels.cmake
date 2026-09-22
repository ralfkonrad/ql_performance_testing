# An INTERFACE target and the function that applies it. A target opts in by name, so the
# flags reach exactly the targets that ask for them and no add_subdirectory() ordering can
# carry them into the submodules.
add_library(rke_warnings INTERFACE)

if (MSVC)
  # warning level 4
  target_compile_options(rke_warnings INTERFACE -W4)
else ()
  # lots of warnings
  target_compile_options(rke_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif ()

# COMPILE_WARNING_AS_ERROR is a target property and does not travel along an INTERFACE link,
# so rke_warnings cannot export it and each target has to be given it here.
function(rke_target_warnings target)
  target_link_libraries(${target} PRIVATE rke_warnings)
  set_target_properties(${target} PROPERTIES
          COMPILE_WARNING_AS_ERROR ${RKE_COMPILE_WARNING_AS_ERROR})
endfunction()
