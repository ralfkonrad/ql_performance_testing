# SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

add_executable(rke_example_bonusclassicoption
        ExampleBonusClassicOption.cpp
)

target_link_libraries(rke_example_bonusclassicoption PRIVATE
        rke_common
        rke::warnings
)
