// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "MCBonusClassicEngine.hpp"
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
}