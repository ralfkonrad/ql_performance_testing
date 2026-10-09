// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "MCBonusClassicEngine.hpp"
#include <cmath>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    namespace {
        // The Brownian-bridge survival probability P = prod_i (1 - p_i) of a path no grid point
        // of which is at or below the barrier, over the steps whose integrated variance
        // stepVariance(i, path) supplies. The caller has tested the grid points, and t = 0 the
        // engine's calculate(), so every log-distance below is positive.
        template <class StepVariance>
        Real continuousSurvival(const Path& path, Real barrier, const StepVariance& stepVariance) {
            const Size n = path.length();

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
            return survival;
        }
    }

    BonusClassicPathPricerBase::BonusClassicPathPricerBase(BonusClassicPayoff payoff,
                                                           DiscountFactor discountFactor)
    : payoff_(std::move(payoff)), discountFactor_(discountFactor) {}

    Real BonusClassicPathPricerBase::barrierHitProbability() const {
        QL_REQUIRE(samples_ > 0, "no path priced yet");
        return hitSum_ / static_cast<Real>(samples_);
    }

    Real BonusClassicPathPricerBase::bonusProbability() const {
        QL_REQUIRE(samples_ > 0, "no path priced yet");
        return bonusSum_ / static_cast<Real>(samples_);
    }

    void BonusClassicPathPricerBase::record(Real hitProbability, Real bonusProbability) const {
        hitSum_ += hitProbability;
        bonusSum_ += bonusProbability;
        ++samples_;
    }

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

    Real BonusClassicPathPricerBase::knockedOutValue(const Path& path) const {
        record(1.0, 0.0);
        return path.back() * discountFactor_;
    }

    Real BonusClassicPathPricerBase::continuousValue(const Path& path, Real survival) const {
        // S_T > barrier here, so payoff(S_T) - S_T is the bonus put max(B - S_T, 0), and the
        // surviving fraction of the path is paid the bonus iff that put is in the money.
        const auto finalPrice = path.back();
        const auto bonusPut = payoff_(finalPrice) - finalPrice;
        record(1.0 - survival, finalPrice < payoff_.bonusLevel() ? survival : 0.0);
        // Same operations in the same order as before the probabilities: the regression locks
        // pin this double.
        return (finalPrice + (survival * bonusPut)) * discountFactor_;
    }

    BiasedBonusClassicPathPricer::BiasedBonusClassicPathPricer(BonusClassicPayoff payoff,
                                                               DiscountFactor discountFactor)
    : BonusClassicPathPricerBase(std::move(payoff), discountFactor) {}

    Real BiasedBonusClassicPathPricer::operator()(const Path& path) const {
        if (knockedOut(path)) {
            return knockedOutValue(path);
        }
        const auto finalPrice = path.back();
        record(0.0, finalPrice < payoff_.bonusLevel() ? 1.0 : 0.0);
        return payoff_(finalPrice) * discountFactor_;
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
            return knockedOutValue(path);
        }
        // The integrated variance the path generator evolved this step with, so the bridge
        // matches the paths for flat and term-structured volatility alike.
        const auto survival =
            continuousSurvival(path, payoff_.barrier(), [this](Size i, const Path& p) {
                const auto& grid = p.timeGrid();
                return process_->variance(grid[i], p[i], grid.dt(i));
            });
        return continuousValue(path, survival);
    }

    CachedStepBonusClassicPathPricer::CachedStepBonusClassicPathPricer(
        BonusClassicPayoff payoff,
        DiscountFactor discountFactor,
        ext::shared_ptr<const BlackScholesStepCache> stepCache)
    : BonusClassicPathPricerBase(std::move(payoff), discountFactor),
      stepCache_(std::move(stepCache)) {
        QL_REQUIRE(stepCache_, "null step cache given");
        QL_REQUIRE(stepCache_->reproducesEvolve(),
                   "the process's step on this grid is not the exact "
                   "Black-Scholes step; use BonusClassicPathPricer");
    }

    Real CachedStepBonusClassicPathPricer::operator()(const Path& path) const {
        if (knockedOut(path)) {
            return knockedOutValue(path);
        }
        QL_REQUIRE(stepCache_->size() == path.length() - 1, "path has " << path.length() - 1
                                                                        << " steps, the step cache "
                                                                        << stepCache_->size());
        const auto survival =
            continuousSurvival(path, payoff_.barrier(),
                               [this](Size i, const Path&) { return stepCache_->variance(i); });
        return continuousValue(path, survival);
    }
}
