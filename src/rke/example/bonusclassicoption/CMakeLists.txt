# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_example_bonusclassicoption
        ExampleBonusClassicOption.cpp
)

target_link_libraries(rke_example_bonusclassicoption PRIVATE
        rke_common
        rke::warnings
)

# A smoke test, not a check of the printed differences: one run of the example, so CI catches
# an example that throws before anyone reads it.
add_test(
        NAME rke_example_bonusclassicoption
        COMMAND rke_example_bonusclassicoption
)
