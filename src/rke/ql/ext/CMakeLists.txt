# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

set(RKE_QL_EXT_SOURCES
        instruments/BonusClassicOption.cpp
        methods/montecarlo/BlackScholesStepCache.cpp
        methods/montecarlo/LocalVolStepCache.cpp
        pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.cpp
        pricingengines/bonusclassic/MCBonusClassicEngine.cpp
)

# Headers are listed by hand, and an unlisted one still compiles, so an omission
# is silent.
set(RKE_QL_EXT_HEADER
        Errors.hpp
        instruments/BonusClassicOption.hpp
        methods/montecarlo/BlackScholesStepCache.hpp
        methods/montecarlo/CachedStepPathGenerator.hpp
        methods/montecarlo/LocalVolStepCache.hpp
        pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp
        pricingengines/bonusclassic/MCBonusClassicEngine.hpp
)

add_library(rke_ql_ext STATIC
        ${RKE_QL_EXT_SOURCES}
)

target_sources(rke_ql_ext PUBLIC
        FILE_SET HEADERS
        # This is what puts <rke/...> on every consumer's include path.
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES ${RKE_QL_EXT_HEADER}
)

target_link_libraries(rke_ql_ext
        PUBLIC ql_library
        PRIVATE rke::warnings
)
