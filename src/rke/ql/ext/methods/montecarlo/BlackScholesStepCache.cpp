// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BlackScholesStepCache.hpp"
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancecurve.hpp>
#include <array>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    namespace {
        // The types GeneralizedBlackScholesProcess::localVolatility() treats as
        // strike-independent.
        bool hasStrikeIndependentVolatility(const GeneralizedBlackScholesProcess& process) {
            const auto& vol = *process.blackVolatility();
            return ext::dynamic_pointer_cast<BlackConstantVol>(vol) != nullptr ||
                   ext::dynamic_pointer_cast<BlackVarianceCurve>(vol) != nullptr;
        }
    }

    BlackScholesStepCache::BlackScholesStepCache(
        const ext::shared_ptr<GeneralizedBlackScholesProcess>& process, const TimeGrid& grid) {
        QL_REQUIRE(process, "null process given");
        if (!hasStrikeIndependentVolatility(*process)) {
            return;
        }

        const auto x0 = process->x0();
        const auto steps = grid.size() - 1;
        variance_.reserve(steps);
        stdDeviation_.reserve(steps);
        drift_.reserve(steps);
        for (Size i = 0; i < steps; ++i) {
            const auto t0 = grid[i];
            const auto dt = grid.dt(i);
            // Same calls, arguments and evaluation order as
            // GeneralizedBlackScholesProcess::evolve().
            const auto var = process->variance(t0, x0, dt);
            const auto drift = ((process->riskFreeRate()
                                     ->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true)
                                     .rate() -
                                 process->dividendYield()
                                     ->forwardRate(t0, t0 + dt, Continuous, NoFrequency, true)
                                     .rate()) *
                                dt) -
                               (0.5 * var);
            variance_.push_back(var);
            stdDeviation_.push_back(std::sqrt(var));
            drift_.push_back(drift);
        }

        // Arbitrary points away from zero, one on each side of the mean.
        constexpr std::array<std::pair<Real, Real>, 2> probes{{{1.0, 0.7}, {0.8, -1.3}}};
        for (Size i = 0; i < steps; ++i) {
            for (const auto& [scale, dw] : probes) {
                const auto x = scale * x0;
                // Bitwise on purpose: a step that is only close would move the regression locks.
                if (evolve(i, x, dw) != process->evolve(grid[i], x, grid.dt(i), dw)) {
                    variance_.clear();
                    stdDeviation_.clear();
                    drift_.clear();
                    return;
                }
            }
        }
        reproducesEvolve_ = true;
    }
}
