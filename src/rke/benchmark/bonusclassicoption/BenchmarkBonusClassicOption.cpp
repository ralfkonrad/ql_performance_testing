// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/cli/SamplesOption.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <CLI/CLI.hpp>
#include <benchmark/benchmark.h>
#include <exception>
#include <iostream>

using namespace RKE::Common;
using namespace QuantLib;

namespace RKE::Benchmark {
    namespace {
        // Paths per pricing: productionSamples unless --samples sets another, which the CTest
        // smoke test does so that its dry run takes seconds, not minutes.
        Size samples = productionSamples;

        // google-benchmark's usage, which ends the process on --help before CLI11 sees it, plus
        // the flag added here.
        void printHelp() {
            benchmark::PrintDefaultHelp();
            std::cout << "          [--samples=<paths per pricing>]\n";
        }

        // The setup and the loop body come from rke_common, which the profile executable uses
        // too, so a hotspot found there is a hotspot here.
        void benchmarkBonusClassicOption(benchmark::State& state,
                                         bool isBiased,
                                         PathGeneration pathGeneration,
                                         Market market = Market::Flat,
                                         Engine engine = Engine::MonteCarlo) {
            const auto setup =
                makeBonusClassicOptionSetup(isBiased, pathGeneration, market, samples, engine);

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

        // The four pricings above on the bilinear smile market, whose step is the Euler step
        // through LocalVolSurface, cached by LocalVolStepCache. Every step asks the surface for
        // a local volatility, so fewer iterations are pinned.
        void BM_BonusClassicOptionSmileBilinear(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBilinear)
            ->Name("BonusClassicOptionSmileBilinear")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionSmileBilinearContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBilinearContinuous)
            ->Name("BonusClassicOptionSmileBilinearContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionSmileBilinearUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBilinearUncached)
            ->Name("BonusClassicOptionSmileBilinearUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionSmileBilinearContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBilinearContinuousUncached)
            ->Name("BonusClassicOptionSmileBilinearContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        // The same four on the bicubic surface, which builds a strike spline on every lookup. A
        // repricing runs up to a minute, so a single iteration.
        void BM_BonusClassicOptionSmileBicubic(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBicubic)
            ->Name("BonusClassicOptionSmileBicubic")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionSmileBicubicContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBicubicContinuous)
            ->Name("BonusClassicOptionSmileBicubicContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionSmileBicubicUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBicubicUncached)
            ->Name("BonusClassicOptionSmileBicubicUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionSmileBicubicContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionSmileBicubicContinuousUncached)
            ->Name("BonusClassicOptionSmileBicubicContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        // The finite-difference engine on the same certificate and flat market, discretely
        // monitored on the Monte-Carlo grid and rolled back on fdTimeGrid x fdSpaceGrid, so
        // its time compares with BonusClassicOption directly. --samples does not apply.
        void BM_BonusClassicOptionFd(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep, Market::Flat,
                                        Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFd)
            ->Name("BonusClassicOptionFd")
            ->Unit(benchmark::kMillisecond)
            // A pricing takes milliseconds, so the pin is higher than the Monte-Carlo ones for
            // a comparable wall time.
            ->Iterations(100);

        // Continuous monitoring: the knock-out becomes a Dirichlet boundary at the barrier and
        // the step conditions on the monitoring dates go away.
        void BM_BonusClassicOptionFdContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep, Market::Flat,
                                        Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFdContinuous)
            ->Name("BonusClassicOptionFdContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(100);

        // The binomial engine, treeTimeSteps Cox-Ross-Rubinstein steps raised by Boyle-Lau,
        // which monitors on every step, so there is no continuous variant; the cheapest of the
        // three.
        void BM_BonusClassicOptionBinomial(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep, Market::Flat,
                                        Engine::Binomial);
        }

        BENCHMARK(BM_BonusClassicOptionBinomial)
            ->Name("BonusClassicOptionBinomial")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1000);
    }
}

// BENCHMARK_MAIN() with a flag of its own: benchmark::Initialize consumes the --benchmark_*
// flags and leaves the rest of argv to CLI11.
int main(int argc, char* argv[]) {
    try {
        benchmark::MaybeReenterWithoutASLR(argc, argv);
        benchmark::Initialize(&argc, argv, RKE::Benchmark::printHelp);

        auto app = CLI::App("Times BonusClassicOption pricings with google-benchmark.");
        RKE::Cli::addSamplesOption(app, RKE::Benchmark::samples);
        // Returns from main on a parse error, with CLI11's exit code.
        CLI11_PARSE(app, argc, argv);

        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
