// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BONUSCLASSICOPTION_HPP
#define BONUSCLASSICOPTION_HPP

#include <ql/exercise.hpp>
#include <ql/instruments/oneassetoption.hpp>
#include <ql/instruments/payoffs.hpp>
#include <string>

namespace RKE::QL::Ext {
    //! Terminal payoff of a bonus certificate ("Bonus Classic")
    /*! Given the price \f$ S \f$ of the underlying at maturity, a barrier
        \f$ H \f$ and a bonus level \f$ B \f$, the payoff is
        \f[
            \begin{cases}
                S              & S \le H \\
                \max(S, B)     & S > H
            \end{cases}
        \f]
        The barrier is inclusive: a price equal to \f$ H \f$ counts as touched.

        The product itself is path-dependent; this class sees one price only.
        On its own it is the payoff of a certificate monitored at maturity
        alone. Path pricers apply it to the final price of a path that never
        touched the barrier, and pay the final price otherwise.

        The barrier is stored as the strike of the base class, whose option
        type is a placeholder (Option::Call) and carries no meaning.

        \warning \f$ B > H \f$ is not checked. With \f$ B \le H \f$ the bonus
                 never pays and the payoff collapses to \f$ S \f$.

        \ingroup instruments
    */
    class BonusClassicPayoff : public QuantLib::StrikedTypePayoff {
      public:
        BonusClassicPayoff(QuantLib::Real barrier, QuantLib::Real bonusLevel);

        //! the knock-out barrier \f$ H \f$, held as the base-class strike
        [[nodiscard]] QuantLib::Real barrier() const { return strike(); }
        //! the bonus level \f$ B \f$ paid as a floor while the barrier is intact
        [[nodiscard]] QuantLib::Real bonusLevel() const { return bonusLevel_; }

        //! \name Payoff interface
        //@{
        [[nodiscard]] std::string name() const override { return "BonusClassic"; }
        QuantLib::Real operator()(QuantLib::Real price) const override;
        void accept(QuantLib::AcyclicVisitor& acyclic_visitor) override;
        //@}


      private:
        QuantLib::Real bonusLevel_;
    };

    //! Bonus certificate on a single asset, European exercise
    /*! At maturity \f$ T \f$ the holder receives \f$ \max(S_T, B) \f$ if the
        underlying never traded at or below the barrier \f$ H \f$ during the
        life of the option, and \f$ S_T \f$ otherwise. Equivalently, the
        product is the asset plus a down-and-out put struck at \f$ B \f$ with
        barrier \f$ H \f$ and no rebate:
        \f[
            S_T + \mathbf{1}_{\{\min_{t \le T} S_t > H\}} \, (B - S_T)^+
        \f]
        European refers to the exercise only: the certificate pays once, at
        maturity. The barrier is monitored over the whole life of the option,
        and how often is up to the pricing engine.

        \warning no default engine is set; see MCBonusClassicEngine,
                 FdBlackScholesBonusClassicEngine and
                 BinomialBonusClassicEngine.

        \ingroup instruments

        \test the Monte Carlo value is checked against the replication above,
              priced with QuantLib::AnalyticBarrierEngine and the
              Broadie-Glasserman-Kou correction for discrete monitoring.
        \test the finite-difference value is checked against the same
              replication.
        \test the binomial value is checked against the same replication.
    */
    class BonusClassicOption : public QuantLib::OneAssetOption {
      public:
        class arguments;
        class engine;
        //! builds the payoff and a EuropeanExercise on \p exerciseDate
        BonusClassicOption(QuantLib::Real barrier,
                           QuantLib::Real bonusLevel,
                           QuantLib::Date exerciseDate);

        /*! \pre \p payoff is not null; QL_REQUIRE checks this. */
        BonusClassicOption(QuantLib::ext::shared_ptr<BonusClassicPayoff> payoff,
                           QuantLib::ext::shared_ptr<QuantLib::EuropeanExercise> exercise);

        void setupArguments(QuantLib::PricingEngine::arguments* arguments) const override;

        //! \name Inspectors
        //@{
        [[nodiscard]] QuantLib::Real barrier() const { return bonusClassicPayoff_->barrier(); }
        [[nodiscard]] QuantLib::Real bonusLevel() const {
            return bonusClassicPayoff_->bonusLevel();
        }
        //@}

      private:
        QuantLib::ext::shared_ptr<BonusClassicPayoff> bonusClassicPayoff_;
    };

    //! %Arguments for bonus certificate calculation
    /*! barrier and bonusLevel repeat the payoff's values so that an engine
        can read them without a cast. Both start at Null<Real>(), and
        validate() rejects them while they still are.
    */
    class BonusClassicOption::arguments : public OneAssetOption::arguments {
      public:
        arguments();
        QuantLib::Real barrier;
        QuantLib::Real bonusLevel;
        void validate() const override;
    };

    //! Bonus certificate engine base class
    class BonusClassicOption::engine : public QuantLib::GenericEngine<arguments, results> {
      protected:
        //! whether \p underlying is at or below the barrier, the same inclusive test as the payoff
        bool triggered(QuantLib::Real underlying) const;
    };
}

#endif // BONUSCLASSICOPTION_HPP
