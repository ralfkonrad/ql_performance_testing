# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_profile_bonusclassicoption
        ProfileBonusClassicOption.cpp
)

target_link_libraries(rke_profile_bonusclassicoption PRIVATE
        rke_common
        rke::warnings
)

# Smoke tests, not measurements: one pricing per monitoring mode, so CI catches a profile
# executable that throws before anyone profiles it.
add_test(
        NAME rke_profile_bonusclassicoption_discrete
        COMMAND rke_profile_bonusclassicoption discrete 1
)

add_test(
        NAME rke_profile_bonusclassicoption_continuous
        COMMAND rke_profile_bonusclassicoption continuous 1
)
