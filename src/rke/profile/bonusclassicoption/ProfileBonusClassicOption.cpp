// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// The workload of the BM_BonusClassicOption* benchmarks, without the benchmark harness, for perf
// and valgrind.
//
//     rke_profile_bonusclassicoption [discrete|continuous] [iterations]
//                                    [--engine mc|fd|binomial]
//                                    [--path-generation cached|uncached]
//                                    [--market flat|smile-bilinear|smile-bicubic]
//                                    [--samples=<paths per pricing>]
//
// Defaults are discrete, 10, mc, cached, flat and 2^16 paths, about 20,000 samples under perf
// -F 999; one iteration is enough under callgrind, which counts instructions exactly, and
// fewer paths keep a smile market countable there, since Ir per step does not depend on them.
// fd and binomial price the flat market only, on rke_common's grids; --path-generation and
// --samples do not apply to them, and binomial monitors discretely only.

#include <rke/cli/SamplesOption.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <CLI/CLI.hpp>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>

using namespace RKE::Common;
using namespace QuantLib;

namespace RKE::Profile {
    namespace {
        struct Arguments {
            std::string monitoring = "discrete";
            Size iterations = 10;
            std::string engine = "mc";
            std::string pathGeneration = "cached";
            std::string market = "flat";
            Size samples = productionSamples;
        };

        // One table per option: CLI::IsMember validates against the keys, run() looks the value
        // up, so a name accepted here cannot mean something else there. Function-local, since a
        // namespace-scope map's initialization could throw before main().
        struct Tables {
            std::map<std::string, bool> monitorings = {
                {"discrete", true},
                {"continuous", false},
            };
            std::map<std::string, Engine> engines = {
                {"mc", Engine::MonteCarlo},
                {"fd", Engine::FiniteDifference},
                {"binomial", Engine::Binomial},
            };
            std::map<std::string, PathGeneration> pathGenerations = {
                {"cached", PathGeneration::CachedStep},
                {"uncached", PathGeneration::Uncached},
            };
            std::map<std::string, Market> markets = {
                {"flat", Market::Flat},
                {"smile-bilinear", Market::SmileBilinear},
                {"smile-bicubic", Market::SmileBicubic},
            };
        };

        const Tables& tables() {
            static const auto instance = Tables();
            return instance;
        }

        void addOptions(CLI::App& app, Arguments& arguments) {
            app.add_option("monitoring", arguments.monitoring, "Barrier monitoring")
                ->check(CLI::IsMember(&tables().monitorings))
                ->capture_default_str();
            app.add_option("iterations", arguments.iterations,
                           "Pricings, each of the same low-discrepancy paths")
                ->check(CLI::Range(Size{1}, std::numeric_limits<Size>::max()))
                ->capture_default_str();
            app.add_option("--engine", arguments.engine,
                           "Monte Carlo, finite differences or the binomial tree; the last two on "
                           "the flat market only, without --path-generation and --samples, and "
                           "the tree under discrete monitoring only")
                ->check(CLI::IsMember(&tables().engines))
                ->capture_default_str();
            app.add_option("--path-generation", arguments.pathGeneration,
                           "The market's step cache or QuantLib::SingleVariate")
                ->check(CLI::IsMember(&tables().pathGenerations))
                ->capture_default_str();
            app.add_option("--market", arguments.market,
                           "Flat curves and volatility, or zero curves and a smile surface")
                ->check(CLI::IsMember(&tables().markets))
                ->capture_default_str();
            RKE::Cli::addSamplesOption(app, arguments.samples);
        }

        void run(const Arguments& arguments) {
            // Built before the loop, as in the benchmark, so the profile shows pricing only.
            const auto engine = tables().engines.at(arguments.engine);
            const auto setup = makeBonusClassicOptionSetup(
                tables().monitorings.at(arguments.monitoring),
                tables().pathGenerations.at(arguments.pathGeneration),
                tables().markets.at(arguments.market), arguments.samples, engine);

            Real npv = Null<Real>();
            for (Size i = 0; i < arguments.iterations; ++i) {
                npv = reprice(*setup.option);
            }

            // The path generation and the paths are Monte-Carlo terms; the other engines report
            // the grid they priced on instead, the lattice after Boyle-Lau in particular.
            const auto isMonteCarlo = engine == Engine::MonteCarlo;
            const auto steps = monitoringSteps(setup);
            std::cout << arguments.engine << ", " << arguments.monitoring;
            if (isMonteCarlo) {
                std::cout << ", " << arguments.pathGeneration;
            }
            std::cout << ", " << arguments.market;
            if (isMonteCarlo) {
                std::cout << ", " << arguments.samples << " paths";
            }
            if (steps != Null<Size>()) {
                std::cout << ", " << steps << " steps";
            }
            std::cout << ", " << arguments.iterations << " iterations, NPV "
                      << std::setprecision(17) << npv << '\n';
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        auto app = CLI::App("Prices a BonusClassicOption repeatedly, for perf and valgrind.");
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
