// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/common/MarketData.hpp>
#include <ql/errors.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/settings.hpp>
#include <boost/test/unit_test.hpp>
#include <cmath>

using namespace QuantLib;

namespace RKE::TestSuite {
    namespace {
        using RKE::Common::evaluationDate;
        using RKE::Common::FlatMarketData;
        using RKE::Common::SmileMarketData;
        using RKE::Common::smileMarketDataAround;

        // A rate node is the default plus the shift: two roundings of a rate of a few percent,
        // each below 1e-17 absolute. A node volatility is atmVolatility - skew * ln(K / spot) on
        // scaled inputs, squared into a variance and back by BlackVarianceSurface: a handful of
        // roundings at 1.1e-16 relative each.
        constexpr Real rateTolerance = 1.0e-16;
        constexpr Real volatilityTolerance = 1.0e-14;

        // Every node of the surface smile builds, against the default surface times scale.
        // Conventions as SmileMarketData's: Actual360, NullCalendar, evaluation date 22 Jun 2025,
        // the bilinear surface, read on its own date and strike nodes.
        void checkScaledSurface(const SmileMarketData& smile, Real scale) {
            const auto today = evaluationDate();
            const auto defaults = SmileMarketData();
            const auto expected = defaults.makeGeneralizedBlackScholesProcess(today, false);
            const auto actual = smile.makeGeneralizedBlackScholesProcess(today, false);
            for (const auto& tenor : defaults.volTenors) {
                for (const auto strike : defaults.strikes) {
                    const auto vol = actual->blackVolatility()->blackVol(today + tenor, strike);
                    const auto reference =
                        scale * expected->blackVolatility()->blackVol(today + tenor, strike);
                    BOOST_CHECK_GT(vol, 0.0);
                    BOOST_CHECK_SMALL(std::fabs(vol - reference) / reference, volatilityTolerance);
                }
            }
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(MarketDataTests)

    BOOST_AUTO_TEST_CASE(testSmileAroundDefaults) { // NOLINT(misc-use-internal-linkage): the
                                                    // struct is the macro's
        BOOST_TEST_MESSAGE("SmileMarketData around FlatMarketData's defaults is the default one");

        const auto actual = smileMarketDataAround(FlatMarketData());
        const auto expected = SmileMarketData();
        BOOST_CHECK_EQUAL(actual.spot, expected.spot);
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.curveTenors.begin(), actual.curveTenors.end(),
                                      expected.curveTenors.begin(), expected.curveTenors.end());
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.riskfreeRates.begin(), actual.riskfreeRates.end(),
                                      expected.riskfreeRates.begin(), expected.riskfreeRates.end());
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.dividendYields.begin(), actual.dividendYields.end(),
                                      expected.dividendYields.begin(),
                                      expected.dividendYields.end());
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.volTenors.begin(), actual.volTenors.end(),
                                      expected.volTenors.begin(), expected.volTenors.end());
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.strikes.begin(), actual.strikes.end(),
                                      expected.strikes.begin(), expected.strikes.end());
        BOOST_CHECK_EQUAL(actual.atmVolatility, expected.atmVolatility);
        BOOST_CHECK_EQUAL(actual.skew, expected.skew);
    }

    BOOST_AUTO_TEST_CASE(testSmileAroundMovedLevels) { // NOLINT(misc-use-internal-linkage): the
                                                       // struct is the macro's
        BOOST_TEST_MESSAGE("SmileMarketData around moved levels shifts the curves and scales the "
                           "smile");
        Settings::instance().evaluationDate() = evaluationDate();

        const auto defaults = SmileMarketData();
        auto flat = FlatMarketData();
        flat.riskfreeRate = 0.02;
        flat.dividendYield = 0.01;
        flat.volatility = 0.25;
        const auto smile = smileMarketDataAround(flat);

        BOOST_REQUIRE_EQUAL(smile.riskfreeRates.size(), defaults.riskfreeRates.size());
        BOOST_REQUIRE_EQUAL(smile.dividendYields.size(), defaults.dividendYields.size());
        for (Size i = 0; i < defaults.riskfreeRates.size(); ++i) {
            BOOST_CHECK_SMALL(smile.riskfreeRates[i] - (defaults.riskfreeRates[i] + 0.01),
                              rateTolerance);
            BOOST_CHECK_SMALL(smile.dividendYields[i] - (defaults.dividendYields[i] - 0.02),
                              rateTolerance);
        }
        checkScaledSurface(smile, 0.25 / 0.20);

        // Below 0.08 * ln(400 / 100) = 0.111, where an at-the-money shift under the default skew
        // would turn the 400 node negative.
        flat.volatility = 0.05;
        checkScaledSurface(smileMarketDataAround(flat), 0.05 / 0.20);

        flat.volatility = 0.0;
        BOOST_CHECK_THROW(static_cast<void>(smileMarketDataAround(flat)), Error);
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
