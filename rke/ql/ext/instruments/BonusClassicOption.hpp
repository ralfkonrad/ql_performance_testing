//
// Created by ralf.eckel on 21.06.2025.
//

#ifndef BONUSCLASSICOPTION_HPP
#define BONUSCLASSICOPTION_HPP

#include <ql/exercise.hpp>
#include <ql/instruments/oneassetoption.hpp>
#include <ql/instruments/payoffs.hpp>

namespace RKE::QL::External {
    class BonusClassicOption : public QuantLib::OneAssetOption {
      public:
        class arguments;
        class engine;
        BonusClassicOption(QuantLib::Real barrier,
                           QuantLib::Real bonusLevel,
                           const QuantLib::ext::shared_ptr<QuantLib::StrikedTypePayoff>& payoff,
                           const QuantLib::ext::shared_ptr<QuantLib::EuropeanExercise>& exercise);

        void setupArguments(QuantLib::PricingEngine::arguments*) const override;

        QuantLib::Real barrier() const { return barrier_; };
        QuantLib::Real bonusLevel() const { return bonusLevel_; };

      private:
        QuantLib::Real barrier_;
        QuantLib::Real bonusLevel_;
    };

    //! %Arguments for barrier option calculation
    class BonusClassicOption::arguments : public OneAssetOption::arguments {
      public:
        arguments();
        QuantLib::Real barrier;
        QuantLib::Real bonusLevel;
        void validate() const override;
    };

    //! %Barrier-option %engine base class
    class BonusClassicOption::engine : public QuantLib::GenericEngine<arguments, results> {
      protected:
        bool triggered(QuantLib::Real underlying) const;
    };
} // namespace RKE::QL::External

#endif // BONUSCLASSICOPTION_HPP
