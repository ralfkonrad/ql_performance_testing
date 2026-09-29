# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

set(RKE_QL_EXT_SOURCES
        instruments/BonusClassicOption.cpp
        pricingengines/bonusclassic/MCBonusClassicEngine.cpp
)

set(RKE_QL_EXT_HEADER
        Error.hpp
        instruments/BonusClassicOption.hpp
        pricingengines/bonusclassic/MCBonusClassicEngine.hpp
)

add_library(rke_ql_ext STATIC
        ${RKE_QL_EXT_SOURCES}
)

target_sources(rke_ql_ext PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES ${RKE_QL_EXT_HEADER}
)

target_link_libraries(rke_ql_ext
        PUBLIC ql_library
        PRIVATE rke::warnings
)
