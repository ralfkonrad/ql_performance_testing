// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/ql/ext/methods/montecarlo/BlackScholesStepCache.hpp>
#include <rke/ql/ext/methods/montecarlo/CachedStepPathGenerator.hpp>
#include <ql/errors.hpp>
#include <ql/math/matrix.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/methods/montecarlo/pathgenerator.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/processes/eulerdiscretization.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancecurve.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/termstructures/volatility/equityfx/localconstantvol.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <boost/test/unit_test.hpp>
#include <test-suite/utilities.hpp>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::TestSuite {
    namespace {
        // Conventions of every case: Actual360 throughout, NullCalendar, evaluation date
        // 22 Jun 2025, spot 100, rate and dividend yield continuously compounded.
        Date today() {
            return {22, Jun, 2025};
        }
        constexpr Real spot = 100.0;
        constexpr Time maturity = 1.0;
        constexpr Size timeSteps = 50;
        constexpr Size paths = 64;
        constexpr BigNatural seed = 42;

        using rsg_type = LowDiscrepancy::rsg_type;

        Handle<Quote> spotQuote() {
            return Handle<Quote>(ext::make_shared<SimpleQuote>(spot));
        }

        Handle<YieldTermStructure> flatCurve(Rate rate) {
            return Handle<YieldTermStructure>(flatRate(today(), rate, Actual360()));
        }

        Handle<BlackVolTermStructure> constantVol() {
            return Handle<BlackVolTermStructure>(flatVol(today(), 0.20, Actual360()));
        }

        // Both generators draw the same Sobol sequence through the same bridge, and every
        // point of every path, plain and antithetic, has to be the same double.
        void checkSamePaths(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process) {
            const TimeGrid grid(maturity, timeSteps);
            const PathGenerator<rsg_type> reference(
                process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true);
            const CachedStepPathGenerator<rsg_type> generator(
                process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true);

            Size mismatches = 0;
            for (Size j = 0; j < paths; ++j) {
                for (const bool antithetic : {false, true}) {
                    const auto& expected = antithetic ? reference.antithetic() : reference.next();
                    const auto& actual = antithetic ? generator.antithetic() : generator.next();
                    BOOST_CHECK_EQUAL(actual.weight, expected.weight);
                    for (Size i = 0; i < expected.value.length(); ++i) {
                        // Exact comparison on purpose: the regression locks depend on it.
                        if (actual.value[i] != expected.value[i]) {
                            ++mismatches;
                        }
                    }
                }
            }
            BOOST_CHECK_EQUAL(mismatches, Size(0));
        }

        // The cache finds the process's step inexact, and the generator refuses the process
        // instead of falling back to evolve().
        void checkRefused(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process) {
            const TimeGrid grid(maturity, timeSteps);
            const BlackScholesStepCache cache(process, grid);
            BOOST_CHECK(!cache.isExact());
            BOOST_CHECK_EQUAL(cache.size(), Size(0));
            BOOST_CHECK_THROW(
                CachedStepPathGenerator<rsg_type>(
                    process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true),
                Error);
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(CachedStepPathGeneratorTests)

    BOOST_AUTO_TEST_CASE(testConstantVolatility) { // NOLINT(misc-use-internal-linkage): the
                                                   // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a constant volatility");
        Settings::instance().evaluationDate() = today();

        const auto process = ext::make_shared<BlackScholesMertonProcess>(
            spotQuote(), flatCurve(0.03), flatCurve(0.01), constantVol());
        checkSamePaths(process);

        const TimeGrid grid(maturity, timeSteps);
        const BlackScholesStepCache cache(process, grid);
        BOOST_REQUIRE(cache.isExact());
        for (Size i = 0; i < timeSteps; ++i) {
            BOOST_CHECK_EQUAL(cache.variance(i), process->variance(grid[i], spot, grid.dt(i)));
        }
    }

    BOOST_AUTO_TEST_CASE(testVarianceCurveAndZeroCurves) { // NOLINT(misc-use-internal-linkage):
                                                           // the struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a variance curve and zero curves");
        Settings::instance().evaluationDate() = today();

        const std::vector<Date> curveDates = {
            today(),
            today() + Period(3, Months),
            today() + Period(1, Years),
            today() + Period(2, Years),
        };
        const auto riskFree = Handle<YieldTermStructure>(
            ext::make_shared<ZeroCurve>(curveDates, std::vector<Rate>{0.010, 0.012, 0.018, 0.022},
                                        Actual360(), NullCalendar()));
        const auto dividend = Handle<YieldTermStructure>(
            ext::make_shared<ZeroCurve>(curveDates, std::vector<Rate>{0.030, 0.028, 0.025, 0.024},
                                        Actual360(), NullCalendar()));

        const std::vector<Date> volDates = {
            today() + Period(3, Months),
            today() + Period(1, Years),
            today() + Period(2, Years),
        };
        const auto vol = Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceCurve>(
            today(), volDates, std::vector<Volatility>{0.25, 0.21, 0.19}, Actual360()));

        checkSamePaths(
            ext::make_shared<BlackScholesMertonProcess>(spotQuote(), dividend, riskFree, vol));
    }

    BOOST_AUTO_TEST_CASE(testVolatilitySurface) { // NOLINT(misc-use-internal-linkage): the
                                                  // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses a smile");
        Settings::instance().evaluationDate() = today();

        const std::vector<Date> dates = {
            today() + Period(6, Months),
            today() + Period(1, Years),
            today() + Period(2, Years),
        };
        const std::vector<Real> strikes = {60.0, 80.0, 100.0, 120.0, 140.0};
        // Rows are strikes, columns dates: a mild skew, flat in time.
        Matrix vols(strikes.size(), dates.size());
        const std::vector<Volatility> skew = {0.28, 0.24, 0.20, 0.19, 0.19};
        for (Size i = 0; i < strikes.size(); ++i) {
            for (Size j = 0; j < dates.size(); ++j) {
                vols(i, j) = skew[i];
            }
        }
        const auto surface = Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceSurface>(
            today(), NullCalendar(), dates, strikes, vols, Actual360(),
            BlackVarianceSurface::ConstantExtrapolation,
            BlackVarianceSurface::ConstantExtrapolation));

        checkRefused(ext::make_shared<BlackScholesMertonProcess>(spotQuote(), flatCurve(0.03),
                                                                 flatCurve(0.01), surface));
    }

    BOOST_AUTO_TEST_CASE(testForcedDiscretization) { // NOLINT(misc-use-internal-linkage): the
                                                     // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses a forced discretization");
        Settings::instance().evaluationDate() = today();

        // The type check alone would take the exact step here; the probe has to catch it.
        checkRefused(ext::make_shared<BlackScholesMertonProcess>(
            spotQuote(), flatCurve(0.03), flatCurve(0.01), constantVol(),
            ext::make_shared<EulerDiscretization>(), true));
    }

    BOOST_AUTO_TEST_CASE(testExternalLocalVolatility) { // NOLINT(misc-use-internal-linkage):
                                                        // the struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses an external local vol");
        Settings::instance().evaluationDate() = today();

        // Euler steps over a BlackConstantVol, which the type check alone would miss.
        const auto localVol = Handle<LocalVolTermStructure>(
            ext::make_shared<LocalConstantVol>(today(), 0.20, Actual360()));
        checkRefused(ext::make_shared<GeneralizedBlackScholesProcess>(
            spotQuote(), flatCurve(0.03), flatCurve(0.01), constantVol(), localVol));
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
