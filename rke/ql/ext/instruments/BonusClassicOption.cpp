//
// Created by ralf.eckel on 21.06.2025.
//

#include "BonusClassicOption.hpp"
#include "ql/exercise.hpp"

using namespace QuantLib;

namespace RKE::QL::External {
    BonusClassicOption::BonusClassicOption(Real barrier,
                                           Real bonusLevel,
                                           const ext::shared_ptr<StrikedTypePayoff>& payoff,
                                           const ext::shared_ptr<EuropeanExercise>& exercise)
    : OneAssetOption(payoff, exercise), barrier_(barrier), bonusLevel_(bonusLevel) {}

    void BonusClassicOption::setupArguments(PricingEngine::arguments* arguments) const {
        OneAssetOption::setupArguments(arguments);

        auto* moreArgs = dynamic_cast<BonusClassicOption::arguments*>(arguments);
        QL_REQUIRE(moreArgs != nullptr, "wrong argument type");
        moreArgs->barrier = barrier_;
        moreArgs->bonusLevel = bonusLevel_;
    }

    BonusClassicOption::arguments::arguments() : barrier(Null<Real>()), bonusLevel(Null<Real>()) {}

    void BonusClassicOption::arguments::validate() const {
        OneAssetOption::arguments::validate();

        QL_REQUIRE(barrier != Null<Real>(), "no barrier given");
        QL_REQUIRE(bonusLevel != Null<Real>(), "no bonus level given");
    }
} // namespace RKE::QL::External
