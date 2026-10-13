// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// QuantLib::MCBarrierEngine on the down-and-out put RKE::Common::makeBarrierOptionSetup builds,
// barrier 70, strike 120, no rebate. Conventions of every case: Actual360 and NullCalendar
// throughout, rates and dividend yields continuously compounded, the payoff discounted on the
// process's risk-free curve from the exercise date, evaluation date 22 Jun 2025, exercise 1Y
// later on 22 Jun 2026. The engine's grid is monitoringStepsPerYear, 252, times the Actual360
// year fraction 365/360, truncated: 255 equal steps. Its paths are Sobol points with Joe-Kuo D7
// direction integers, seed 42, through a Brownian bridge, without antithetic paths.

#include "TestSuiteFixture.hpp"
#include <rke/common/BarrierOptionSetup.hpp>
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BarrierMonitoring.hpp>
#include <ql/errors.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/settings.hpp>
#include <ql/time/date.hpp>
#include <ql/timegrid.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <string_view>
#include <vector>

using namespace QuantLib;

namespace RKE::TestSuite {
    namespace {
        using RKE::Common::BarrierOptionData;
        using RKE::Common::BarrierOptionSetup;
        using RKE::Common::evaluationDate;
        using RKE::Common::FlatMarketData;
        using RKE::Common::makeBarrierOption;
        using RKE::Common::makeBarrierOptionSetup;
        using RKE::Common::Market;
        using RKE::Common::monitoringStepsPerYear;
        using RKE::Common::SmileMarketData;
        using RKE::QL::Ext::BarrierMonitoring;

        // The locks' sample count. On the production grid a path takes the same steps at any
        // sample count, and the same step gives the same double, so a lock at 2^12 paths proves
        // what one at 2^16 would, in about a sixteenth of the time. The 2^16 NPVs come from the
        // profile executable, rke_profile_barrieroption.
        constexpr Size lockSamples = Size{1} << 12U;

        // The replication's sample count. At lockSamples the residuals against the closed form
        // are 2.7e-3 relative discrete and 5.0e-3 continuous, bounds too wide to see a one-percent
        // error, the size of a year's discounting at the flat rate; at 2^16 they are 6.1e-4 and
        // 4.1e-4.
        constexpr Size replicationSamples = Size{1} << 16U;

        // Broadie, Glasserman and Kou (1997), "A continuity correction for discrete barrier
        // options", Mathematical Finance 7(4), 325-349: a down barrier monitored every dt prices
        // like a continuously monitored one at H * exp(-beta * sigma * sqrt(dt)), with
        // beta = -zeta(1/2) / sqrt(2 * pi). A copy of BonusClassicOption.cpp's, which is local to
        // that file.
        Real bgkShiftedBarrier(Real barrier, Volatility volatility, Time dt) {
            constexpr Real beta = 0.5826;
            return barrier * std::exp(-beta * volatility * std::sqrt(dt));
        }

        std::string_view monitoringName(BarrierMonitoring monitoring) {
            return monitoring == BarrierMonitoring::Discrete ? "discrete" : "continuous";
        }

        // Spelled as the profile executable's --market option.
        std::string_view marketName(Market market) {
            switch (market) {
                case Market::Flat:
                    return "flat";
                case Market::SmileBilinear:
                    return "smile-bilinear";
                case Market::SmileBicubic:
                    return "smile-bicubic";
            }
            QL_FAIL("unknown market");
        }

        // The put on BarrierOptionData's levels on the default markets, the barrier monitored as
        // asked, at samples paths. makeBarrierOptionSetup sets the evaluation date.
        BarrierOptionSetup makeSetup(Market market, BarrierMonitoring monitoring, Size samples) {
            return makeBarrierOptionSetup(BarrierOptionData(), FlatMarketData(), SmileMarketData(),
                                          monitoring, market, samples);
        }

        // The step of the grid the engine monitors on. MCBarrierEngine::timeGrid() is protected,
        // so this rebuilds the grid with its arithmetic: the year fraction to the exercise date
        // on the process's risk-free day counter, times timeStepsPerYear, truncated, and at least
        // one step.
        Time monitoringStep(const BarrierOptionSetup& setup) {
            const auto maturity = setup.process->time(setup.option->exercise()->lastDate());
            const auto steps = std::max<Size>(
                static_cast<Size>(static_cast<Real>(monitoringStepsPerYear) * maturity), 1);
            // 252 times 365/360 is 255.5.
            BOOST_REQUIRE_EQUAL(steps, Size(255));
            return TimeGrid(maturity, steps).dt(0);
        }

        // QuantLib's closed form, which assumes continuous monitoring: AnalyticBarrierEngine on
        // the setup's process, the put on BarrierOptionData's levels with the barrier given here.
        Real analyticPrice(const BarrierOptionSetup& setup, Real barrier) {
            auto data = BarrierOptionData();
            data.barrier = barrier;
            const auto option = makeBarrierOption(data, evaluationDate());
            option->setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(setup.process));
            return option->NPV();
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(BarrierOptionTests)

    BOOST_AUTO_TEST_CASE(testBarrierOptionSetup) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BarrierOption setup test");

        const auto setup = makeSetup(Market::Flat, BarrierMonitoring::Discrete, lockSamples);

        const Date today = Settings::instance().evaluationDate();
        BOOST_CHECK_EQUAL(today, Date(22, Jun, 2025));

        // BarrierOption keeps its barrier type, barrier and rebate protected and hands them to
        // an engine through setupArguments(), so the test reads them the way an engine does.
        // The levels are literals: the locks hold for these and no others.
        BarrierOption::arguments args;
        setup.option->setupArguments(&args);
        BOOST_CHECK_EQUAL(args.barrierType, Barrier::DownOut);
        BOOST_CHECK_EQUAL(args.barrier, 70.0);
        BOOST_CHECK_EQUAL(args.rebate, 0.0);

        const auto payoff = ext::dynamic_pointer_cast<PlainVanillaPayoff>(setup.option->payoff());
        BOOST_REQUIRE(payoff != nullptr);
        BOOST_CHECK_EQUAL(payoff->optionType(), Option::Put);
        BOOST_CHECK_EQUAL(payoff->strike(), 120.0);

        BOOST_CHECK_EQUAL(setup.option->exercise()->type(), Exercise::European);
        BOOST_CHECK_EQUAL(setup.option->exercise()->lastDate(), Date(22, Jun, 2026));

        // The put starts alive: MCBarrierEngine::calculate() fails on a spot below the barrier.
        BOOST_CHECK_GT(setup.process->x0(), args.barrier);
    }

    BOOST_AUTO_TEST_CASE(testBarrierOptionValuation) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BarrierOption valuation test");

        struct Lock {
            Market market;
            BarrierMonitoring monitoring;
            Real npv;
        };
        // Regression locks: QuantLib::MCBarrierEngine's own output for a fixed seed, sample count
        // and grid, not validated prices. testBarrierOptionReplication checks the engine on the
        // flat market against QuantLib's closed form; the smiles have no independent check here,
        // their locks pin the engine on the markets the profile executable measures.
        //
        // The values are clang's on x86-64, which does not contract to FMAs. gcc at
        // -march=x86-64-v3, which does, as CI's gcc legs do, moves the flat locks by up to
        // 5.9e-16 relative and the smile ones by up to 6.9e-6 on the bilinear surface and 2.1e-7
        // on the bicubic one: LocalVolSurface turns a last-bit difference in the Black variance
        // into a visible one in the local variance. The flat bound is the 1e-8 the other flat
        // locks hold across the CI matrix; the smile bound leaves three times the largest smile
        // move, rounded up to one digit. Neither is a bit-level criterion: a QuantLib change is
        // held to the profile executable's 17-digit NPV, the same double before and after the
        // change on one build.
        constexpr Real flatTolerance = 1e-8;
        constexpr Real smileTolerance = 3e-5;
        const std::vector<Lock> locks = {
            {Market::Flat, BarrierMonitoring::Discrete, 18.459122731997851},
            {Market::Flat, BarrierMonitoring::Continuous, 18.09121433559682},
            {Market::SmileBilinear, BarrierMonitoring::Discrete, 16.222683755096813},
            {Market::SmileBilinear, BarrierMonitoring::Continuous, 15.757714015308345},
            {Market::SmileBicubic, BarrierMonitoring::Discrete, 16.352950815657127},
            {Market::SmileBicubic, BarrierMonitoring::Continuous, 15.997770904041804},
        };
        for (const auto& lock : locks) {
            const auto setup = makeSetup(lock.market, lock.monitoring, lockSamples);
            const auto npv = setup.option->NPV();

            BOOST_TEST_MESSAGE(marketName(lock.market) << " " << monitoringName(lock.monitoring)
                                                       << " " << std::setprecision(17) << npv);
            BOOST_CHECK_CLOSE_FRACTION(
                lock.npv, npv, lock.market == Market::Flat ? flatTolerance : smileTolerance);
        }
    }

    BOOST_AUTO_TEST_CASE(testBarrierOptionReplication) { // NOLINT(misc-use-internal-linkage):
                                                         // the struct is the macro's
        BOOST_TEST_MESSAGE("BarrierOption replication test");

        const auto barrier = BarrierOptionData().barrier;
        const auto volatility = FlatMarketData().volatility;

        {
            const auto setup =
                makeSetup(Market::Flat, BarrierMonitoring::Discrete, replicationSamples);
            const auto npv = setup.option->NPV();

            // AnalyticBarrierEngine assumes continuous monitoring, while BiasedBarrierPathPricer
            // knocks out on the grid points after t = 0 only, so the replication takes the
            // Broadie-Glasserman-Kou barrier for the step the paths were monitored on.
            const auto replication =
                analyticPrice(setup, bgkShiftedBarrier(barrier, volatility, monitoringStep(setup)));

            // Measured residual 6.1e-4 relative. BiasedBarrierPathPricer draws no numbers of its
            // own, so the residual is the correction's own error on 255 steps plus the error of
            // the Sobol paths, both deterministic; at 2^17 and 2^18 paths it settles near 8.5e-4,
            // 8.2e-4 and 8.6e-4, the correction's share. The bound leaves three times the
            // residual, rounded up to one digit.
            BOOST_TEST_MESSAGE("discrete: MC " << npv << ", analytic " << replication
                                               << ", relative residual "
                                               << std::fabs(npv - replication) / replication);
            BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2e-3);
        }

        {
            const auto setup =
                makeSetup(Market::Flat, BarrierMonitoring::Continuous, replicationSamples);
            const auto npv = setup.option->NPV();

            // BarrierPathPricer monitors continuously, as AnalyticBarrierEngine assumes, so the
            // replication takes the barrier itself and no Broadie-Glasserman-Kou shift applies.
            const auto replication = analyticPrice(setup, barrier);

            // With flat r, q and sigma the step is exact, and BarrierPathPricer draws each step's
            // minimum from the Brownian bridge's own law at the constant volatility, so the
            // residual is sampling error alone. Unlike MCBonusClassicEngine, which weights a path
            // with its bridge survival probability, BarrierPathPricer knocks out on a minimum
            // drawn from MersenneTwister uniforms seeded 5, pseudo-random noise beyond the Sobol
            // paths' error: measured 4.1e-4 relative at 2^16, 5.5e-4 at 2^17 and 7.2e-4 at 2^18,
            // with no trend in the path count. The bound leaves nearly three times the largest.
            BOOST_TEST_MESSAGE("continuous: MC " << npv << ", analytic " << replication
                                                 << ", relative residual "
                                                 << std::fabs(npv - replication) / replication);
            BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2e-3);
        }
    }

    BOOST_AUTO_TEST_CASE(testBarrierOptionMonitoringOrder) { // NOLINT(misc-use-internal-linkage):
                                                             // the struct is the macro's
        BOOST_TEST_MESSAGE("BarrierOption monitoring order test");

        // The configuration of the flat locks in testBarrierOptionValuation.
        const auto discrete =
            makeSetup(Market::Flat, BarrierMonitoring::Discrete, lockSamples).option->NPV();
        const auto continuous =
            makeSetup(Market::Flat, BarrierMonitoring::Continuous, lockSamples).option->NPV();

        // Both pricers walk the same Sobol paths: the two setups build the same process, grid
        // and sequence generator. BiasedBarrierPathPricer knocks a path out where a grid point
        // after t = 0 is at or below the barrier, BarrierPathPricer where a step's sampled
        // minimum S_i * exp(y) is at or below it, with x = ln(S_{i+1} / S_i) and
        // y = (x - sqrt(x^2 - 2 sigma^2 dt ln u)) / 2. MersenneTwisterUniformRng draws u in
        // (0, 1), so -ln u > 0, the root exceeds |x|, and y < min(0, x): the minimum lies below
        // both ends of the step, and every grid knock-out is a continuous knock-out. The put
        // payoff is non-negative and the rebate zero, so on every path the continuous value is
        // at most the discrete one, and the price is strictly lower once a path that ends below
        // the strike is knocked out between grid points only.
        //
        // Rounding cannot reverse that: y undercuts min(0, x) by at least
        // c / (4 sqrt(x^2 + c)) with c = -2 sigma^2 dt ln u, and u <= 1 - 2^-33 keeps that above
        // 1.8e-14 on this grid for any step up to |x| = 0.5, over a hundred times the rounding
        // of a double.
        BOOST_TEST_MESSAGE("discrete " << discrete << ", continuous " << continuous);
        BOOST_CHECK_LT(continuous, discrete);
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
