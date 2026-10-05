// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// The workload of the BM_BonusClassicOption* benchmarks, without the benchmark harness, for perf
// and valgrind.
//
//     rke_profile_bonusclassicoption [discrete|continuous] [iterations]
//                                    [--path-generation cached|uncached]
//                                    [--market flat|smile-bilinear|smile-bicubic]
//
// Defaults are discrete, 10, cached and flat, about 20,000 samples under perf -F 999; one
// iteration is enough under callgrind, which counts instructions exactly. The smile markets need
// uncached: the cached path generator refuses them.

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <CLI/CLI.hpp>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

using namespace RKE::Common;
using namespace QuantLib;

namespace RKE::Profile {
    namespace {
        struct Arguments {
            std::string monitoring = "discrete";
            Size iterations = 10;
            std::string pathGeneration = "cached";
            std::string market = "flat";
        };

        void addOptions(CLI::App& app, Arguments& arguments) {
            app.add_option("monitoring", arguments.monitoring, "Barrier monitoring")
                ->check(CLI::IsMember({"discrete", "continuous"}))
                ->capture_default_str();
            app.add_option("iterations", arguments.iterations,
                           "Pricings, each of the same low-discrepancy paths")
                ->check(CLI::Range(Size{1}, std::numeric_limits<Size>::max()))
                ->capture_default_str();
            app.add_option("--path-generation", arguments.pathGeneration,
                           "CachedStepSingleVariate or QuantLib::SingleVariate")
                ->check(CLI::IsMember({"cached", "uncached"}))
                ->capture_default_str();
            app.add_option("--market", arguments.market,
                           "Flat curves and volatility, or zero curves and a smile surface")
                ->check(CLI::IsMember({"flat", "smile-bilinear", "smile-bicubic"}))
                ->capture_default_str();
        }

        Market toMarket(const std::string& market) {
            if (market == "smile-bilinear") {
                return Market::SmileBilinear;
            }
            if (market == "smile-bicubic") {
                return Market::SmileBicubic;
            }
            return Market::Flat;
        }

        void run(const Arguments& arguments) {
            // Built before the loop, as in the benchmark, so the profile shows pricing only.
            const auto setup = makeBonusClassicOptionSetup(arguments.monitoring == "discrete",
                                                           arguments.pathGeneration == "cached" ?
                                                               PathGeneration::CachedStep :
                                                               PathGeneration::Uncached,
                                                           toMarket(arguments.market));

            Real npv = Null<Real>();
            for (Size i = 0; i < arguments.iterations; ++i) {
                npv = reprice(*setup.option);
            }

            std::cout << arguments.monitoring << ", " << arguments.pathGeneration << ", "
                      << arguments.market << ", " << arguments.iterations << " iterations, NPV "
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
