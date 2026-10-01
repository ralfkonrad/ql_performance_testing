// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "MCBonusClassicEngine.hpp"
#include <cmath>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    BiasedBonusClassicPathPricer::BiasedBonusClassicPathPricer(BonusClassicPayoff payoff,
                                                               DiscountFactor discountFactor)
    : payoff_(std::move(payoff)), discountFactor_(discountFactor) {
        QL_REQUIRE(payoff_.barrier() > 0.0, "barrier less/equal zero not allowed");
        QL_REQUIRE(payoff_.bonusLevel() > 0.0, "bonus level less/equal zero not allowed");
    }

    Real BiasedBonusClassicPathPricer::operator()(const Path& path) const {
        const Size n = path.length();
        QL_REQUIRE(n > 1, "the path cannot be empty");

        // t=0 is not a monitoring date and is checked once in the engine's calculate().
        for (Size i = 1; i < n; i++) {
            const auto assetPrice = path[i];
            if (assetPrice <= payoff_.barrier()) {
                return path.back() * discountFactor_;
            }
        }
        return payoff_(path.back()) * discountFactor_;
    }

    BonusClassicPathPricer::BonusClassicPathPricer(
        BonusClassicPayoff payoff,
        DiscountFactor discountFactor,
        ext::shared_ptr<GeneralizedBlackScholesProcess> process)
    : payoff_(std::move(payoff)), discountFactor_(discountFactor), process_(std::move(process)) {
        QL_REQUIRE(payoff_.barrier() > 0.0, "barrier less/equal zero not allowed");
        QL_REQUIRE(payoff_.bonusLevel() > 0.0, "bonus level less/equal zero not allowed");
        QL_REQUIRE(process_, "null process given");
    }

    Real BonusClassicPathPricer::operator()(const Path& path) const {
        const Size n = path.length();
        QL_REQUIRE(n > 1, "the path cannot be empty");

        const auto barrier = payoff_.barrier();
        // t=0 is not a monitoring date and is checked once in the engine's calculate(), so
        // path[0] > barrier and every log-distance below is positive.
        for (Size i = 1; i < n; i++) {
            if (path[i] <= barrier) {
                return path.back() * discountFactor_;
            }
        }

        const auto& grid = path.timeGrid();
        Real survival = 1.0;
        Real logDistance = std::log(path.front() / barrier);
        for (Size i = 0; i < n - 1; i++) {
            const auto nextLogDistance = std::log(path[i + 1] / barrier);
            // The integrated variance the path generator evolved this step with, so the
            // bridge matches the paths for flat and term-structured volatility alike.
            const auto variance = process_->variance(grid[i], path[i], grid.dt(i));
            if (variance > 0.0) {
                survival *= 1.0 - std::exp(-2.0 * logDistance * nextLogDistance / variance);
            }
            logDistance = nextLogDistance;
        }

        // S_T > barrier here, so payoff(S_T) - S_T is the bonus put max(B - S_T, 0).
        const auto finalPrice = path.back();
        const auto bonusPut = payoff_(finalPrice) - finalPrice;
        return (finalPrice + (survival * bonusPut)) * discountFactor_;
    }
}
