//
// Created by ralf.eckel on 23.06.2025.
//

#include "MCBonusClassicEngine.hpp"
#include <utility>

using namespace QuantLib;

namespace RKE::QL::External {
    BiasedBonusClassicPathPricer::BiasedBonusClassicPathPricer(BonusClassicPayoff payoff,
                                                               DiscountFactor discountFactor)
    : payoff_(std::move(payoff)), discountFactor_(discountFactor) {
        QL_REQUIRE(payoff_.barrier() > 0.0, "barrier less/equal zero not allowed");
        QL_REQUIRE(payoff_.bonusLevel() > 0.0, "bonus level less/equal zero not allowed");
    }

    Real BiasedBonusClassicPathPricer::operator()(const Path& path) const {
        Size const n = path.length();
        QL_REQUIRE(n > 1, "the path cannot be empty");

        for (Size i = 1; i < n; i++) {
            auto assetPrice = path[i];
            if (assetPrice <= payoff_.barrier()) {
                return path.back() * discountFactor_;
            }
        }
        return payoff_(path.back()) * discountFactor_;
    }
}