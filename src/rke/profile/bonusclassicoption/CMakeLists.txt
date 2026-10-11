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

# Smoke tests, not measurements: one pricing per monitoring mode and path generation, one per
# smile market at 256 paths instead of 2^16, one per mode of the finite-difference engine and
# one per smile market under it, and one each of the binomial and the analytic engine, so CI
# catches a profile executable that throws before anyone profiles it.
add_test(
        NAME rke_profile_bonusclassicoption_discrete
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine mc --market flat
                --path-generation cached --samples=65536
)

add_test(
        NAME rke_profile_bonusclassicoption_continuous
        COMMAND rke_profile_bonusclassicoption
                --monitoring continuous --iterations 1 --engine mc --market flat
                --path-generation cached --samples=65536
)

add_test(
        NAME rke_profile_bonusclassicoption_discrete_uncached
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine mc --market flat
                --path-generation uncached --samples=65536
)

add_test(
        NAME rke_profile_bonusclassicoption_continuous_uncached
        COMMAND rke_profile_bonusclassicoption
                --monitoring continuous --iterations 1 --engine mc --market flat
                --path-generation uncached --samples=65536
)

add_test(
        NAME rke_profile_bonusclassicoption_smile_bilinear
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine mc --market smile-bilinear
                --path-generation cached --samples=256
)

add_test(
        NAME rke_profile_bonusclassicoption_smile_bicubic
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine mc --market smile-bicubic
                --path-generation cached --samples=256
)

add_test(
        NAME rke_profile_bonusclassicoption_fd_discrete
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine fd --market flat
)

add_test(
        NAME rke_profile_bonusclassicoption_fd_continuous
        COMMAND rke_profile_bonusclassicoption
                --monitoring continuous --iterations 1 --engine fd --market flat
)

add_test(
        NAME rke_profile_bonusclassicoption_fd_smile_bilinear
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine fd --market smile-bilinear
)

add_test(
        NAME rke_profile_bonusclassicoption_fd_smile_bicubic
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine fd --market smile-bicubic
)

add_test(
        NAME rke_profile_bonusclassicoption_binomial
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine binomial --market flat
)

add_test(
        NAME rke_profile_bonusclassicoption_analytic
        COMMAND rke_profile_bonusclassicoption
                --monitoring continuous --iterations 1 --engine analytic --market flat
)

# The Monte-Carlo options are required with mc and refused with the other engines. Each test
# passes on the refusal's message, not on a failing exit code, which any other error would give.
add_test(
        NAME rke_profile_bonusclassicoption_mc_without_samples
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine mc --market flat
                --path-generation cached
)

add_test(
        NAME rke_profile_bonusclassicoption_fd_with_path_generation
        COMMAND rke_profile_bonusclassicoption
                --monitoring discrete --iterations 1 --engine fd --market flat
                --path-generation cached
)

set_tests_properties(rke_profile_bonusclassicoption_mc_without_samples PROPERTIES
        PASS_REGULAR_EXPRESSION "--samples is required with --engine mc"
)

set_tests_properties(rke_profile_bonusclassicoption_fd_with_path_generation PROPERTIES
        PASS_REGULAR_EXPRESSION "--path-generation applies to --engine mc only, not fd"
)
