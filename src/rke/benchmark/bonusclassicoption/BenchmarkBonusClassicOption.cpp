// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <benchmark/benchmark.h>

using namespace RKE::Common;

namespace RKE::Benchmark {
    namespace {
        // The setup and the loop body come from rke_common, which the profile executable uses
        // too, so a hotspot found there is a hotspot here.
        void benchmarkBonusClassicOption(benchmark::State& state,
                                         bool isBiased,
                                         PathGeneration pathGeneration) {
            const auto setup = makeBonusClassicOptionSetup(isBiased, pathGeneration);

            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                auto npv = reprice(*setup.option);
                benchmark::DoNotOptimize(npv);
            }
        }

        void BM_BonusClassicOption(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep);
        }

        BENCHMARK(BM_BonusClassicOption)
            ->Name("BonusClassicOption")
            ->Unit(benchmark::kMillisecond)
            // Pins the count instead of letting google-benchmark scale to statistical stability,
            // trading the variance estimate for a predictable wall time; measure variance with
            // --benchmark_repetitions.
            ->Iterations(10);

        // The same pricing with the barrier monitored continuously, which adds a variance()
        // call, a logarithm and an exponential per step of every surviving path.
        void BM_BonusClassicOptionContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep);
        }

        BENCHMARK(BM_BonusClassicOptionContinuous)
            ->Name("BonusClassicOptionContinuous")
            ->Unit(benchmark::kMillisecond)
            // Pinned like BonusClassicOption, so the two times compare directly.
            ->Iterations(10);

        // The two pricings above with QuantLib::SingleVariate, whose PathGenerator asks the
        // process for drift and diffusion at every step, as the baseline the step cache is
        // measured against. Same paths, same NPVs.
        void BM_BonusClassicOptionUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached);
        }

        BENCHMARK(BM_BonusClassicOptionUncached)
            ->Name("BonusClassicOptionUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);

        void BM_BonusClassicOptionContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached);
        }

        BENCHMARK(BM_BonusClassicOptionContinuousUncached)
            ->Name("BonusClassicOptionContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);
    }
}

BENCHMARK_MAIN();
