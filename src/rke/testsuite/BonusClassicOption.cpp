// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BinomialBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/math/matrix.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
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
#include <utility>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::TestSuite {
    // Barrier monitoring dates of the MC engine are its time grid points.
    constexpr Size mcTimeStepsPerYear = 100;
    // The FD grid QuantLib's own barrier tests price Haug's table on.
    constexpr Size fdTimeGrid = 200;
    constexpr Size fdSpaceGrid = 400;
    // The step count QuantLib's own barrier tests price Haug's table with.
    constexpr Size treeTimeSteps = 400;

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

        // Broadie, Glasserman and Kou (1997), "A continuity correction for discrete barrier
        // options", Mathematical Finance 7(4), 325-349: a down barrier monitored every dt prices
        // like a continuously monitored one at H * exp(-beta * sigma * sqrt(dt)), with
        // beta = -zeta(1/2) / sqrt(2 * pi).
        Real bgkShiftedBarrier(Real barrier, Volatility volatility, Time dt) {
            constexpr Real beta = 0.5826;
            return barrier * std::exp(-beta * volatility * std::sqrt(dt));
        }

        // The certificate pays S_T once the barrier has been touched and max(S_T, bonusLevel)
        // otherwise, i.e. S_T + 1{never touched} * max(bonusLevel - S_T, 0): the asset itself
        // plus a down-and-out put struck at the bonus level. Receiving the asset at maturity is
        // worth spot * exp(-q * T); AnalyticBarrierEngine prices the put under continuous
        // monitoring of the barrier given here.
        Real replicationPrice(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                              const OptionData& data,
                              Date exerciseDate,
                              Real barrier) {
            const auto assetLeg = process->x0() * process->dividendYield()->discount(exerciseDate);

            auto downOutPut =
                BarrierOption(Barrier::DownOut, barrier, 0.0,
                              ext::make_shared<PlainVanillaPayoff>(Option::Put, data.bonusLevel),
                              ext::make_shared<EuropeanExercise>(exerciseDate));
            downOutPut.setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(process));

            return assetLeg + downOutPut.NPV();
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

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionEngineGuards) { // NOLINT(misc-use-internal-linkage):
                                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption engines fail with a QuantLib::Error on a null "
                           "process and on timeGrid() before the first NPV()");

        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        BOOST_CHECK_THROW(MCBonusClassicEngine<LowDiscrepancy>(nullptr, mcTimeStepsPerYear, 1'000,
                                                               1'001, Null<Real>(), true, true, 42),
                          Error);
        BOOST_CHECK_THROW(FdBlackScholesBonusClassicEngine(nullptr, mcTimeStepsPerYear), Error);
        BOOST_CHECK_THROW(BinomialBonusClassicEngine<CoxRossRubinstein>(nullptr, treeTimeSteps),
                          Error);

        // The arguments, the exercise among them, are empty until an instrument sets them up.
        const MCBonusClassicEngine<LowDiscrepancy> mcEngine(process, mcTimeStepsPerYear, 1'000,
                                                            1'001, Null<Real>(), true, true, 42);
        BOOST_CHECK_THROW(static_cast<void>(mcEngine.timeGrid()), Error);
        const FdBlackScholesBonusClassicEngine fdEngine(process, mcTimeStepsPerYear);
        BOOST_CHECK_THROW(static_cast<void>(fdEngine.timeGrid()), Error);
        const BinomialBonusClassicEngine<CoxRossRubinstein> treeEngine(process, treeTimeSteps);
        BOOST_CHECK_THROW(static_cast<void>(treeEngine.timeGrid()), Error);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionNonPositiveLevels) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption rejects a barrier or bonus level at or below "
                           "zero before any engine runs");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;
        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        // One engine per method; the FD engine in continuous mode is the one whose grid would
        // otherwise take log(0) and fail deep inside the solver.
        const std::vector<ext::shared_ptr<PricingEngine>> engines = {
            ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
                process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), true, true, 42),
            ext::make_shared<FdBlackScholesBonusClassicEngine>(process),
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(process, treeTimeSteps),
        };
        for (const auto& engine : engines) {
            for (const auto& [barrier, bonusLevel] :
                 {std::pair{0.0, option_data.bonusLevel}, std::pair{option_data.barrier, 0.0}}) {
                BonusClassicOption option(barrier, bonusLevel, exerciseDate);
                option.setPricingEngine(engine);
                BOOST_CHECK_THROW(static_cast<void>(option.NPV()), Error);
            }
        }
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

        // AnalyticBarrierEngine assumes continuous monitoring, while the engine monitors on
        // its time grid only, so the replication takes the Broadie-Glasserman-Kou barrier.
        // Read from the engine's own grid, so the correction always uses the step the paths
        // were monitored on.
        const auto dt = mcEngine->timeGrid().dt(0);
        const auto replication =
            replicationPrice(process, option_data, exerciseDate,
                             bgkShiftedBarrier(option_data.barrier, market_data.volatility, dt));

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

        // The engine now monitors continuously, as AnalyticBarrierEngine assumes, so the put
        // takes the barrier itself and no Broadie-Glasserman-Kou shift applies.
        const auto replication =
            replicationPrice(process, option_data, exerciseDate, option_data.barrier);

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

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdContinuousValuation) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD continuous valuation test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        bonusClassicOption->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, Null<Size>(), fdTimeGrid, fdSpaceGrid));
        const auto npv = bonusClassicOption->NPV();

        // Regression lock: the engine's own output on this grid and the default TrBDF2 scheme,
        // not an externally validated price. See testBonusClassicOptionFdContinuousReplication
        // for that.
        BOOST_CHECK_CLOSE_FRACTION(105.88958719924801, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdContinuousReplication) { // NOLINT(misc-use-internal-linkage):
                                                         // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD continuous replication test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        // std::exp(std::log(85.0)) lies above 85, so a grid starting at std::log(85.0) would pay
        // the bonus on its first node at maturity; 90 maps back to itself.
        for (const Real barrier : {option_data.barrier, 85.0}) {
            const auto bonusClassicOption =
                ext::make_shared<BonusClassicOption>(barrier, option_data.bonusLevel, exerciseDate);
            bonusClassicOption->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
                process, Null<Size>(), fdTimeGrid, fdSpaceGrid));
            const auto npv = bonusClassicOption->NPV();

            // Both sides monitor continuously, so the put takes the barrier itself.
            const auto replication = replicationPrice(process, option_data, exerciseDate, barrier);

            // Measured residuals 5.2e-6 relative at barrier 90 and 4.9e-6 at 85 under the
            // default TrBDF2 scheme. They halve per doubling of both grids, to 1.4e-6 and 1.0e-6
            // at 800 steps and 1,600 nodes: QuantLib imposes the Dirichlet value on the barrier
            // node after each implicit solve, so the next node is coupled to the unconstrained
            // one. A first node above 85 was 2.1e-4 off. The bound leaves nearly three times
            // the larger residual.
            BOOST_TEST_MESSAGE("barrier " << barrier << ": " << npv << ", relative residual "
                                          << std::fabs(npv - replication) / replication);
            BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1.5e-5);
        }
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionFdValuation) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD valuation test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        bonusClassicOption->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2()));
        const auto npv = bonusClassicOption->NPV();

        // Regression lock: the engine's own output on this grid and scheme, not an externally
        // validated price. See testBonusClassicOptionFdReplication and
        // testBonusClassicOptionFdVersusMc for that.
        BOOST_CHECK_CLOSE_FRACTION(106.95082725557951, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdReplication) { // NOLINT(misc-use-internal-linkage):
                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD replication test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);
        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2());
        bonusClassicOption->setPricingEngine(fdEngine);
        const auto npv = bonusClassicOption->NPV();

        // The engine monitors on its time grid, so the replication takes the
        // Broadie-Glasserman-Kou barrier for the grid's step.
        const auto dt = fdEngine->timeGrid().dt(0);
        const auto replication =
            replicationPrice(process, option_data, exerciseDate,
                             bgkShiftedBarrier(option_data.barrier, market_data.volatility, dt));

        // Measured residual 4.7e-4 relative. The engine at 6,400 nodes and 3,200 steps, 2.5e-7
        // from its value at half that grid, is 4.5e-4 below the replication, so nearly all of it
        // is the correction's own error on 42 steps; the Monte Carlo engine's is 3.8e-4.
        BOOST_TEST_MESSAGE("FD " << npv << ", relative residual "
                                 << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1e-3);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionFdVersusMc) { // NOLINT(misc-use-internal-linkage):
                                                             // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD versus MC test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2());
        bonusClassicOption->setPricingEngine(fdEngine);
        const auto fd = bonusClassicOption->NPV();

        // The configuration of testBonusClassicOptionValuation's lock.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        bonusClassicOption->setPricingEngine(mcEngine);
        const auto mc = bonusClassicOption->NPV();

        // Both engines monitor the same product: the same 42 points after t = 0.
        const auto fdGrid = fdEngine->timeGrid();
        const auto mcGrid = mcEngine->timeGrid();
        BOOST_CHECK_EQUAL(fdGrid.size() - 1, Size(42));
        BOOST_REQUIRE_EQUAL(fdGrid.size(), mcGrid.size());
        for (Size i = 0; i < fdGrid.size(); ++i) {
            BOOST_CHECK_EQUAL(fdGrid[i], mcGrid[i]);
        }

        // Measured residual 9.0e-5 relative, the sum of two deterministic errors: the engine at
        // 6,400 nodes and 3,200 steps is 2.8e-5 above this grid's value and 6.2e-5 below the
        // Monte Carlo lock, which is that lock's own distance from the discrete price at 50,000
        // low-discrepancy paths. The bound leaves three times the residual.
        BOOST_TEST_MESSAGE("FD " << fd << ", MC " << mc << ", relative difference "
                                 << std::fabs(fd - mc) / mc);
        BOOST_CHECK_CLOSE_FRACTION(mc, fd, 3e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdMonitoringOrder) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD monitoring order test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto discreteEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2());
        bonusClassicOption->setPricingEngine(discreteEngine);
        const auto discrete = bonusClassicOption->NPV();

        const auto continuousEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            process, Null<Size>(), fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2());
        bonusClassicOption->setPricingEngine(continuousEngine);
        const auto continuous = bonusClassicOption->NPV();

        BOOST_CHECK(!discreteEngine->monitorsContinuously());
        BOOST_CHECK(continuousEngine->monitorsContinuously());
        // A continuously monitored barrier has no grid to report, and no fallback either.
        BOOST_CHECK_THROW(static_cast<void>(continuousEngine->timeGrid()), Error);

        // Continuous monitoring knocks out every path the grid knocks out and more, and a
        // knock-out only removes the bonus put, so the continuous price is lower.
        BOOST_TEST_MESSAGE("discrete " << discrete << ", continuous " << continuous);
        BOOST_CHECK_LT(continuous, discrete);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialValuation) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial valuation test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        const auto engine =
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(process, treeTimeSteps);
        bonusClassicOption->setPricingEngine(engine);
        const auto npv = bonusClassicOption->NPV();

        // Boyle-Lau: the first floor(i^2 sigma^2 T / ln^2(S / H)) above 400, at i = 17.
        BOOST_CHECK_EQUAL(engine->timeGrid().size() - 1, Size(442));

        // Regression lock: the engine's own output on this lattice, not an externally validated
        // price. See testBonusClassicOptionBinomialReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(105.89600347917739, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialReplication) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial replication test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);
        bonusClassicOption->setPricingEngine(
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(process,
                                                                            treeTimeSteps));
        const auto npv = bonusClassicOption->NPV();

        // The tree monitors on every step, and Boyle-Lau puts a layer of nodes at 89.9938, just
        // below the barrier, so the put takes the barrier itself and no Broadie-Glasserman-Kou
        // shift applies.
        const auto replication =
            replicationPrice(process, option_data, exerciseDate, option_data.barrier);

        // Measured residual 5.5e-5 relative at 442 steps. It is discretisation error, not
        // noise, and it does not fall steadily with the steps: from 100 to 1600 requested steps
        // it follows how far the Boyle-Lau floor leaves the layer below the barrier, 1.7e-6 at
        // 810 steps with the layer 7e-4 below it, 2.3e-4 at 220 steps with it 1.1e-2 below.
        // The bound leaves 3.6 times the 442-step residual.
        BOOST_TEST_MESSAGE("binomial " << npv << ", relative residual "
                                       << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialBoyleLau) { // NOLINT(misc-use-internal-linkage):
                                                  // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial Boyle-Lau test");

        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);
        const auto replication =
            replicationPrice(process, option_data, exerciseDate, option_data.barrier);

        bonusClassicOption->setPricingEngine(
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(process,
                                                                            treeTimeSteps));
        const auto withBoyleLau = std::fabs(bonusClassicOption->NPV() - replication) / replication;

        // maxTimeSteps = timeSteps disables Boyle-Lau.
        const auto plainEngine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            process, treeTimeSteps, treeTimeSteps);
        bonusClassicOption->setPricingEngine(plainEngine);
        const auto withoutBoyleLau =
            std::fabs(bonusClassicOption->NPV() - replication) / replication;
        BOOST_CHECK_EQUAL(plainEngine->timeGrid().size() - 1, treeTimeSteps);

        // Without Boyle-Lau the first knocked-out layer at 400 steps sits at 89.51, an effective
        // barrier 0.55% low; measured residuals 5.5e-5 relative with Boyle-Lau and 4.9e-3
        // without.
        BOOST_TEST_MESSAGE("relative residual with Boyle-Lau " << withBoyleLau << ", without "
                                                               << withoutBoyleLau);
        BOOST_CHECK_LT(withBoyleLau, withoutBoyleLau);
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
