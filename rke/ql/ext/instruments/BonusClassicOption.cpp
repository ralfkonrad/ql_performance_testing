//
// Created by ralf.eckel on 21.06.2025.
//

#include <rke/ql/ext/Error.hpp>
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>

using namespace QuantLib;

namespace RKE::QL::External {
    BonusClassicPayoff::BonusClassicPayoff(Real barrier, Real bonusLevel)
    : StrikedTypePayoff(Option::Call, barrier), bonusLevel_(bonusLevel) {}

    Real BonusClassicPayoff::operator()(Real price) const {
        if (price <= barrier())
            return price;
        return std::max(price, bonusLevel());
    }

    void BonusClassicPayoff::accept(AcyclicVisitor& acyclic_visitor) {
        StrikedTypePayoff::accept(acyclic_visitor);
    }


    BonusClassicOption::BonusClassicOption(Real barrier, Real bonusLevel, Date exerciseDate)
    : BonusClassicOption(ext::make_shared<BonusClassicPayoff>(barrier, bonusLevel),
                         ext::make_shared<EuropeanExercise>(exerciseDate)) {}

    BonusClassicOption::BonusClassicOption(ext::shared_ptr<BonusClassicPayoff> payoff,
                                           ext::shared_ptr<EuropeanExercise> exercise)
    : OneAssetOption(std::move(payoff), std::move(exercise)) {
        bonusClassicPayoff_ = ext::dynamic_pointer_cast<BonusClassicPayoff>(payoff_);
        QL_REQUIRE(bonusClassicPayoff_ != nullptr, "BonusClassicPayoff_ is null");
    }

    void BonusClassicOption::setupArguments(PricingEngine::arguments* arguments) const {
        OneAssetOption::setupArguments(arguments);

        auto* moreArgs = dynamic_cast<BonusClassicOption::arguments*>(arguments);
        QL_REQUIRE(moreArgs != nullptr, "wrong argument type");
        moreArgs->barrier = barrier();
        moreArgs->bonusLevel = bonusLevel();
    }

    BonusClassicOption::arguments::arguments() : barrier(Null<Real>()), bonusLevel(Null<Real>()) {}

    void BonusClassicOption::arguments::validate() const {
        OneAssetOption::arguments::validate();

        QL_REQUIRE(barrier != Null<Real>(), "no barrier given");
        QL_REQUIRE(bonusLevel != Null<Real>(), "no bonus level given");
    }
} // namespace RKE::QL::External
