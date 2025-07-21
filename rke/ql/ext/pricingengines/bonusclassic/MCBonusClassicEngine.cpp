//
// Created by ralf.eckel on 23.06.2025.
//

#include "MCBonusClassicEngine.hpp"
#include <iostream>

using namespace QuantLib;

namespace RKE::QL::External {
    BiasedBonusClassicPathPricer::BiasedBonusClassicPathPricer(Real barrier,
                                                               Real bonusLevel,
                                                               DiscountFactor discountFactor)
    : barrier_(barrier), bonusLevel_(bonusLevel), discountFactor_(discountFactor),
      payoff_(barrier, bonusLevel) {
        QL_REQUIRE(barrier_ > 0.0, "barrier less/equal zero not allowed");
        QL_REQUIRE(bonusLevel_ > 0.0, "bonus level less/equal zero not allowed");
    }

    Real BiasedBonusClassicPathPricer::operator()(const Path& path) const {
        Size n = path.length();
        QL_REQUIRE(n > 1, "the path cannot be empty");

        for (Size i = 1; i < n; i++) {
            auto assetPrice = path[i];
            if (assetPrice <= barrier_) {
                return path.back() * discountFactor_;
            }
        }
        return payoff_(path.back()) * discountFactor_;
    }
}