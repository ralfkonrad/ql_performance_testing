set(RKE_QL_EXT_SOURCES
        instruments/BonusClassicOption.cpp
)

set(RKE_QL_EXT_HEADER
        instruments/BonusClassicOption.hpp
)

add_library(rke_ql_ext STATIC
        ${RKE_QL_EXT_SOURCES}
        ${RKE_QL_EXT_HEADER}
)

target_include_directories(rke_ql_ext PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
