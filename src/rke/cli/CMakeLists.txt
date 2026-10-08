# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# The CLI11 options the benchmark and the profile executables share, header-only, so
# rke_common stays free of CLI11.

# Headers are listed by hand, and an unlisted one still compiles, so an omission
# is silent.
set(RKE_CLI_HEADER
        SamplesOption.hpp
)

add_library(rke_cli INTERFACE)

target_sources(rke_cli INTERFACE
        FILE_SET HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES ${RKE_CLI_HEADER}
)

target_link_libraries(rke_cli INTERFACE
        CLI11::CLI11
        rke::warnings
)
