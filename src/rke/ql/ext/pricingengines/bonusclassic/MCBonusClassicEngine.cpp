// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "MCBonusClassicEngine.hpp"
#include <cmath>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    namespace {
        // The continuously monitored value of a path no grid point of which is at or below the
        // barrier: S_T plus the bonus put weighted with the Brownian-bridge survival probability
        // over the steps, whose integrated variance stepVariance(i, path) supplies. The caller
        // has tested the grid points, and t = 0 the engine's calculate(), so every log-distance
        // below is positive.
        template <class StepVariance>
        Real continuousValue(const Path& path,
                             const BonusClassicPayoff& payoff,
                             DiscountFactor discountFactor,
                             const StepVariance& stepVariance) {
            const Size n = path.length();
            const auto barrier = payoff.barrier();

            Real survival = 1.0;
            Real logDistance = std::log(path.front() / barrier);
            for (Size i = 0; i < n - 1; i++) {
                const auto nextLogDistance = std::log(path[i + 1] / barrier);
                const auto variance = stepVariance(i, path);
                if (variance > 0.0) {
                    survival *= 1.0 - std::exp(-2.0 * logDistance * nextLogDistance / variance);
                }
                logDistance = nextLogDistance;
            }

            // S_T > barrier here, so payoff(S_T) - S_T is the bonus put max(B - S_T, 0).
            const auto finalPrice = path.back();
            const auto bonusPut = payoff(finalPrice) - finalPrice;
            return (finalPrice + (survival * bonusPut)) * discountFactor;
        }
    }

    BonusClassicPathPricerBase::BonusClassicPathPricerBase(BonusClassicPayoff payoff,
                                                           DiscountFactor discountFactor)
    : payoff_(std::move(payoff)), discountFactor_(discountFactor) {}

    bool BonusClassicPathPricerBase::knockedOut(const Path& path) const {
        const Size n = path.length();
        QL_REQUIRE(n > 1, "the path cannot be empty");

        // t=0 is not a monitoring date and is checked once in the engine's calculate().
        const auto barrier = payoff_.barrier();
        for (Size i = 1; i < n; i++) {
            if (path[i] <= barrier) {
                return true;
            }
        }
        return false;
    }

    BiasedBonusClassicPathPricer::BiasedBonusClassicPathPricer(BonusClassicPayoff payoff,
                                                               DiscountFactor discountFactor)
    : BonusClassicPathPricerBase(std::move(payoff), discountFactor) {}

    Real BiasedBonusClassicPathPricer::operator()(const Path& path) const {
        if (knockedOut(path)) {
            return path.back() * discountFactor_;
        }
        return payoff_(path.back()) * discountFactor_;
    }

    BonusClassicPathPricer::BonusClassicPathPricer(
        BonusClassicPayoff payoff,
        DiscountFactor discountFactor,
        ext::shared_ptr<GeneralizedBlackScholesProcess> process)
    : BonusClassicPathPricerBase(std::move(payoff), discountFactor), process_(std::move(process)) {
        QL_REQUIRE(process_, "null process given");
    }

    Real BonusClassicPathPricer::operator()(const Path& path) const {
        if (knockedOut(path)) {
            return path.back() * discountFactor_;
        }
        // The integrated variance the path generator evolved this step with, so the bridge
        // matches the paths for flat and term-structured volatility alike.
        return continuousValue(path, payoff_, discountFactor_, [this](Size i, const Path& p) {
            const auto& grid = p.timeGrid();
            return process_->variance(grid[i], p[i], grid.dt(i));
        });
    }

    CachedStepBonusClassicPathPricer::CachedStepBonusClassicPathPricer(
        BonusClassicPayoff payoff,
        DiscountFactor discountFactor,
        const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
        const TimeGrid& grid)
    : BonusClassicPathPricerBase(std::move(payoff), discountFactor), stepCache_(process, grid) {
        QL_REQUIRE(stepCache_.reproducesEvolve(),
                   "the process's step on this grid is not the exact "
                   "Black-Scholes step; use BonusClassicPathPricer");
    }

    Real CachedStepBonusClassicPathPricer::operator()(const Path& path) const {
        if (knockedOut(path)) {
            return path.back() * discountFactor_;
        }
        QL_REQUIRE(stepCache_.size() == path.length() - 1, "path has " << path.length() - 1
                                                                       << " steps, the step cache "
                                                                       << stepCache_.size());
        return continuousValue(path, payoff_, discountFactor_,
                               [this](Size i, const Path&) { return stepCache_.variance(i); });
    }
}
