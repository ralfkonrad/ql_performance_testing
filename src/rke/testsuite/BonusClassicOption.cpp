// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/math/matrix.hpp>
#include <ql/methods/montecarlo/mctraits.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/processes/eulerdiscretization.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <test-suite/utilities.hpp>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::TestSuite {
    // Barrier monitoring dates of the MC engine are its time grid points.
    constexpr Size mcTimeStepsPerYear = 100;

    namespace {
        struct OptionData {
            Real barrier = 90.0;
            Real bonusLevel = 120.00;
            Period ttm = Period(5, Months);
        };

        struct MarketData {
            Real spot = 100.00;
            Real riskfreeRate = 0.01;
            Real dividendYield = 0.03;
            Real volatility = 0.20;

            // forceDiscretization = true makes evolve() take Euler steps over the flat
            // volatility, which BlackScholesStepCache cannot reproduce.
            ext::shared_ptr<GeneralizedBlackScholesProcess>
            makeGeneralizedBlackScholesProcess(Date today, bool forceDiscretization = false) {
                const auto dc = Actual360();
                const auto spotQuote = ext::make_shared<SimpleQuote>(spot);

                const auto qH_SME = ext::make_shared<SimpleQuote>(dividendYield);
                const auto qTS = flatRate(today, qH_SME, dc);

                const auto rH_SME = ext::make_shared<SimpleQuote>(riskfreeRate);
                const auto rTS = flatRate(today, rH_SME, dc);

                const auto volaQuote = ext::make_shared<SimpleQuote>(volatility);
                const auto volTS = flatVol(today, volaQuote, dc);

                return ext::make_shared<BlackScholesMertonProcess>(
                    Handle<Quote>(spotQuote), Handle(qTS), Handle(rTS), Handle(volTS),
                    ext::make_shared<EulerDiscretization>(), forceDiscretization);
            }
        };

        // The Euler step through LocalVolSurface: zero curves linear between the nodes,
        // continuously compounded, and a bilinear Black variance surface with
        // sigma(K) = 0.20 - 0.08 ln(K / 100) on every date. The strikes reach far beyond any
        // path, so the flat strike extrapolation's kink, which LocalVolSurface turns into a
        // negative local variance, is never reached.
        ext::shared_ptr<GeneralizedBlackScholesProcess> makeSmileProcess(Date today) {
            const auto dc = Actual360();
            const std::vector<Date> curveDates = {
                today,
                today + Period(3, Months),
                today + Period(1, Years),
                today + Period(2, Years),
            };
            const auto rTS = Handle<YieldTermStructure>(ext::make_shared<ZeroCurve>(
                curveDates, std::vector<Rate>{0.008, 0.009, 0.010, 0.012}, dc, NullCalendar()));
            const auto qTS = Handle<YieldTermStructure>(ext::make_shared<ZeroCurve>(
                curveDates, std::vector<Rate>{0.032, 0.031, 0.030, 0.028}, dc, NullCalendar()));

            const std::vector<Date> volDates = {
                today + Period(1, Months),
                today + Period(6, Months),
                today + Period(1, Years),
            };
            const std::vector<Real> strikes = {
                10.0, 25.0, 50.0, 70.0, 85.0, 100.0, 115.0, 130.0, 160.0, 220.0, 400.0,
            };
            // Rows are strikes, columns dates.
            Matrix vols(strikes.size(), volDates.size());
            for (Size i = 0; i < strikes.size(); ++i) {
                for (Size j = 0; j < volDates.size(); ++j) {
                    vols(i, j) = 0.20 - (0.08 * std::log(strikes[i] / 100.0));
                }
            }
            const auto volTS = Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceSurface>(
                today, NullCalendar(), volDates, strikes, vols, dc,
                BlackVarianceSurface::ConstantExtrapolation,
                BlackVarianceSurface::ConstantExtrapolation));

            return ext::make_shared<BlackScholesMertonProcess>(
                Handle<Quote>(ext::make_shared<SimpleQuote>(100.0)), qTS, rTS, volTS);
        }

        // Prices under LocalVolStepSingleVariate and SingleVariate, both monitoring modes, and
        // requires them within the relative tolerance.
        void checkLocalVolStepPrices(Date today,
                                     const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                                     Real tolerance) {
            const auto option_data = OptionData();
            const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
                option_data.barrier, option_data.bonusLevel, today + option_data.ttm);

            for (const bool isBiased : {true, false}) {
                bonusClassicOption->setPricingEngine(
                    ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics,
                                                          LocalVolStepSingleVariate>>(
                        process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), isBiased, true,
                        42));
                const auto cached = bonusClassicOption->NPV();

                bonusClassicOption->setPricingEngine(
                    ext::make_shared<
                        MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                        process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), isBiased, true,
                        42));
                const auto plain = bonusClassicOption->NPV();

                BOOST_TEST_MESSAGE((isBiased ? "discrete " : "continuous ")
                                   << cached << ", relative difference "
                                   << std::fabs(cached - plain) / plain);
                BOOST_CHECK_CLOSE_FRACTION(cached, plain, tolerance);
            }
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

    BOOST_AUTO_TEST_CASE(
        testBonusClassicPayoff) { // NOLINT(misc-use-internal-linkage): the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicPayoff test");

        const auto data = OptionData();
        const auto payoff = BonusClassicPayoff(data.barrier, data.bonusLevel);

        BOOST_CHECK_EQUAL(payoff(80.00), 80.00);
        // Pins the boundary convention: at the barrier itself the bonus is already gone.
        BOOST_CHECK_EQUAL(payoff(data.barrier), data.barrier);
        BOOST_CHECK_EQUAL(payoff(100.00), data.bonusLevel);
        BOOST_CHECK_EQUAL(payoff(125.00), 125.00);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOption) { // NOLINT(misc-use-internal-linkage): the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption test");

        const auto data = OptionData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + data.ttm;

        const auto bonusClassicOption =
            ext::make_shared<BonusClassicOption>(data.barrier, data.bonusLevel, exerciseDate);

        BOOST_CHECK_EQUAL(bonusClassicOption->exercise()->type(), Exercise::European);
        BOOST_CHECK_EQUAL(bonusClassicOption->exercise()->lastDate(), Date(22, Nov, 2025));
        BOOST_CHECK_EQUAL(bonusClassicOption->barrier(), data.barrier);
        BOOST_CHECK_EQUAL(bonusClassicOption->bonusLevel(), data.bonusLevel);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionValuation) { // NOLINT(misc-use-internal-linkage):
                                                            // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption valuation test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        // The Null<Real>() tolerance is mandatory, not a default: with no error estimate under
        // LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and maxSamples
        // never applies.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);

        bonusClassicOption->setPricingEngine(mcEngine);
        const auto npv = bonusClassicOption->NPV();

        // Regression lock. The low-discrepancy sequence is deterministic for a fixed seed
        // and time grid, so this pins the engine to its own output; it is not an
        // externally validated price. See testBonusClassicOptionReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(106.96041418042263, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionReplication) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption replication test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        bonusClassicOption->setPricingEngine(mcEngine);
        const auto npv = bonusClassicOption->NPV();

        // The pricer pays S_T once the barrier has been touched and max(S_T, bonusLevel)
        // otherwise, i.e. S_T + 1{never touched} * max(bonusLevel - S_T, 0): the asset
        // itself plus a down-and-out put struck at the bonus level.
        //
        // AnalyticBarrierEngine assumes continuous monitoring, while the engine monitors
        // on its time grid only. Broadie, Glasserman and Kou (1997), "A continuity
        // correction for discrete barrier options", Mathematical Finance 7(4), 325-349,
        // give the correction as a shift of a down barrier to H * exp(-beta * sigma *
        // sqrt(dt)) with beta = -zeta(1/2) / sqrt(2 * pi).
        // Read from the engine's own grid, so the correction always uses the step the paths
        // were monitored on.
        const auto dt = mcEngine->timeGrid().dt(0);
        constexpr Real beta = 0.5826;
        const auto correctedBarrier =
            option_data.barrier * std::exp(-beta * market_data.volatility * std::sqrt(dt));

        // Receiving the asset at maturity is worth spot * exp(-q * T).
        const auto assetLeg = process->x0() * process->dividendYield()->discount(exerciseDate);

        auto downOutPut =
            BarrierOption(Barrier::DownOut, correctedBarrier, 0.0,
                          ext::make_shared<PlainVanillaPayoff>(Option::Put, option_data.bonusLevel),
                          ext::make_shared<EuropeanExercise>(exerciseDate));
        downOutPut.setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(process));

        const auto replication = assetLeg + downOutPut.NPV();

        // Measured residual 3.8e-4 relative; the correction is O(1 / sqrt(steps)) and the
        // grid has 42 steps. Both sides are deterministic, so this is model error, not noise.
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1e-3);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionContinuousValuation) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption continuous valuation test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42);

        bonusClassicOption->setPricingEngine(mcEngine);
        const auto npv = bonusClassicOption->NPV();

        // Regression lock, as in testBonusClassicOptionValuation: the engine's own output for
        // a fixed seed and grid, not an externally validated price. See
        // testBonusClassicOptionContinuousReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(105.88329042929441, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionContinuousReplication) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption continuous replication test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42);
        bonusClassicOption->setPricingEngine(mcEngine);
        const auto npv = bonusClassicOption->NPV();

        // The same decomposition as testBonusClassicOptionReplication: the asset plus a
        // down-and-out put struck at the bonus level. The engine now monitors continuously,
        // as AnalyticBarrierEngine assumes, so the put takes the barrier itself and no
        // Broadie-Glasserman-Kou shift applies.
        const auto assetLeg = process->x0() * process->dividendYield()->discount(exerciseDate);

        auto downOutPut =
            BarrierOption(Barrier::DownOut, option_data.barrier, 0.0,
                          ext::make_shared<PlainVanillaPayoff>(Option::Put, option_data.bonusLevel),
                          ext::make_shared<EuropeanExercise>(exerciseDate));
        downOutPut.setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(process));

        const auto replication = assetLeg + downOutPut.NPV();

        // With flat r, q and sigma the bridge is exact, so the residual is sampling error
        // alone: measured 6.5e-5 relative at 50,000 paths, 1.5e-5 at 200,000 and 2.3e-5 at
        // 400,000. The bound leaves three times the 50,000-path residual.
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionMonitoringOrder) { // NOLINT(misc-use-internal-linkage):
                                                 // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption monitoring order test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        bonusClassicOption->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42));
        const auto discrete = bonusClassicOption->NPV();

        bonusClassicOption->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42));
        const auto continuous = bonusClassicOption->NPV();

        // Continuous monitoring sees every crossing the grid sees and more, and a knock-out
        // only removes the bonus put, so on the same paths the continuous price is lower.
        BOOST_TEST_MESSAGE("discrete " << discrete << ", continuous " << continuous);
        BOOST_CHECK_LT(continuous, discrete);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionPathGeneratorTraits) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption prices the same under CachedStepSingleVariate and "
                           "SingleVariate");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        for (const bool isBiased : {true, false}) {
            bonusClassicOption->setPricingEngine(
                ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
                    process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), isBiased, true, 42));
            const auto cached = bonusClassicOption->NPV();

            bonusClassicOption->setPricingEngine(
                ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                    process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), isBiased, true, 42));
            const auto plain = bonusClassicOption->NPV();

            // Exact on purpose: the cached step is the double QuantLib::PathGenerator evolves,
            // so the traits move no price.
            BOOST_TEST_MESSAGE((isBiased ? "discrete " : "continuous ") << cached);
            BOOST_CHECK_EQUAL(cached, plain);
        }
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionLocalVolStep) { // NOLINT(misc-use-internal-linkage):
                                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption prices the Euler step under "
                           "LocalVolStepSingleVariate as under SingleVariate");

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        // The forced discretization takes the Euler step through LocalConstantVol, where the
        // paths agree to their rounding.
        checkLocalVolStepPrices(today, MarketData().makeGeneralizedBlackScholesProcess(today, true),
                                1.0e-12);
        // Under a smile LocalVolSurface amplifies a last bit along a path. Where the compiler
        // contracts to FMAs the prices then differ by up to 1.3e-8 relative, measured with gcc
        // and clang at -march=x86-64-v3, far inside the Monte Carlo error; without contraction
        // they are the same double.
        checkLocalVolStepPrices(today, makeSmileProcess(today), 1.0e-6);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionInexactStep) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption refuses an inexact step under "
                           "CachedStepSingleVariate and prices it under SingleVariate");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today, true);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        // The volatility's type alone passes for exact; only the probe against evolve()
        // catches the forced discretization, and the engine fails loud instead of falling back.
        bonusClassicOption->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), false, true, 42));
        BOOST_CHECK_THROW(bonusClassicOption->NPV(), Error);

        bonusClassicOption->setPricingEngine(
            ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), false, true, 42));
        BOOST_CHECK_NO_THROW(bonusClassicOption->NPV());
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
