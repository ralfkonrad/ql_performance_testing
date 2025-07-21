//
// Created by ralf.eckel on 21.06.2025.
//

#ifndef BONUSCLASSICOPTION_HPP
#define BONUSCLASSICOPTION_HPP

#include <ql/exercise.hpp>
#include <ql/instruments/oneassetoption.hpp>
#include <ql/instruments/payoffs.hpp>

namespace RKE::QL::External {
    class BonusClassicPayoff : public QuantLib::StrikedTypePayoff {
      public:
        BonusClassicPayoff(QuantLib::Real barrier, QuantLib::Real bonusLevel);

        [[nodiscard]] QuantLib::Real barrier() const { return strike(); }
        [[nodiscard]] QuantLib::Real bonusLevel() const { return bonusLevel_; }

        [[nodiscard]] std::string name() const override { return "BonusClassic"; }
        QuantLib::Real operator()(QuantLib::Real price) const override;
        void accept(QuantLib::AcyclicVisitor&) override;


      private:
        QuantLib::Real bonusLevel_;
    };

    class BonusClassicOption : public QuantLib::OneAssetOption {
      public:
        class arguments;
        class engine;
        BonusClassicOption(QuantLib::Real barrier,
                           QuantLib::Real bonusLevel,
                           QuantLib::Date exerciseDate);

        BonusClassicOption(QuantLib::ext::shared_ptr<BonusClassicPayoff> payoff,
                           QuantLib::ext::shared_ptr<QuantLib::EuropeanExercise> exercise);

        void setupArguments(QuantLib::PricingEngine::arguments*) const override;

        [[nodiscard]] QuantLib::Real barrier() const { return bonusClassicPayoff_->barrier(); }
        [[nodiscard]] QuantLib::Real bonusLevel() const {
            return bonusClassicPayoff_->bonusLevel();
        }

      private:
        QuantLib::ext::shared_ptr<BonusClassicPayoff> bonusClassicPayoff_;
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
