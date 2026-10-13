// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/cli/SamplesOption.hpp>
#include <rke/common/BarrierOptionSetup.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <CLI/CLI.hpp>
#include <benchmark/benchmark.h>
#include <exception>
#include <iostream>

using namespace RKE::Common;
using namespace QuantLib;
using RKE::QL::Ext::BarrierMonitoring;

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

        // The setup and the loop body come from rke_common, so a profile executable that prices
        // this workload measures exactly what is timed here. Nothing of rke_ql_ext runs in the
        // loop: the engine, the path generator and both path pricers are QuantLib's.
        void benchmarkBarrierOption(benchmark::State& state,
                                    BarrierMonitoring monitoring,
                                    Market market) {
            const auto setup =
                makeBarrierOptionSetup(BarrierOptionData(), FlatMarketData(), SmileMarketData(),
                                       monitoring, market, samples);

            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                auto npv = reprice(*setup.option);
                benchmark::DoNotOptimize(npv);
            }
        }

        // The certificate's put leg on the flat market, monitored discretely:
        // BiasedBarrierPathPricer compares each point after t = 0 with the barrier and makes no
        // call into the process, so the work it adds per step to the path generator's exact
        // step is one comparison.
        void BM_BarrierOptionMC(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Discrete, Market::Flat);
        }

        BENCHMARK(BM_BarrierOptionMC)
            ->Name("BarrierOptionMC")
            ->Unit(benchmark::kMillisecond)
            // Pins the count instead of letting google-benchmark scale to statistical stability,
            // trading the variance estimate for a predictable wall time; measure variance with
            // --benchmark_repetitions.
            ->Iterations(10);

        // The same pricing monitored continuously, by BarrierPathPricer: on every step of every
        // path, knocked out or not, it calls the process's diffusion() at the step's start and
        // draws the Brownian-bridge minimum from a uniform of its own MersenneTwister, through
        // two logarithms, a square root and an exponential. On the flat market diffusion()
        // returns LocalConstantVol's quote, and the generator's exact step makes no such call.
        void BM_BarrierOptionMCContinuous(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Continuous, Market::Flat);
        }

        BENCHMARK(BM_BarrierOptionMCContinuous)
            ->Name("BarrierOptionMCContinuous")
            ->Unit(benchmark::kMillisecond)
            // Pinned like BarrierOptionMC, so the two times compare directly.
            ->Iterations(10);

        // The two pricings above on the bilinear smile market, whose paths take the Euler step
        // through LocalVolSurface: the step's drift and its standard deviation each ask the
        // surface for a local volatility. Under continuous monitoring diffusion() is a third
        // lookup per step, at the (t, x) the step itself has evaluated twice. Every step asks the
        // surface, so fewer iterations are pinned.
        void BM_BarrierOptionMCSmileBilinear(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Discrete, Market::SmileBilinear);
        }

        BENCHMARK(BM_BarrierOptionMCSmileBilinear)
            ->Name("BarrierOptionMCSmileBilinear")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        void BM_BarrierOptionMCSmileBilinearContinuous(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Continuous, Market::SmileBilinear);
        }

        BENCHMARK(BM_BarrierOptionMCSmileBilinearContinuous)
            ->Name("BarrierOptionMCSmileBilinearContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(3);

        // The same two on the bicubic surface, which builds a strike spline on every lookup, so
        // a single iteration.
        void BM_BarrierOptionMCSmileBicubic(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Discrete, Market::SmileBicubic);
        }

        BENCHMARK(BM_BarrierOptionMCSmileBicubic)
            ->Name("BarrierOptionMCSmileBicubic")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);

        void BM_BarrierOptionMCSmileBicubicContinuous(benchmark::State& state) {
            benchmarkBarrierOption(state, BarrierMonitoring::Continuous, Market::SmileBicubic);
        }

        BENCHMARK(BM_BarrierOptionMCSmileBicubicContinuous)
            ->Name("BarrierOptionMCSmileBicubicContinuous")
            ->Unit(benchmark::kMillisecond)
            ->Iterations(1);
    }
}

// BENCHMARK_MAIN() with a flag of its own: benchmark::Initialize consumes the --benchmark_*
// flags and leaves the rest of argv to CLI11.
int main(int argc, char* argv[]) {
    try {
        benchmark::MaybeReenterWithoutASLR(argc, argv);
        benchmark::Initialize(&argc, argv, RKE::Benchmark::printHelp);

        auto app = CLI::App("Times BarrierOption pricings with google-benchmark.");
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
