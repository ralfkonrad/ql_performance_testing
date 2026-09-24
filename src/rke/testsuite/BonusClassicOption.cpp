//
// Created by ralf.eckel on 21.06.2025.
//

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <test-suite/utilities.hpp>

using namespace RKE::QL::External;
using namespace QuantLib;

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

        ext::shared_ptr<GeneralizedBlackScholesProcess>
        makeGeneralizedBlackScholesProcess(Date today) {
            const auto dc = Actual360();
            const auto spotQuote = ext::make_shared<SimpleQuote>(spot);

            const auto qH_SME = ext::make_shared<SimpleQuote>(dividendYield);
            const auto qTS = flatRate(today, qH_SME, dc);

            const auto rH_SME = ext::make_shared<SimpleQuote>(riskfreeRate);
            const auto rTS = flatRate(today, rH_SME, dc);

            const auto volaQuote = ext::make_shared<SimpleQuote>(volatility);
            const auto volTS = flatVol(today, volaQuote, dc);

            return ext::make_shared<BlackScholesMertonProcess>(
                Handle<Quote>(spotQuote), Handle(qTS), Handle(rTS), Handle(volTS));
        }
    };

}

BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

BOOST_AUTO_TEST_CASE(
    testBonusClassicPayoff) { // NOLINT(misc-use-internal-linkage): the struct is the macro's
    BOOST_TEST_MESSAGE("BonusClassicPayoff test");

    const auto data = OptionData();
    const auto payoff = BonusClassicPayoff(data.barrier, data.bonusLevel);

    BOOST_CHECK_EQUAL(payoff(80.00), 80.00);
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

BOOST_AUTO_TEST_CASE(testBonusClassicOptionValuation) { // NOLINT(misc-use-internal-linkage): the
                                                        // struct is the macro's
    BOOST_TEST_MESSAGE("BonusClassicOption valuation test");

    const auto option_data = OptionData();
    auto market_data = MarketData();

    const auto today = Date(22, Jun, 2025);
    Settings::instance().evaluationDate() = today;

    const auto exerciseDate = today + option_data.ttm;

    const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
        option_data.barrier, option_data.bonusLevel, exerciseDate);

    const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
    const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
        process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);

    bonusClassicOption->setPricingEngine(mcEngine);
    const auto npv = bonusClassicOption->NPV();

    // Regression lock. The low-discrepancy sequence is deterministic for a fixed seed
    // and time grid, so this pins the engine to its own output; it is not an
    // externally validated price. See testBonusClassicOptionReplication for that.
    BOOST_CHECK_CLOSE_FRACTION(106.96041418042263, npv, 1e-8);
}

BOOST_AUTO_TEST_CASE(testBonusClassicOptionReplication) { // NOLINT(misc-use-internal-linkage): the
                                                          // struct is the macro's
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

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
