//
// Created by ralf.eckel on 21.06.2025.
//

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <boost/test/unit_test.hpp>
#include <test-suite/utilities.hpp>

using namespace RKE::QL::External;
using namespace QuantLib;

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

    ext::shared_ptr<GeneralizedBlackScholesProcess> makeGeneralizedBlackScholesProcess(Date today) {
        auto dc = Actual360();
        auto spotQuote = ext::make_shared<SimpleQuote>(spot);

        auto qH_SME = ext::make_shared<SimpleQuote>(dividendYield);
        auto qTS = flatRate(today, qH_SME, dc);

        auto rH_SME = ext::make_shared<SimpleQuote>(riskfreeRate);
        auto rTS = flatRate(today, rH_SME, dc);

        auto volaQuote = ext::make_shared<SimpleQuote>(volatility);
        auto volTS = flatVol(today, volaQuote, dc);

        return ext::make_shared<BlackScholesMertonProcess>(Handle<Quote>(spotQuote), Handle(qTS),
                                                           Handle(rTS), Handle(volTS));
    }
};

BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

BOOST_AUTO_TEST_CASE(testBonusClassicPayoff) {
    BOOST_TEST_MESSAGE("BonusClassicPayoff test");

    auto data = OptionData();
    auto payoff = BonusClassicPayoff(data.barrier, data.bonusLevel);

    BOOST_CHECK_EQUAL(payoff(80.00), 80.00);
    BOOST_CHECK_EQUAL(payoff(data.barrier), data.barrier);
    BOOST_CHECK_EQUAL(payoff(100.00), data.bonusLevel);
    BOOST_CHECK_EQUAL(payoff(125.00), 125.00);
}

BOOST_AUTO_TEST_CASE(testBonusClassicOption) {
    BOOST_TEST_MESSAGE("BonusClassicOption test");

    auto data = OptionData();

    auto today = Date(22, Jun, 2025);
    Settings::instance().evaluationDate() = today;

    auto exerciseDate = today + data.ttm;

    auto bonusClassicOption =
        ext::make_shared<BonusClassicOption>(data.barrier, data.bonusLevel, exerciseDate);

    BOOST_CHECK_EQUAL(bonusClassicOption->exercise()->type(), Exercise::European);
    BOOST_CHECK_EQUAL(bonusClassicOption->exercise()->lastDate(), Date(22, Nov, 2025));
    BOOST_CHECK_EQUAL(bonusClassicOption->barrier(), data.barrier);
    BOOST_CHECK_EQUAL(bonusClassicOption->bonusLevel(), data.bonusLevel);
}

BOOST_AUTO_TEST_CASE(testBonusClassicOptionValuation) {
    BOOST_TEST_MESSAGE("BonusClassicOption valuation test");

    auto option_data = OptionData();
    auto market_data = MarketData();

    auto today = Date(22, Jun, 2025);
    Settings::instance().evaluationDate() = today;

    auto exerciseDate = today + option_data.ttm;

    auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
        option_data.barrier, option_data.bonusLevel, exerciseDate);

    auto process = market_data.makeGeneralizedBlackScholesProcess(today);
    auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
        process, 100, 50'000, 50'001, Null<Real>(), true, true, 42);

    bonusClassicOption->setPricingEngine(mcEngine);
    auto npv = bonusClassicOption->NPV();

    BOOST_CHECK_CLOSE_FRACTION(107.34, npv, 0.005);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
