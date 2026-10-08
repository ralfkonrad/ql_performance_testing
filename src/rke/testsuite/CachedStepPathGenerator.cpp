// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/StepCacheMarkets.hpp>
#include <rke/ql/ext/methods/montecarlo/BlackScholesStepCache.hpp>
#include <rke/ql/ext/methods/montecarlo/CachedStepPathGenerator.hpp>
#include <rke/ql/ext/methods/montecarlo/LocalVolStepCache.hpp>
#include <ql/errors.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/methods/montecarlo/pathgenerator.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::TestSuite {
    namespace {
        // Conventions of every case: Actual360 throughout, NullCalendar, evaluation date
        // 22 Jun 2025, spot 100, rate and dividend yield continuously compounded.
        using RKE::Common::evaluationDate;
        constexpr Time maturity = 1.0;
        constexpr Size timeSteps = 50;
        constexpr Size paths = 64;
        constexpr BigNatural seed = 42;
        // A step's rounding, about 1e-16 relative, accumulated over timeSteps steps.
        constexpr Real pathTolerance = 1.0e-12;

        using rsg_type = LowDiscrepancy::rsg_type;

        // Both generators draw the same Sobol sequence through the same bridge, and every
        // point of every path, plain and antithetic, has to be the same double, or within the
        // relative tolerance where one is given.
        template <class StepCache = BlackScholesStepCache>
        void checkSamePaths(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                            Real tolerance = 0.0) {
            const TimeGrid grid(maturity, timeSteps);
            const PathGenerator<rsg_type> reference(
                process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true);
            const CachedStepPathGenerator<rsg_type, StepCache> generator(
                process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true);

            Size mismatches = 0;
            Real maxRelative = 0.0;
            for (Size j = 0; j < paths; ++j) {
                for (const bool antithetic : {false, true}) {
                    const auto& expected = antithetic ? reference.antithetic() : reference.next();
                    const auto& actual = antithetic ? generator.antithetic() : generator.next();
                    BOOST_CHECK_EQUAL(actual.weight, expected.weight);
                    for (Size i = 0; i < expected.value.length(); ++i) {
                        const auto difference = std::fabs(actual.value[i] - expected.value[i]);
                        maxRelative = std::max(maxRelative, difference / expected.value[i]);
                        // Exact comparison by default: the regression locks depend on it.
                        if (difference > tolerance * expected.value[i]) {
                            ++mismatches;
                        }
                    }
                }
            }
            BOOST_TEST_MESSAGE("  largest relative difference " << maxRelative);
            BOOST_CHECK_EQUAL(mismatches, Size(0));
        }

        // One step from the same point on every step of the grid: both sides evaluate the same
        // local volatility, so only the rounding of the step's own arithmetic can differ.
        void checkSameSteps(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process) {
            const TimeGrid grid(maturity, timeSteps);
            const LocalVolStepCache cache(process, grid);
            BOOST_REQUIRE(cache.reproducesEvolve());

            // Onto and around the surface's interior strike nodes, where LocalVolSurface's
            // finite differences in strike are least smooth, and beyond the last one. Not near
            // the first, where the flat extrapolation's kink makes the local variance negative.
            const std::vector<Real> levels = {
                65.0, 79.99, 80.0, 95.0, 100.0, 100.01, 119.99, 120.0, 135.0, 150.0,
            };
            const std::vector<Real> increments = {-2.5, -0.4, 0.0, 0.9, 3.1};
            Size mismatches = 0;
            Real maxRelative = 0.0;
            for (Size i = 0; i < timeSteps; ++i) {
                for (const auto x : levels) {
                    for (const auto dw : increments) {
                        const auto expected = process->evolve(grid[i], x, grid.dt(i), dw);
                        const auto difference = std::fabs(cache.evolve(i, x, dw) - expected);
                        maxRelative = std::max(maxRelative, difference / expected);
                        if (difference > LocalVolStepCache::stepTolerance * expected) {
                            ++mismatches;
                        }
                    }
                }
            }
            BOOST_TEST_MESSAGE("  largest relative difference " << maxRelative);
            BOOST_CHECK_EQUAL(mismatches, Size(0));
        }

        // The cache finds the process's step inexact, and the generator refuses the process
        // instead of falling back to evolve().
        template <class StepCache = BlackScholesStepCache>
        void checkRefused(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process) {
            const TimeGrid grid(maturity, timeSteps);
            const StepCache cache(process, grid);
            BOOST_CHECK(!cache.reproducesEvolve());
            BOOST_CHECK_EQUAL(cache.size(), Size(0));
            BOOST_CHECK_THROW(
                (CachedStepPathGenerator<rsg_type, StepCache>(
                    process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true)),
                Error);
        }

        // The flat reference market under EulerDiscretization, as the benchmarks price it.
        ext::shared_ptr<GeneralizedBlackScholesProcess> constantVolProcess() {
            return RKE::Common::MarketData().makeGeneralizedBlackScholesProcess(evaluationDate());
        }

        // The type check alone would take the exact step here.
        ext::shared_ptr<GeneralizedBlackScholesProcess> forcedDiscretizationProcess() {
            return RKE::Common::MarketData().makeGeneralizedBlackScholesProcess(evaluationDate(),
                                                                                true);
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(CachedStepPathGeneratorTests)

    BOOST_AUTO_TEST_CASE(testConstantVolatility) { // NOLINT(misc-use-internal-linkage): the
                                                   // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a constant volatility");
        Settings::instance().evaluationDate() = evaluationDate();

        const auto process = constantVolProcess();
        checkSamePaths(process);

        const TimeGrid grid(maturity, timeSteps);
        const BlackScholesStepCache cache(process, grid);
        BOOST_REQUIRE(cache.reproducesEvolve());
        for (Size i = 0; i < timeSteps; ++i) {
            BOOST_CHECK_EQUAL(
                cache.variance(i),
                process->variance(grid[i], RKE::Common::MarketData().spot, grid.dt(i)));
        }
    }

    BOOST_AUTO_TEST_CASE(testVarianceCurveAndZeroCurves) { // NOLINT(misc-use-internal-linkage):
                                                           // the struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a variance curve and zero curves");
        Settings::instance().evaluationDate() = evaluationDate();

        checkSamePaths(RKE::Common::varianceCurveProcess(evaluationDate()));
    }

    BOOST_AUTO_TEST_CASE(testVolatilitySurface) { // NOLINT(misc-use-internal-linkage): the
                                                  // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses a smile");
        Settings::instance().evaluationDate() = evaluationDate();

        checkRefused(RKE::Common::smileProcess(evaluationDate()));
    }

    BOOST_AUTO_TEST_CASE(testForcedDiscretization) { // NOLINT(misc-use-internal-linkage): the
                                                     // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses a forced discretization");
        Settings::instance().evaluationDate() = evaluationDate();

        // The probe has to catch what the type check misses.
        checkRefused(forcedDiscretizationProcess());
    }

    BOOST_AUTO_TEST_CASE(testExternalLocalVolatility) { // NOLINT(misc-use-internal-linkage):
                                                        // the struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator refuses an external local vol");
        Settings::instance().evaluationDate() = evaluationDate();

        checkRefused(RKE::Common::externalLocalVolProcess(evaluationDate()));
    }

    BOOST_AUTO_TEST_CASE(testSharedCache) { // NOLINT(misc-use-internal-linkage): the struct is
                                            // the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a shared cache produces the paths of "
                           "its own, and refuses a cache on another grid");
        Settings::instance().evaluationDate() = evaluationDate();

        const auto process = constantVolProcess();
        const TimeGrid grid(maturity, timeSteps);
        const auto cache = ext::make_shared<const BlackScholesStepCache>(process, grid);
        const CachedStepPathGenerator<rsg_type> own(
            process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true);
        const CachedStepPathGenerator<rsg_type> shared(
            process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true, cache);
        for (Size j = 0; j < paths; ++j) {
            const auto& expected = own.next();
            const auto& actual = shared.next();
            for (Size i = 0; i < expected.value.length(); ++i) {
                BOOST_CHECK_EQUAL(actual.value[i], expected.value[i]);
            }
        }

        const TimeGrid otherGrid(maturity, timeSteps + 1);
        BOOST_CHECK_THROW(
            (CachedStepPathGenerator<rsg_type>(
                process, otherGrid, LowDiscrepancy::make_sequence_generator(timeSteps + 1, seed),
                true, cache)),
            Error);
        BOOST_CHECK_THROW(
            (CachedStepPathGenerator<rsg_type>(
                process, grid, LowDiscrepancy::make_sequence_generator(timeSteps, seed), true,
                nullptr)),
            Error);
    }

    BOOST_AUTO_TEST_CASE(testEmptyGrid) { // NOLINT(misc-use-internal-linkage): the struct is
                                          // the macro's
        BOOST_TEST_MESSAGE("BlackScholesStepCache and LocalVolStepCache refuse an empty grid");
        Settings::instance().evaluationDate() = evaluationDate();

        // A QuantLib::Error, not the std::length_error of reserving SIZE_MAX steps.
        const TimeGrid empty;
        BOOST_CHECK_THROW(BlackScholesStepCache(constantVolProcess(), empty), Error);
        BOOST_CHECK_THROW(LocalVolStepCache(forcedDiscretizationProcess(), empty), Error);
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE(LocalVolStepPathGeneratorTests)

    // Paths are compared within a tolerance where the local volatility is constant: there a
    // step's rounding cannot feed back into sigma, and the difference stays at the rounding
    // accumulated over the steps. Under a smile, LocalVolSurface's finite differences in strike
    // turn a last bit into a different sigma, so the smile is checked one step at a time.

    BOOST_AUTO_TEST_CASE(testVolatilitySurface) { // NOLINT(misc-use-internal-linkage): the
                                                  // struct is the macro's
        BOOST_TEST_MESSAGE("LocalVolStepCache with a smile and zero curves");
        Settings::instance().evaluationDate() = evaluationDate();

        checkSameSteps(RKE::Common::smileProcess(evaluationDate()));
    }

    BOOST_AUTO_TEST_CASE(testForcedDiscretization) { // NOLINT(misc-use-internal-linkage): the
                                                     // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a LocalVolStepCache and a forced "
                           "discretization");
        Settings::instance().evaluationDate() = evaluationDate();

        checkSamePaths<LocalVolStepCache>(forcedDiscretizationProcess(), pathTolerance);
    }

    BOOST_AUTO_TEST_CASE(testExternalLocalVolatility) { // NOLINT(misc-use-internal-linkage):
                                                        // the struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a LocalVolStepCache and an external "
                           "local vol");
        Settings::instance().evaluationDate() = evaluationDate();

        checkSamePaths<LocalVolStepCache>(RKE::Common::externalLocalVolProcess(evaluationDate()),
                                          pathTolerance);
    }

    BOOST_AUTO_TEST_CASE(testExactStep) { // NOLINT(misc-use-internal-linkage): the struct is
                                          // the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a LocalVolStepCache refuses the exact "
                           "step on a variance curve");
        Settings::instance().evaluationDate() = evaluationDate();

        checkRefused<LocalVolStepCache>(RKE::Common::varianceCurveProcess(evaluationDate()));
    }

    BOOST_AUTO_TEST_CASE(testCoincidingExactStep) { // NOLINT(misc-use-internal-linkage): the
                                                    // struct is the macro's
        BOOST_TEST_MESSAGE("CachedStepPathGenerator with a LocalVolStepCache follows the exact "
                           "step where it coincides with Euler");
        Settings::instance().evaluationDate() = evaluationDate();

        // A constant volatility on flat curves makes the Euler and the exact step equal in exact
        // arithmetic, so the check cannot tell them apart, and need not. They round differently:
        // the Euler drift's forward rate spans 1e-4 years, the exact step's the whole step, about
        // 5e-15 relative per step and 2.3e-13 over the grid, measured.
        checkSamePaths<LocalVolStepCache>(constantVolProcess(), 1.0e-11);
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
