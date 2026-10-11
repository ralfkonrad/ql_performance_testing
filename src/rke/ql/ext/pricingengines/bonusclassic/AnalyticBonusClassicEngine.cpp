// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/AnalyticBonusClassicEngine.hpp>
#include <rke/ql/ext/processes/StrikeIndependentVolatility.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/pricingengines/barrier/analyticbinarybarrierengine.hpp>
#include <utility>

using namespace QuantLib;

namespace RKE::QL::Ext {
    AnalyticBonusClassicEngine::AnalyticBonusClassicEngine(
        ext::shared_ptr<GeneralizedBlackScholesProcess> process, bool calculateProbabilities)
    : process_(std::move(process)), calculateProbabilities_(calculateProbabilities) {
        QL_REQUIRE(process_, "null process given");
        // A variance curve or a smile would enter as the one Black volatility at (T, B); see
        // the class warning.
        QL_REQUIRE(hasConstantVolatility(*process_), "a constant volatility is required");
        // Without this, NPV() keeps returning the first price.
        registerWith(process_);
    }

    bool AnalyticBonusClassicEngine::calculatesProbabilities() const {
        return calculateProbabilities_;
    }

    void AnalyticBonusClassicEngine::calculate() const {
        const auto payoff = ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-bonus-classic payoff given");
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European,
                   "only european style option are supported");

        const auto spot = process_->x0();
        QL_REQUIRE(spot > 0.0, "negative or null underlying given");
        // QuantLib's down barrier is touched below it only, ours at it too.
        QL_REQUIRE(!triggered(spot), "barrier touched");

        const auto exerciseDate = arguments_.exercise->lastDate();

        auto downOutPut =
            BarrierOption(Barrier::DownOut, payoff->barrier(), 0.0,
                          ext::make_shared<PlainVanillaPayoff>(Option::Put, payoff->bonusLevel()),
                          ext::make_shared<EuropeanExercise>(exerciseDate));
        downOutPut.setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(process_));
        const auto assetLeg = spot * process_->dividendYield()->discount(exerciseDate);
        results_.value = assetLeg + downOutPut.NPV();

        if (!calculateProbabilities_) {
            return;
        }

        // AnalyticBinaryBarrierEngine takes an AmericanExercise paying at expiry whose first
        // date is at or before the volatility's reference date: a barrier watched from today.
        const auto exercise = ext::make_shared<AmericanExercise>(
            process_->blackVolatility()->referenceDate(), exerciseDate, true);
        const auto binaryEngine = ext::make_shared<AnalyticBinaryBarrierEngine>(process_);
        const auto downOutCashOrNothing = [&](Option::Type type) {
            auto option = BarrierOption(
                Barrier::DownOut, payoff->barrier(), 0.0,
                ext::make_shared<CashOrNothingPayoff>(type, payoff->bonusLevel(), 1.0), exercise);
            option.setPricingEngine(binaryEngine);
            return option.NPV();
        };

        // The curve is deterministic, so E[P_r(T) 1_A] = P_r(T) P(A). A path that never touches
        // the barrier ends above or below the bonus level, S_T = B having probability zero.
        const auto discount = process_->riskFreeRate()->discount(exerciseDate);
        const Real bonusProbability = downOutCashOrNothing(Option::Put) / discount;
        const Real aboveBonusProbability = downOutCashOrNothing(Option::Call) / discount;

        results_.additionalResults["barrierHitProbability"] =
            1.0 - (aboveBonusProbability + bonusProbability);
        results_.additionalResults["bonusProbability"] = bonusProbability;
    }
}
