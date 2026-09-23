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

# FILE_SET keeps the headers and the directory they are reached through in one place: under
# PUBLIC, BASE_DIRS lands in INCLUDE_DIRECTORIES and INTERFACE_INCLUDE_DIRECTORIES both, so
# <rke/ql/ext/...> resolves for this target and for everything that links it.
target_sources(rke_ql_ext PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES ${RKE_QL_EXT_HEADER}
)

# PUBLIC: the headers name QuantLib types, so every consumer needs ql_library's include
# directories and its Boost ones, both of which it exports itself.
target_link_libraries(rke_ql_ext
        PUBLIC ql_library
        PRIVATE rke::warnings
)
