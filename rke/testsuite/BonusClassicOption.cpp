//
// Created by ralf.eckel on 21.06.2025.
//

#include "TestSuiteFixture.hpp"
#include <boost/test/unit_test.hpp>

using namespace RKE::QL::External;

BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

BOOST_AUTO_TEST_CASE(testTrue) {
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(testFalse) {
    BOOST_CHECK(false == false);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
