# SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
# SPDX-License-Identifier: MIT

# One subdirectory per workload, each building rke_benchmark_<workload> from a single
# file that defines, registers and runs its benchmarks.
add_subdirectory(bonusclassicoption)

add_subdirectory(calendar)

add_subdirectory(rng)
