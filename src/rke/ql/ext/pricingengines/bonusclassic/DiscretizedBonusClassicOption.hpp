// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef DISCRETIZEDBONUSCLASSICOPTION_HPP
#define DISCRETIZEDBONUSCLASSICOPTION_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/discretizedasset.hpp>
#include <vector>

namespace RKE::QL::Ext {
    //! Bonus certificate on a lattice, monitored on every lattice step
    /*! At maturity \f$ T \f$ every node takes BonusClassicPayoff of its
        underlying \f$ S_j \f$, which returns \f$ S_j \f$ at or below the
        barrier \f$ H \f$. On every other lattice time \f$ t \f$ a knocked-out
        certificate still delivers the asset, so the nodes at or below
        \f$ H \f$ take the asset leg
        \f[
            A(S_j, t) = S_j \, e^{-q (T - t)},
        \f]
        \f$ q \f$ being the constant dividend yield the lattice was built
        from, and the nodes above \f$ H \f$ keep their rolled-back value. The
        test is the inclusive one of the payoff and
        BonusClassicOption::engine.

        QuantLib::TreeLattice adjusts the asset after every step of a
        rollback, so the barrier is monitored on every lattice time,
        \f$ t = 0 \f$ included.
    */
    class DiscretizedBonusClassicOption : public QuantLib::DiscretizedAsset {
      public:
        /*! \param dividendYield the continuous yield the lattice was built from
            \param maturity      the lattice time the asset is delivered at
            \pre \p args carries a BonusClassicPayoff; QL_REQUIRE checks this.
        */
        DiscretizedBonusClassicOption(const BonusClassicOption::arguments& args,
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
        QuantLib::ext::shared_ptr<BonusClassicPayoff> payoff_;
        QuantLib::Rate dividendYield_;
        QuantLib::Time maturity_;
    };
}

#endif // DISCRETIZEDBONUSCLASSICOPTION_HPP
