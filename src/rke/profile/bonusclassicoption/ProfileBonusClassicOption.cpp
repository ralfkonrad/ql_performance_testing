// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// The workload of the BM_BonusClassicOption* benchmarks, without the benchmark harness, for perf
// and valgrind.
//
//     rke_profile_bonusclassicoption --monitoring discrete|continuous --iterations <pricings>
//                                    --engine mc|fd|binomial|analytic
//                                    --market flat|smile-bilinear|smile-bicubic
//                                    --path-generation cached|uncached      (mc only)
//                                    --samples=<paths per pricing>          (mc only)
//
// No option has a default. The first four are always required; the last two are required with
// mc and rejected by the other engines, which read neither. Ten iterations of the discrete,
// cached flat pricing at 2^16 paths are about 20,000 samples under perf -F 999; one iteration
// is enough under callgrind, which counts instructions exactly, and fewer paths keep a smile
// market countable there, since Ir per step does not depend on them. fd prices every market on
// rke_common's grids, the smiles under the local volatility; binomial prices the flat market
// only and monitors discretely only; analytic prices the flat market only and monitors
// continuously only.

#include <rke/cli/SamplesOption.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <ql/errors.hpp>
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
            std::string engine;
            std::string market;
            std::string pathGeneration;
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
            std::map<std::string, Engine> engines = {
                {"mc", Engine::MonteCarlo},
                {"fd", Engine::FiniteDifference},
                {"binomial", Engine::Binomial},
                {"analytic", Engine::Analytic},
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

        const Choices& choices() {
            static const auto instance = Choices();
            return instance;
        }

        void addOptions(CLI::App& app, Arguments& arguments) {
            app.add_option("--monitoring", arguments.monitoring, "Barrier monitoring")
                ->required()
                ->check(CLI::IsMember(&choices().monitorings));
            app.add_option("--iterations", arguments.iterations,
                           "Pricings, each of the same low-discrepancy paths")
                ->required()
                ->check(CLI::Range(Size{1}, std::numeric_limits<Size>::max()));
            app.add_option("--engine", arguments.engine,
                           "Monte Carlo, finite differences, the binomial tree or the closed "
                           "form; the last three without --path-generation and --samples, the "
                           "tree on the flat market under discrete monitoring only, the closed "
                           "form on the flat market under continuous monitoring only")
                ->required()
                ->check(CLI::IsMember(&choices().engines));
            app.add_option("--market", arguments.market,
                           "Flat curves and volatility, or zero curves and a smile surface")
                ->required()
                ->check(CLI::IsMember(&choices().markets));
            const auto* pathGeneration =
                app.add_option("--path-generation", arguments.pathGeneration,
                               "The market's step cache or QuantLib::SingleVariate; required "
                               "with --engine mc")
                    ->check(CLI::IsMember(&choices().pathGenerations));
            const auto* samples = RKE::Cli::addSamplesOption(app, arguments.samples)
                                      ->description("Paths per pricing; required with --engine mc");

            // Read by the Monte-Carlo engine alone: required with it and rejected with the
            // others, so no run takes a default or carries an argument it ignores. The final
            // callback runs after CLI11 has checked the required options and the --engine value.
            app.final_callback([&arguments, pathGeneration, samples] {
                const auto isMonteCarlo =
                    choices().engines.at(arguments.engine) == Engine::MonteCarlo;
                for (const auto* option : {pathGeneration, samples}) {
                    QL_REQUIRE(!isMonteCarlo || option->count() > 0,
                               option->get_name() << " is required with --engine mc");
                    QL_REQUIRE(isMonteCarlo || option->count() == 0,
                               option->get_name()
                                   << " applies to --engine mc only, not " << arguments.engine);
                }
            });
        }

        void run(const Arguments& arguments) {
            // Built before the loop, as in the benchmark, so the profile shows pricing only.
            const auto engine = choices().engines.at(arguments.engine);
            const auto isMonteCarlo = engine == Engine::MonteCarlo;
            // The command line leaves both unset for the other engines, which read neither; the
            // setup takes them all the same.
            const auto pathGeneration = isMonteCarlo ?
                                            choices().pathGenerations.at(arguments.pathGeneration) :
                                            PathGeneration::CachedStep;
            const auto setup =
                makeBonusClassicOptionSetup(OptionData(), FlatMarketData(), SmileMarketData(),
                                            choices().monitorings.at(arguments.monitoring),
                                            pathGeneration, choices().markets.at(arguments.market),
                                            arguments.samples, engine, ProbabilityCalculation::Off);

            Real npv = Null<Real>();
            for (Size i = 0; i < arguments.iterations; ++i) {
                npv = reprice(*setup.option);
            }

            // The path generation and the paths are Monte-Carlo terms; the other engines report
            // the grid they priced on instead, the lattice after Boyle-Lau in particular.
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
