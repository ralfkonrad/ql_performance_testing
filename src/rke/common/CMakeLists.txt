# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# Workloads shared by the benchmarks and the profile executables, so both measure the same
# instrument, market and engine.
set(RKE_COMMON_SOURCES
        BonusClassicOptionSetup.cpp
)

# Headers are listed by hand, and an unlisted one still compiles, so an omission
# is silent.
set(RKE_COMMON_HEADER
        BonusClassicOptionSetup.hpp
)

add_library(rke_common STATIC
        ${RKE_COMMON_SOURCES}
)

target_sources(rke_common PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES ${RKE_COMMON_HEADER}
)

target_link_libraries(rke_common
        PUBLIC rke_ql_ext
        PRIVATE rke::warnings
)
