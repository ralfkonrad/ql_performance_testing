// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/DiscretizedBonusClassicOption.hpp>
#include <ql/math/array.hpp>
#include <cmath>
#include <vector>

using namespace QuantLib;

namespace RKE::QL::Ext {
    DiscretizedBonusClassicOption::DiscretizedBonusClassicOption(
        const BonusClassicOption::arguments& args,
        Leg leg,
        Rate riskFreeRate,
        Rate dividendYield,
        Time maturity)
    : payoff_(ext::dynamic_pointer_cast<BonusClassicPayoff>(args.payoff)), leg_(leg),
      riskFreeRate_(riskFreeRate), dividendYield_(dividendYield), maturity_(maturity) {
        QL_REQUIRE(payoff_, "non-bonus-classic payoff given");
    }

    void DiscretizedBonusClassicOption::reset(Size size) {
        values_ = Array(size, 0.0);
        adjustValues();
    }

    std::vector<Time> DiscretizedBonusClassicOption::mandatoryTimes() const {
        return {maturity_};
    }

    void DiscretizedBonusClassicOption::setValuesAtMaturity(const Array& spots) {
        // The payoff returns the asset at and below the barrier, so it monitors maturity itself;
        // the indicators test the barrier there the same inclusive way.
        const auto barrier = payoff_->barrier();
        const auto bonusLevel = payoff_->bonusLevel();
        for (Size j = 0; j < values_.size(); ++j) {
            switch (leg_) {
                case Leg::Certificate:
                    values_[j] = (*payoff_)(spots[j]);
                    break;
                case Leg::HitIndicator:
                    values_[j] = spots[j] <= barrier ? 1.0 : 0.0;
                    break;
                case Leg::BonusIndicator:
                    values_[j] = (barrier < spots[j] && spots[j] < bonusLevel) ? 1.0 : 0.0;
                    break;
            }
        }
    }

    void DiscretizedBonusClassicOption::postAdjustValuesImpl() {
        const Array spots = method()->grid(time());
        const auto barrier = payoff_->barrier();

        if (isOnTime(maturity_)) {
            setValuesAtMaturity(spots);
            return;
        }

        // What a node at or below the barrier is worth, the inclusive test being the payoff's and
        // BonusClassicOption::engine::triggered()'s: the asset delivered at maturity for the
        // certificate, a unit of cash at maturity discounted at the lattice's rate for the hit
        // indicator, and nothing for the bonus indicator. One loop per leg keeps the
        // certificate's, the one the benchmarks measure, free of a branch per node.
        const auto residual = maturity_ - time();
        switch (leg_) {
            case Leg::Certificate: {
                const auto growth = std::exp(-dividendYield_ * residual);
                for (Size j = 0; j < values_.size(); ++j) {
                    if (spots[j] <= barrier) {
                        values_[j] = spots[j] * growth;
                    }
                }
                break;
            }
            case Leg::HitIndicator: {
                const auto cash = std::exp(-riskFreeRate_ * residual);
                for (Size j = 0; j < values_.size(); ++j) {
                    if (spots[j] <= barrier) {
                        values_[j] = cash;
                    }
                }
                break;
            }
            case Leg::BonusIndicator:
                for (Size j = 0; j < values_.size(); ++j) {
                    if (spots[j] <= barrier) {
                        values_[j] = 0.0;
                    }
                }
                break;
        }
    }
}
