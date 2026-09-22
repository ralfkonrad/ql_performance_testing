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
        ${RKE_QL_EXT_HEADER}
)

target_include_directories(rke_ql_ext PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

# PUBLIC: the headers name QuantLib types, so every consumer needs ql_library's include
# directories and its Boost ones, both of which it exports itself.
target_link_libraries(rke_ql_ext PUBLIC
        ql_library
)
