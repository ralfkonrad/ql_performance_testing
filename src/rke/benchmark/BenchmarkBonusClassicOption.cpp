// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BenchmarkBonusClassicOption.hpp"
#include <rke/common/BonusClassicOptionSetup.hpp>

using namespace RKE::Common;

namespace RKE::Benchmark {
    namespace {
        // The setup and the loop body come from rke_common, which the profile executable uses
        // too, so a hotspot found there is a hotspot here.
        void benchmarkBonusClassicOption(benchmark::State& state, bool isBiased) {
            const auto setup = makeBonusClassicOptionSetup(isBiased);

            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                auto npv = reprice(*setup.option);
                benchmark::DoNotOptimize(npv);
            }
        }
    }

    void BM_BonusClassicOption(benchmark::State& state) {
        benchmarkBonusClassicOption(state, true);
    }

    // The same pricing with the barrier monitored continuously, which adds a variance() call,
    // a logarithm and an exponential per step of every surviving path.
    void BM_BonusClassicOptionContinuous(benchmark::State& state) {
        benchmarkBonusClassicOption(state, false);
    }
}
