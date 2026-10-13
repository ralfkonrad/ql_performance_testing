// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// The workload of the BM_BarrierOption* benchmarks, without the benchmark harness, for perf and
// valgrind.
//
//     rke_profile_barrieroption --monitoring discrete|continuous --iterations <pricings>
//                               --market flat|smile-bilinear|smile-bicubic
//                               --samples=<paths per pricing>
//
// No option has a default, and all four are required. The engine is always
// QuantLib::MCBarrierEngine on QuantLib::PathGenerator, so the measured loop runs QuantLib alone,
// apart from reprice() and the Joe-Kuo D7 trait's factory, called once per pricing. Continuous
// monitoring is the mode that reaches BarrierPathPricer, which asks the process for diffusion()
// on every step of every path, the call docs/quantlib-mc-step-precomputation-upstream.md proposes
// to take from a table; discrete monitoring prices through BiasedBarrierPathPricer, which never
// calls the process. One iteration is enough under callgrind, which counts instructions exactly.

#include <rke/cli/SamplesOption.hpp>
#include <rke/common/BarrierOptionSetup.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <ql/utilities/null.hpp>
#include <CLI/CLI.hpp>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>

using namespace RKE::Common;
using namespace QuantLib;
using RKE::QL::Ext::BarrierMonitoring;

namespace RKE::Profile {
    namespace {
        // No defaults: every member is set from the command line before run() reads it, the
        // zeros being values the range checks refuse.
        struct Arguments {
            std::string monitoring;
            Size iterations = 0;
            std::string market;
            Size samples = 0;
        };

        // One map of choices per option: CLI::IsMember validates against the keys, run() looks the
        // value up, so a name accepted here cannot mean something else there. Function-local, since
        // a namespace-scope map's initialization could throw before main().
        struct Choices {
            std::map<std::string, BarrierMonitoring> monitorings = {
                {"discrete", BarrierMonitoring::Discrete},
                {"continuous", BarrierMonitoring::Continuous},
            };
            std::map<std::string, Market> markets = {
                {"flat", Market::Flat},
                {"smile-bilinear", Market::SmileBilinear},
                {"smile-bicubic", Market::SmileBicubic},
            };
        };

        const Choices& choices() {
            static const auto instance = Choices();
            return instance;
        }

        void addOptions(CLI::App& app, Arguments& arguments) {
            app.add_option("--monitoring", arguments.monitoring,
                           "Barrier monitoring: on the grid points, or continuously through the "
                           "Brownian bridge")
                ->required()
                ->check(CLI::IsMember(&choices().monitorings));
            app.add_option("--iterations", arguments.iterations,
                           "Pricings, each of the same low-discrepancy paths")
                ->required()
                ->check(CLI::Range(Size{1}, std::numeric_limits<Size>::max()));
            app.add_option("--market", arguments.market,
                           "Flat curves and volatility, or zero curves and a smile surface")
                ->required()
                ->check(CLI::IsMember(&choices().markets));
            RKE::Cli::addSamplesOption(app, arguments.samples)
                ->required()
                ->description("Paths per pricing, 65536 in a production run");
        }

        void run(const Arguments& arguments) {
            // Built before the loop, so the profile shows pricing only. Each pricing builds its
            // path generator and path pricer anew in McSimulation::calculate, the Sobol sequence
            // and the continuous pricer's MersenneTwister restarting from their seeds, so every
            // iteration prices the same paths to the same NPV.
            const auto setup =
                makeBarrierOptionSetup(BarrierOptionData(), FlatMarketData(), SmileMarketData(),
                                       choices().monitorings.at(arguments.monitoring),
                                       choices().markets.at(arguments.market), arguments.samples);

            Real npv = Null<Real>();
            for (Size i = 0; i < arguments.iterations; ++i) {
                npv = reprice(*setup.option);
            }

            // The certificate profile's Monte-Carlo line without its path generation, which is
            // always QuantLib::PathGenerator here.
            std::cout << "mc, " << arguments.monitoring << ", " << arguments.market << ", "
                      << arguments.samples << " paths, " << arguments.iterations
                      << " iterations, NPV " << std::setprecision(17) << npv << '\n';
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        auto app =
            CLI::App("Prices a down-and-out put BarrierOption repeatedly, for perf and valgrind.");
        auto arguments = RKE::Profile::Arguments();
        RKE::Profile::addOptions(app, arguments);
        // Returns from main on a parse error or --help, with CLI11's exit code.
        CLI11_PARSE(app, argc, argv);

        RKE::Profile::run(arguments);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
