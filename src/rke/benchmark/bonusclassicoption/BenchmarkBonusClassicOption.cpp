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

        void BM_BonusClassicOptionMC(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep);
        }

        BENCHMARK(BM_BonusClassicOptionMC)
            ->Name("BonusClassicOptionMC")
            ->Unit(benchmark::kMillisecond)
            // Pins the count instead of letting google-benchmark scale to statistical stability,
            // trading the variance estimate for a predictable wall time; measure variance with
            // --benchmark_repetitions.
            ->Iterations(10);

        // The same pricing with the barrier monitored continuously, which adds a variance()
        // call, a logarithm and an exponential per step of every surviving path.
        void BM_BonusClassicOptionMCContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep);
        }

        BENCHMARK(BM_BonusClassicOptionMCContinuous)
            ->Name("BonusClassicOptionMCContinuous")
            ->Unit(benchmark::kMillisecond)
            // Pinned like BonusClassicOptionMC, so the two times compare directly.
            ->Iterations(10);

        // The two pricings above with QuantLib::SingleVariate, whose PathGenerator asks the
        // process for drift and diffusion at every step, as the baseline the step cache is
        // measured against. Same paths, same NPVs.
        void BM_BonusClassicOptionMCUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached);
        }

        BENCHMARK(BM_BonusClassicOptionMCUncached)
            ->Name("BonusClassicOptionMCUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);

        void BM_BonusClassicOptionMCContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached);
        }

        BENCHMARK(BM_BonusClassicOptionMCContinuousUncached)
            ->Name("BonusClassicOptionMCContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);

        // The four pricings above on the bilinear smile market, whose step is the Euler step
        // through LocalVolSurface, cached by LocalVolStepCache. Every step asks the surface for
        // a local volatility, so fewer iterations are pinned.
        void BM_BonusClassicOptionMCSmileBilinear(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBilinear)
            ->Name("BonusClassicOptionMCSmileBilinear")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionMCSmileBilinearContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBilinearContinuous)
            ->Name("BonusClassicOptionMCSmileBilinearContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionMCSmileBilinearUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBilinearUncached)
            ->Name("BonusClassicOptionMCSmileBilinearUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionMCSmileBilinearContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached,
                                        Market::SmileBilinear);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBilinearContinuousUncached)
            ->Name("BonusClassicOptionMCSmileBilinearContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        // The same four on the bicubic surface, which builds a strike spline on every lookup. A
        // repricing runs up to a minute, so a single iteration.
        void BM_BonusClassicOptionMCSmileBicubic(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBicubic)
            ->Name("BonusClassicOptionMCSmileBicubic")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionMCSmileBicubicContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBicubicContinuous)
            ->Name("BonusClassicOptionMCSmileBicubicContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionMCSmileBicubicUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::Uncached,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBicubicUncached)
            ->Name("BonusClassicOptionMCSmileBicubicUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BonusClassicOptionMCSmileBicubicContinuousUncached(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::Uncached,
                                        Market::SmileBicubic);
        }

        BENCHMARK(BM_BonusClassicOptionMCSmileBicubicContinuousUncached)
            ->Name("BonusClassicOptionMCSmileBicubicContinuousUncached")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        // The finite-difference engine on the same certificate and flat market, discretely
        // monitored on the Monte-Carlo grid and rolled back on fdTimeGrid x fdSpaceGrid, so
        // its time compares with BonusClassicOptionMC directly. --samples does not apply.
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

        // The two pricings above on the bilinear smile under the local volatility: every time
        // step asks LocalVolSurface for a volatility at every node, five surface lookups each,
        // so a pricing costs tens of milliseconds instead of a few. The price carries the kink
        // defect the engine's header warns about; the cost does not depend on it.
        void BM_BonusClassicOptionFdSmileBilinear(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBilinear, Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFdSmileBilinear)
            ->Name("BonusClassicOptionFdSmileBilinear")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);

        void BM_BonusClassicOptionFdSmileBilinearContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBilinear, Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFdSmileBilinearContinuous)
            ->Name("BonusClassicOptionFdSmileBilinearContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(10);

        // The same two on the bicubic surface, which builds a strike spline on every lookup: a
        // pricing takes a few hundred milliseconds.
        void BM_BonusClassicOptionFdSmileBicubic(benchmark::State& state) {
            benchmarkBonusClassicOption(state, true, PathGeneration::CachedStep,
                                        Market::SmileBicubic, Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFdSmileBicubic)
            ->Name("BonusClassicOptionFdSmileBicubic")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BonusClassicOptionFdSmileBicubicContinuous(benchmark::State& state) {
            benchmarkBonusClassicOption(state, false, PathGeneration::CachedStep,
                                        Market::SmileBicubic, Engine::FiniteDifference);
        }

        BENCHMARK(BM_BonusClassicOptionFdSmileBicubicContinuous)
            ->Name("BonusClassicOptionFdSmileBicubicContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

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
