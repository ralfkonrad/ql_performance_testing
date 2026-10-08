// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef FDBLACKSCHOLESBONUSCLASSICENGINE_HPP
#define FDBLACKSCHOLESBONUSCLASSICENGINE_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/processes/blackscholesprocess.hpp>

namespace RKE::QL::Ext {
    //! Finite-difference Black-Scholes engine for bonus certificates
    /*! Solves the Black-Scholes PDE in \f$ x = \ln S \f$ backwards from
        maturity \f$ T \f$,
        \f[
            \frac{\partial V}{\partial t}
            + \frac{\sigma^2}{2} \frac{\partial^2 V}{\partial x^2}
            + \left(r - q - \frac{\sigma^2}{2}\right) \frac{\partial V}{\partial x}
            - r V = 0,
        \f]
        from BonusClassicPayoff, averaged over each grid cell, with
        QuantLib::FdmBlackScholesSolver. A knocked-out certificate still
        delivers the asset, so at and below the barrier \f$ H \f$ its value is
        the asset leg
        \f[
            A(S, t) = S \, \frac{P_q(T)}{P_q(t)},
        \f]
        \f$ P_q \f$ being the discount factor of the dividend curve.

        The barrier is monitored continuously: the grid starts at \f$ H \f$
        and its first node carries \f$ A(H, t) \f$ as a Dirichlet condition.

        Time is the process's QuantLib::GeneralizedBlackScholesProcess::time(),
        i.e. the day counter of its risk-free curve. The PDE discounts on the
        risk-free curve; the asset leg reads the dividend curve at those times.
        Delta and gamma come from the solver's spline at the spot, theta from
        a snapshot of the rollback at \f$ 0.99 / 365 \f$.

        \warning \f$ P_q(t) \f$ reads \f$ t \f$ in the dividend curve's day
                 counter, the same approximation QuantLib's engines make;
                 it is exact when both curves share a day counter.
        \warning neither discrete dividends nor local volatility are
                 supported. Under a smile the operator reads the variance at
                 the bonus level.

        \ingroup barrierengines

        \test the continuously monitored value is checked against the asset
              plus a down-and-out put struck at the bonus level, priced with
              QuantLib::AnalyticBarrierEngine, and regression-locked for a
              fixed grid.
    */
    class FdBlackScholesBonusClassicEngine : public BonusClassicOption::engine {
      public:
        /*! \param process               the underlying; the engine registers
                                         with it
            \param monitoringStepsPerYear Null<Size>() monitors the barrier
                                         continuously; any other value is
                                         not implemented and fails at
                                         construction
            \param tGrid                 time steps of the rollback
            \param xGrid                 nodes of the log-spot grid
            \param dampingSteps          implicit Euler steps after maturity
            \param schemeDesc            the time-stepping scheme
        */
        explicit FdBlackScholesBonusClassicEngine(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::Size monitoringStepsPerYear = QuantLib::Null<QuantLib::Size>(),
            QuantLib::Size tGrid = 100,
            QuantLib::Size xGrid = 100,
            QuantLib::Size dampingSteps = 0,
            const QuantLib::FdmSchemeDesc& schemeDesc = QuantLib::FdmSchemeDesc::Douglas());

        void calculate() const override;

        //! whether the barrier is monitored continuously, i.e. no monitoring step was given
        [[nodiscard]] bool monitorsContinuously() const;

      private:
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        QuantLib::Size monitoringStepsPerYear_;
        QuantLib::Size tGrid_;
        QuantLib::Size xGrid_;
        QuantLib::Size dampingSteps_;
        QuantLib::FdmSchemeDesc schemeDesc_;
    };
}

#endif // FDBLACKSCHOLESBONUSCLASSICENGINE_HPP
