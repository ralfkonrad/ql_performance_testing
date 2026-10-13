# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_profile_barrieroption
        ProfileBarrierOption.cpp
)

target_link_libraries(rke_profile_barrieroption PRIVATE
        rke_common
        rke_cli
        rke::warnings
)

# Smoke tests, not measurements: one pricing per monitoring mode on the flat market, and one per
# smile market under discrete monitoring at 256 paths instead of 2^16, so CI catches a profile
# executable that throws before anyone profiles it.
add_test(
        NAME rke_profile_barrieroption_discrete
        COMMAND rke_profile_barrieroption
                --monitoring discrete --iterations 1 --market flat --samples=65536
)

add_test(
        NAME rke_profile_barrieroption_continuous
        COMMAND rke_profile_barrieroption
                --monitoring continuous --iterations 1 --market flat --samples=65536
)

add_test(
        NAME rke_profile_barrieroption_smile_bilinear
        COMMAND rke_profile_barrieroption
                --monitoring discrete --iterations 1 --market smile-bilinear --samples=256
)

add_test(
        NAME rke_profile_barrieroption_smile_bicubic
        COMMAND rke_profile_barrieroption
                --monitoring discrete --iterations 1 --market smile-bicubic --samples=256
)

# Every option is required, --samples as well, which RKE::Cli::addSamplesOption leaves optional.
# The test passes on CLI11's own message for the missing option, not on a failing exit code,
# which any other error would give.
add_test(
        NAME rke_profile_barrieroption_without_samples
        COMMAND rke_profile_barrieroption
                --monitoring discrete --iterations 1 --market flat
)

set_tests_properties(rke_profile_barrieroption_without_samples PROPERTIES
        PASS_REGULAR_EXPRESSION "--samples is required"
)
