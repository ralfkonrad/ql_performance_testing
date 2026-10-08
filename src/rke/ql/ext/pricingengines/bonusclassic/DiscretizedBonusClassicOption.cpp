// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/DiscretizedBonusClassicOption.hpp>
#include <ql/math/array.hpp>
#include <cmath>
#include <vector>

using namespace QuantLib;

namespace RKE::QL::Ext {
    DiscretizedBonusClassicOption::DiscretizedBonusClassicOption(
        const BonusClassicOption::arguments& args, Rate dividendYield, Time maturity)
    : payoff_(ext::dynamic_pointer_cast<BonusClassicPayoff>(args.payoff)),
      dividendYield_(dividendYield), maturity_(maturity) {
        QL_REQUIRE(payoff_, "non-bonus-classic payoff given");
    }

    void DiscretizedBonusClassicOption::reset(Size size) {
        values_ = Array(size, 0.0);
        adjustValues();
    }

    std::vector<Time> DiscretizedBonusClassicOption::mandatoryTimes() const {
        return {maturity_};
    }

    void DiscretizedBonusClassicOption::postAdjustValuesImpl() {
        const Array spots = method()->grid(time());

        // The payoff returns the asset at and below the barrier, so it monitors maturity itself.
        if (isOnTime(maturity_)) {
            for (Size j = 0; j < values_.size(); ++j) {
                values_[j] = (*payoff_)(spots[j]);
            }
            return;
        }

        const auto growth = std::exp(-dividendYield_ * (maturity_ - time()));
        const auto barrier = payoff_->barrier();
        for (Size j = 0; j < values_.size(); ++j) {
            // The inclusive test of the payoff and BonusClassicOption::engine::triggered().
            if (spots[j] <= barrier) {
                values_[j] = spots[j] * growth;
            }
        }
    }
}
