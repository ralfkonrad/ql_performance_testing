// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// The workload of BM_BonusClassicOption and BM_BonusClassicOptionContinuous, without the
// benchmark harness, for perf and valgrind.
//
//     rke_profile_bonusclassicoption [discrete|continuous] [iterations]
//
// Defaults are discrete and 10: enough samples for perf, and one iteration is enough under
// callgrind, which counts instructions exactly.

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <ql/errors.hpp>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace RKE::Common;
using namespace QuantLib;

namespace RKE::Profile {
    namespace {
        struct Arguments {
            bool isBiased = true;
            Size iterations = 10;
        };

        Arguments parseArguments(const std::vector<std::string>& args) {
            QL_REQUIRE(args.size() <= 2, "usage: [discrete|continuous] [iterations]");

            auto result = Arguments();
            if (!args.empty()) {
                QL_REQUIRE(args[0] == "discrete" || args[0] == "continuous",
                           "unknown monitoring '" << args[0]
                                                  << "', expected discrete or continuous");
                result.isBiased = args[0] == "discrete";
            }
            if (args.size() == 2) {
                // Read signed, so that "-1" is rejected instead of wrapping around.
                auto stream = std::istringstream(args[1]);
                std::int64_t iterations = 0;
                stream >> iterations;
                QL_REQUIRE(stream && stream.eof() && iterations >= 1,
                           "iterations must be a positive integer, got '" << args[1] << "'");
                result.iterations = static_cast<Size>(iterations);
            }
            return result;
        }

        void run(const Arguments& arguments) {
            // Built before the loop, as in the benchmark, so the profile shows pricing only.
            const auto setup = makeBonusClassicOptionSetup(arguments.isBiased);

            Real npv = Null<Real>();
            for (Size i = 0; i < arguments.iterations; ++i) {
                npv = reprice(*setup.option);
            }

            std::cout << (arguments.isBiased ? "discrete" : "continuous") << ", "
                      << arguments.iterations << " iterations, NPV " << std::setprecision(17) << npv
                      << '\n';
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv is argc long
        const auto args = std::vector<std::string>(argv + 1, argv + argc);
        RKE::Profile::run(RKE::Profile::parseArguments(args));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
