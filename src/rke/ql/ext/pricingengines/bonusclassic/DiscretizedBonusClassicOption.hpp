// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef DISCRETIZEDBONUSCLASSICOPTION_HPP
#define DISCRETIZEDBONUSCLASSICOPTION_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/discretizedasset.hpp>
#include <ql/math/array.hpp>
#include <cstdint>
#include <vector>

namespace RKE::QL::Ext {
    //! Bonus certificate on a lattice, monitored on every lattice step
    /*! One of three legs, selected by Leg, on the same lattice: the
        certificate itself, or one of the two indicators whose discounted
        value over the lattice's discount factor from maturity is a
        probability under the lattice's measure.

        At maturity \f$ T \f$ every node takes a function of its underlying
        \f$ S_j \f$; on every other lattice time \f$ t \f$ the nodes at or
        below the barrier \f$ H \f$ take a knocked-out value and the nodes
        above \f$ H \f$ keep their rolled-back value:

        - Leg::Certificate: BonusClassicPayoff of \f$ S_j \f$ at maturity,
          which returns \f$ S_j \f$ at or below \f$ H \f$. A knocked-out
          certificate still delivers the asset, so the knocked-out value is
          the asset leg
          \f[
              A(S_j, t) = S_j \, e^{-q (T - t)},
          \f]
          \f$ q \f$ being the constant dividend yield the lattice was built
          from.
        - Leg::HitIndicator: \f$ \mathbf{1}_{\{S_j \le H\}} \f$ at maturity;
          a knocked-out node takes a unit of cash at maturity, discounted at
          the constant risk-free rate \f$ r \f$ the lattice discounts with,
          \f[
              e^{-r (T - t)}.
          \f]
          Rolled back to \f$ t = 0 \f$ this is \f$ e^{-r T} \, P(\text{hit}) \f$,
          the hit being a node at or below \f$ H \f$ on any lattice time.
        - Leg::BonusIndicator: \f$ \mathbf{1}_{\{H < S_j < B\}} \f$ at
          maturity, \f$ B \f$ being the bonus level; a knocked-out node takes
          \f$ 0 \f$. Rolled back to \f$ t = 0 \f$ this is
          \f$ e^{-r T} \, P(\text{never hit and } S_T < B) \f$, i.e. of the
          bonus being paid.

        The test at \f$ H \f$ is the inclusive one of the payoff and
        BonusClassicOption::engine.

        QuantLib::TreeLattice adjusts the asset after every step of a
        rollback, so the barrier is monitored on every lattice time,
        \f$ t = 0 \f$ included.
    */
    class DiscretizedBonusClassicOption : public QuantLib::DiscretizedAsset {
      public:
        //! which of the three legs above the asset rolls back
        enum class Leg : std::uint8_t { Certificate, HitIndicator, BonusIndicator };

        /*! \param leg           the leg to roll back
            \param riskFreeRate  the continuous rate the lattice discounts with
            \param dividendYield the continuous yield the lattice was built from
            \param maturity      the lattice time the leg is paid at
            \pre \p args carries a BonusClassicPayoff; QL_REQUIRE checks this.
        */
        DiscretizedBonusClassicOption(const BonusClassicOption::arguments& args,
                                      Leg leg,
                                      QuantLib::Rate riskFreeRate,
                                      QuantLib::Rate dividendYield,
                                      QuantLib::Time maturity);

        //! \name DiscretizedAsset interface
        //@{
        void reset(QuantLib::Size size) override;
        [[nodiscard]] std::vector<QuantLib::Time> mandatoryTimes() const override;
        //@}

      protected:
        void postAdjustValuesImpl() override;

      private:
        //! the leg's value at maturity on every node of \p spots
        void setValuesAtMaturity(const QuantLib::Array& spots);

        QuantLib::ext::shared_ptr<BonusClassicPayoff> payoff_;
        Leg leg_;
        QuantLib::Rate riskFreeRate_;
        QuantLib::Rate dividendYield_;
        QuantLib::Time maturity_;
    };
}

#endif // DISCRETIZEDBONUSCLASSICOPTION_HPP
