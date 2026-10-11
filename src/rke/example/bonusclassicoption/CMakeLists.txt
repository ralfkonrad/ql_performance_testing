# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_example_bonusclassicoption
        ExampleBonusClassicOption.cpp
)

target_link_libraries(rke_example_bonusclassicoption PRIVATE
        rke_common
        CLI11::CLI11
        rke::warnings
)

# A smoke test, not a check of the printed differences: one run of the example, so CI catches
# an example that throws before anyone reads it.
add_test(
        NAME rke_example_bonusclassicoption
        COMMAND rke_example_bonusclassicoption
)

# A bonus level at or below the barrier is refused before anything is priced. The test passes on
# the refusal's message, not on a failing exit code, which any other error would give.
add_test(
        NAME rke_example_bonusclassicoption_bonus_level_at_barrier
        COMMAND rke_example_bonusclassicoption --barrier 120 --bonus-level 120
)

set_tests_properties(rke_example_bonusclassicoption_bonus_level_at_barrier PROPERTIES
        PASS_REGULAR_EXPRESSION "--bonus-level 120 is not above --barrier 120"
)
