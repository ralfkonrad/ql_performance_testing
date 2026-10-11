// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef ANALYTICBONUSCLASSICENGINE_HPP
#define ANALYTICBONUSCLASSICENGINE_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/processes/blackscholesprocess.hpp>

namespace RKE::QL::Ext {
    //! Analytic pricing engine for bonus certificates
    /*! Prices the certificate by its replication
        \f[
            S_T + \mathbf{1}_{\{\min_{t \le T} S_t > H\}} \, (B - S_T)^+,
        \f]
        the asset plus a down-and-out put struck at the bonus level
        \f$ B \f$ with barrier \f$ H \f$ and no rebate. The asset delivered at
        maturity is worth \f$ S_0 \, P_q(T) \f$, \f$ P_q \f$ being the
        discount factor of the dividend curve; QuantLib::AnalyticBarrierEngine
        prices the put. With \f$ B \le H \f$ the put never pays and the price
        is the asset leg alone.

        The barrier is monitored continuously, as QuantLib::AnalyticBarrierEngine
        assumes: the engine prices the product MCBonusClassicEngine prices
        under BarrierMonitoring::Continuous and FdBlackScholesBonusClassicEngine
        with Null<Size>() monitoring steps. QuantLib::AnalyticBarrierEngine
        reads the Black volatility at \f$ (T, B) \f$ on the volatility's day
        counter and the continuously compounded zero rates of the risk-free
        and dividend curves to \f$ T \f$, each on its curve's day counter.
        Only the value is set, no Greeks.

        With calculateProbabilities the engine also reports, in
        Instrument::additionalResults(), the probabilities under the
        process's measure that the barrier is hit and that the bonus is
        paid, read with Instrument::result<Real>("barrierHitProbability")
        and Instrument::result<Real>("bonusProbability"). Both come from
        cash-or-nothing options paying one unit at maturity, priced by
        QuantLib::AnalyticBinaryBarrierEngine and divided by the risk-free
        discount factor \f$ P_r(T) \f$: the bonus is paid iff the
        down-and-out put struck at \f$ B \f$ pays, and the barrier is hit iff
        neither that put nor the down-and-out call struck at \f$ B \f$ pays.

        \warning the closed forms hold for constant parameters. The
                 constructor refuses a volatility that is not constant;
                 curves that are not flat, or day counters that differ
                 between the curves and the volatility, are accepted and
                 enter as constant rates to maturity, an approximation for a
                 path-dependent payoff.
        \warning calculate() requires the spot above the barrier and throws
                 otherwise, by the inclusive test of the other engines.
                 QuantLib::BarrierOption's own test is strict, so
                 QuantLib::AnalyticBarrierEngine alone would price a spot on
                 the barrier.

        \ingroup barrierengines

        \test the value and both probabilities are regression-locked on the
              flat market.
        \test the value is checked to be the same double as the replication
              the test suite prices the other engines against, so the
              continuously monitored FdBlackScholesBonusClassicEngine and
              MCBonusClassicEngine are checked against it too, and the hit
              probability against the down-and-in options that replication's
              probabilities take, by in-out parity.
        \test with the barrier far below the spot the value is checked
              against the asset plus a vanilla put, and the bonus
              probability against a cash-or-nothing put, both priced by
              QuantLib::AnalyticEuropeanEngine.
        \test with \f$ B \le H \f$ the value is checked to be the asset leg
              and the bonus probability to vanish.
        \test the constructor is checked to refuse a variance curve, a smile
              and a smile given as an external local volatility, and to
              accept the flat market and a flat external local volatility.
    */
    class AnalyticBonusClassicEngine : public BonusClassicOption::engine {
      public:
        /*! \param process                the underlying; the engine registers
                                          with it
            \param calculateProbabilities whether calculate() also reports
                                          barrierHitProbability and
                                          bonusProbability, three more
                                          analytic prices; off by default
            \pre \p process is not null; QL_REQUIRE checks this.
            \pre the process's Black volatility is a QuantLib::BlackConstantVol
                 and its local volatility a QuantLib::LocalConstantVol, i.e.
                 no external local volatility of another type beside it;
                 QL_REQUIRE checks this with hasConstantVolatility().
        */
        explicit AnalyticBonusClassicEngine(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            bool calculateProbabilities = false);

        void calculate() const override;

        //! whether calculate() reports the two probabilities beside the price
        [[nodiscard]] bool calculatesProbabilities() const;

      private:
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        bool calculateProbabilities_;
    };
}

#endif // ANALYTICBONUSCLASSICENGINE_HPP
