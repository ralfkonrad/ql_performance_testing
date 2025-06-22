//
// Created by ralf.eckel on 21.06.2025.
//

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <boost/test/unit_test.hpp>

using namespace RKE::QL::External;
using namespace QuantLib;

struct OptionData {
    Real barrier = 90.00;
    Real bonusLevel = 120.00;
    Period ttm = Period(5, Months);
};

BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

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

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
