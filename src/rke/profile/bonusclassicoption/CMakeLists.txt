# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_profile_bonusclassicoption
        ProfileBonusClassicOption.cpp
)

target_link_libraries(rke_profile_bonusclassicoption PRIVATE
        rke_common
        rke_cli
        rke::warnings
)

# Smoke tests, not measurements: one pricing per monitoring mode and path generation, and one
# per smile market at 256 paths instead of 2^16, so CI catches a profile executable that throws
# before anyone profiles it.
add_test(
        NAME rke_profile_bonusclassicoption_discrete
        COMMAND rke_profile_bonusclassicoption discrete 1
)

add_test(
        NAME rke_profile_bonusclassicoption_continuous
        COMMAND rke_profile_bonusclassicoption continuous 1
)

add_test(
        NAME rke_profile_bonusclassicoption_discrete_uncached
        COMMAND rke_profile_bonusclassicoption discrete 1 --path-generation uncached
)

add_test(
        NAME rke_profile_bonusclassicoption_continuous_uncached
        COMMAND rke_profile_bonusclassicoption continuous 1 --path-generation uncached
)

add_test(
        NAME rke_profile_bonusclassicoption_smile_bilinear
        COMMAND rke_profile_bonusclassicoption discrete 1 --market smile-bilinear --samples=256
)

add_test(
        NAME rke_profile_bonusclassicoption_smile_bicubic
        COMMAND rke_profile_bonusclassicoption discrete 1 --market smile-bicubic --samples=256
)
