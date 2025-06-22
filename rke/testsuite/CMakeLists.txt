add_executable(rke_ql_ext_testsuite testsuite.cpp
        BonusClassicOption.cpp)
target_link_libraries(rke_ql_ext_testsuite PUBLIC
        rke_ql_ext)