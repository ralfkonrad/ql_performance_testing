// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "LocalVolStepCache.hpp"
#include <array>
#include <cmath>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    LocalVolStepCache::LocalVolStepCache(
        const ext::shared_ptr<GeneralizedBlackScholesProcess>& process, const TimeGrid& grid) {
        QL_REQUIRE(process, "null process given");

        // The link the process evaluates: LocalVolSurface under a smile, LocalConstantVol or
        // LocalVolCurve under a forced discretization, or the external one it was given.
        localVolatility_ = process->localVolatility().currentLink();

        const auto steps = grid.size() - 1;
        time_.reserve(steps);
        dt_.reserve(steps);
        sqrtDt_.reserve(steps);
        rateDrift_.reserve(steps);
        for (Size i = 0; i < steps; ++i) {
            const auto t = grid[i];
            // Same calls and arguments as GeneralizedBlackScholesProcess::drift().
            const auto t1 = t + 0.0001;
            time_.push_back(t);
            dt_.push_back(grid.dt(i));
            sqrtDt_.push_back(std::sqrt(grid.dt(i)));
            rateDrift_.push_back(
                process->riskFreeRate()->forwardRate(t, t1, Continuous, NoFrequency, true).rate() -
                process->dividendYield()->forwardRate(t, t1, Continuous, NoFrequency, true).rate());
        }

        // Arbitrary points away from zero, one on each side of the mean.
        const auto x0 = process->x0();
        constexpr std::array<std::pair<Real, Real>, 2> probes{{{1.0, 0.7}, {0.8, -1.3}}};
        for (Size i = 0; i < steps; ++i) {
            for (const auto& [scale, dw] : probes) {
                const auto x = scale * x0;
                const auto expected = process->evolve(grid[i], x, grid.dt(i), dw);
                if (std::fabs(evolve(i, x, dw) - expected) > stepTolerance * std::fabs(expected)) {
                    time_.clear();
                    dt_.clear();
                    sqrtDt_.clear();
                    rateDrift_.clear();
                    return;
                }
            }
        }
        reproducesEvolve_ = true;
    }
}
