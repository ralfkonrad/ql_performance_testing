// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef FDBLACKSCHOLESBONUSCLASSICENGINE_HPP
#define FDBLACKSCHOLESBONUSCLASSICENGINE_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/math/array.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmesher.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/methods/finitedifferences/stepcondition.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/timegrid.hpp>
#include <vector>

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
        \f$ P_q \f$ being the discount factor of the dividend curve. The
        lowest node of the grid carries \f$ A \f$ as a Dirichlet condition.

        monitoringStepsPerYear selects how the barrier is monitored:

        - Null<Size>(): continuously. The grid starts at \f$ H \f$ and is
          uniform above it.
        - any other value: on the points after \f$ t = 0 \f$ of timeGrid(),
          maturity included, the grid MCBonusClassicEngine monitors with
          isBiased = true and the same timeStepsPerYear. The grid spans
          QuantLib::FdmBlackScholesMesher's range around the bonus level,
          concentrated at the barrier and the bonus level, with one node
          exactly on \f$ \ln H \f$. FdmBonusClassicKnockOutCondition knocks
          the certificate out on each monitoring time before maturity, and
          the cell-averaged payoff at maturity. Between monitoring times the
          whole grid evolves under the PDE.

        Time is the process's QuantLib::GeneralizedBlackScholesProcess::time(),
        i.e. the day counter of its risk-free curve. The PDE discounts on the
        risk-free curve; the asset leg reads the dividend curve at those times.
        Delta and gamma come from the solver's spline at the spot, theta from
        a snapshot of the rollback at \f$ 0.99 / 365 \f$.

        \warning discrete monitoring converges at second order in the grid
                 spacing only because a node sits on the barrier and takes
                 the average of its cell there; a barrier between nodes moves
                 by up to a cell.
        \warning with discrete monitoring each knock-out re-creates a jump at
                 the barrier, which Crank-Nicolson, i.e. QuantLib's default
                 Douglas scheme in one dimension, damps only slowly: its
                 prices then converge erratically in time. The default here
                 is therefore QuantLib::FdmSchemeDesc::TrBDF2(), which is
                 L-stable and converges at second order there.
        \warning calculate() requires the spot above the barrier in either
                 mode and throws otherwise. With discrete monitoring that is
                 a restriction, not a knock-out: \f$ t = 0 \f$ is no
                 monitoring time, so a certificate alive today with its spot
                 at or below the barrier has a price, which this engine does
                 not compute.
        \warning with discrete monitoring the barrier has to lie inside
                 QuantLib::FdmBlackScholesMesher's range; calculate() fails
                 otherwise.
        \warning \f$ P_q(t) \f$ reads \f$ t \f$ in the dividend curve's day
                 counter, the same approximation QuantLib's engines make;
                 it is exact when both curves share a day counter.
        \warning neither discrete dividends nor local volatility are
                 supported. Under a smile the operator reads the variance at
                 the bonus level.

        \ingroup barrierengines

        \test the continuously monitored value is checked against the asset
              plus a down-and-out put struck at the bonus level, priced with
              QuantLib::AnalyticBarrierEngine.
        \test the discretely monitored value is checked against the same
              replication at the barrier corrected after Broadie, Glasserman
              and Kou (1997), and against MCBonusClassicEngine with
              isBiased = true on the same monitoring grid.
        \test both values are regression-locked for a fixed grid.
        \test the continuously monitored value is checked to lie below the
              discretely monitored one.
    */
    class FdBlackScholesBonusClassicEngine : public BonusClassicOption::engine {
      public:
        /*! \param process               the underlying; the engine registers
                                         with it
            \param monitoringStepsPerYear Null<Size>() monitors the barrier
                                         continuously; any other value on
                                         max(floor(monitoringStepsPerYear * T), 1)
                                         equal steps up to maturity, as
                                         MCBonusClassicEngine with
                                         isBiased = true
            \param tGrid                 time steps of the rollback
            \param xGrid                 nodes of the log-spot grid
            \param dampingSteps          implicit Euler steps after maturity
            \param schemeDesc            the time-stepping scheme; TrBDF2 by
                                         default, not QuantLib's Douglas, see
                                         the warning above
            \pre \p process is not null; QL_REQUIRE checks this.
        */
        explicit FdBlackScholesBonusClassicEngine(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::Size monitoringStepsPerYear = QuantLib::Null<QuantLib::Size>(),
            QuantLib::Size tGrid = 100,
            QuantLib::Size xGrid = 100,
            QuantLib::Size dampingSteps = 0,
            const QuantLib::FdmSchemeDesc& schemeDesc = QuantLib::FdmSchemeDesc::TrBDF2());

        void calculate() const override;

        //! the monitoring grid; QL_FAIL when the barrier is monitored continuously
        /*! \pre the instrument has set up the arguments, i.e. NPV() has run once;
                 QL_REQUIRE checks this. */
        [[nodiscard]] QuantLib::TimeGrid timeGrid() const;
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

    //! Discrete knock-out of a bonus certificate on a log-spot grid
    /*! On each monitoring time \f$ t \f$ the nodes below the barrier take the
        asset leg \f$ A(S_i, t) = S_i \, P_q(T) / P_q(t) \f$, and the node
        \f$ k \f$ on the barrier the average over its cell, knocked out below
        \f$ \ln H \f$ and alive above:
        \f[
            a_k \leftarrow w \, A(S_k, t) + (1 - w) \, a_k, \qquad
            w = \frac{\Delta x_k^-}{\Delta x_k^- + \Delta x_k^+}.
        \f]
        Nodes above the barrier are left alone. A node on the barrier taking
        one side's value would move the effective barrier by half a cell.

        The finite-difference model calls applyTo() after every time step, so
        the condition acts on its own monitoring times only, compared
        exactly, and on each of them once.

        \pre one inner node of \p mesher lies on \f$ \ln H \f$; QL_REQUIRE
             checks this.
    */
    class FdmBonusClassicKnockOutCondition : public QuantLib::StepCondition<QuantLib::Array> {
      public:
        /*! \param monitoringTimes times in the process's measure, maturity
                                   excluded
            \param maturity        the time the asset is delivered at
            \param dividendYield   the curve \f$ P_q \f$ is read from
        */
        FdmBonusClassicKnockOutCondition(
            const QuantLib::ext::shared_ptr<QuantLib::FdmMesher>& mesher,
            std::vector<QuantLib::Time> monitoringTimes,
            QuantLib::Real barrier,
            QuantLib::Time maturity,
            QuantLib::ext::shared_ptr<QuantLib::YieldTermStructure> dividendYield);

        void applyTo(QuantLib::Array& a, QuantLib::Time t) const override;

      private:
        QuantLib::Array spots_;
        std::vector<QuantLib::Time> monitoringTimes_;
        QuantLib::Size barrierIndex_ = 0;
        QuantLib::Real weight_ = 0.0;
        QuantLib::Time maturity_;
        QuantLib::ext::shared_ptr<QuantLib::YieldTermStructure> dividendYield_;
        mutable QuantLib::Time lastApplied_ = QuantLib::Null<QuantLib::Time>();
    };
}

#endif // FDBLACKSCHOLESBONUSCLASSICENGINE_HPP
