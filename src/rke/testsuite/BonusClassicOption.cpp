// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "TestSuiteFixture.hpp"
#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/FlatTermStructures.hpp>
#include <rke/common/StepCacheMarkets.hpp>
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BinomialBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
#include <ql/methods/montecarlo/mctraits.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/pricingengines/barrier/analyticbinarybarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/volatility/equityfx/localvolsurface.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <iomanip>
#include <string_view>
#include <utility>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::TestSuite {
    // Barrier monitoring dates of the MC engine are its time grid points.
    constexpr Size mcTimeStepsPerYear = 100;
    // The FD grid and the tree's step count the benchmarks and profiles measure.
    using RKE::Common::fdSpaceGrid;
    using RKE::Common::fdTimeGrid;
    using RKE::Common::treeTimeSteps;

    namespace {
        // The probability that the barrier is touched and the probability that it never is and
        // S_T < B, i.e. that the bonus is paid: the two results MCBonusClassicEngine reports
        // beside the price, and what the analytic reference gives for them.
        struct Probabilities {
            Real hit;
            Real bonus;
        };

        // The engine's additional results after NPV() has run; result() throws if a key is
        // missing.
        Probabilities engineProbabilities(const BonusClassicOption& option) {
            return {
                option.result<Real>("barrierHitProbability"),
                option.result<Real>("bonusProbability"),
            };
        }

        // |value - reference| over |reference|, or over 1 where the reference is zero: a
        // probability's absolute scale, so a zero on both sides is no difference and a zero on
        // one side is the whole other value.
        Real relativeDifference(Real value, Real reference) {
            const auto scale = reference == 0.0 ? 1.0 : std::fabs(reference);
            return std::fabs(value - reference) / scale;
        }

        // Prices under LocalVolStepSingleVariate and SingleVariate, both monitoring modes, and
        // requires the prices and both probabilities within the relative tolerance.
        void checkLocalVolStepPrices(Date today,
                                     const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                                     Real tolerance) {
            auto option_data = RKE::Common::OptionData();
            option_data.ttm = Period(5, Months);
            const auto bonusClassicOption = RKE::Common::makeBonusClassicOption(option_data, today);

            for (const bool isBiased : {true, false}) {
                bonusClassicOption->setPricingEngine(
                    ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics,
                                                          LocalVolStepSingleVariate>>(
                        process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), isBiased, true,
                        42));
                const auto cached = bonusClassicOption->NPV();
                const auto cachedProbabilities = engineProbabilities(*bonusClassicOption);

                bonusClassicOption->setPricingEngine(
                    ext::make_shared<
                        MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                        process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), isBiased, true,
                        42));
                const auto plain = bonusClassicOption->NPV();
                const auto plainProbabilities = engineProbabilities(*bonusClassicOption);

                BOOST_TEST_MESSAGE((isBiased ? "discrete " : "continuous ")
                                   << cached << ", relative difference "
                                   << std::fabs(cached - plain) / plain);
                BOOST_CHECK_CLOSE_FRACTION(cached, plain, tolerance);

                // The probabilities are means of the same per-path quantities the price is
                // built from, so they agree to the same rounding. At 1,000 paths a hit
                // probability can be zero, which relativeDifference scales absolutely.
                BOOST_TEST_MESSAGE(
                    "hit " << cachedProbabilities.hit << ", relative difference "
                           << relativeDifference(cachedProbabilities.hit, plainProbabilities.hit)
                           << "; bonus " << cachedProbabilities.bonus << ", relative difference "
                           << relativeDifference(cachedProbabilities.bonus,
                                                 plainProbabilities.bonus));
                BOOST_CHECK_LE(relativeDifference(cachedProbabilities.hit, plainProbabilities.hit),
                               tolerance);
                BOOST_CHECK_LE(
                    relativeDifference(cachedProbabilities.bonus, plainProbabilities.bonus),
                    tolerance);
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
                              const RKE::Common::OptionData& data,
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

        // AnalyticBinaryBarrierEngine (Haug, "The complete guide to option pricing formulas",
        // 2nd ed., p. 176 ff.) prices a cash-or-nothing payoff paid at expiry under continuous
        // monitoring of the barrier given here. Its calculate() takes an AmericanExercise with
        // payoffAtExpiry = true whose first date is at or before the volatility's reference
        // date, so the exercise starts today. Paying 1 at expiry, the down-and-in call and put
        // struck at the bonus level together pay 1 iff the barrier was touched, and the
        // down-and-out put, the engine's "strike >= barrier" branch, pays 1 iff it never was
        // and S_T < B. The curve is deterministic, so E[D(T) 1_A] = D(T) P(A) and each price
        // over D(T) is the probability of its event.
        Probabilities
        referenceProbabilities(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                               const RKE::Common::OptionData& data,
                               Date today,
                               Date exerciseDate,
                               Real barrier) {
            const auto exercise = ext::make_shared<AmericanExercise>(today, exerciseDate, true);
            const auto cashOrNothingPrice = [&](Barrier::Type barrierType,
                                                Option::Type optionType) {
                auto option = BarrierOption(
                    barrierType, barrier, 0.0,
                    ext::make_shared<CashOrNothingPayoff>(optionType, data.bonusLevel, 1.0),
                    exercise);
                option.setPricingEngine(ext::make_shared<AnalyticBinaryBarrierEngine>(process));
                return option.NPV();
            };
            const auto discount = process->riskFreeRate()->discount(exerciseDate);

            return {
                (cashOrNothingPrice(Barrier::DownIn, Option::Call) +
                 cashOrNothingPrice(Barrier::DownIn, Option::Put)) /
                    discount,
                cashOrNothingPrice(Barrier::DownOut, Option::Put) / discount,
            };
        }

        // The flat market every case prices: the option with its maturity cut to 5M and the
        // process as of 22 Jun 2025, which the helper also makes the evaluation date.
        struct FlatCase {
            RKE::Common::OptionData optionData;
            RKE::Common::MarketData marketData;
            Date today;
            Date exerciseDate;
            ext::shared_ptr<GeneralizedBlackScholesProcess> process;
            ext::shared_ptr<BonusClassicOption> option;
        };

        FlatCase makeFlatCase(bool forceDiscretization = false) {
            auto flat = FlatCase();
            flat.today = RKE::Common::evaluationDate();
            Settings::instance().evaluationDate() = flat.today;
            flat.optionData.ttm = Period(5, Months);
            flat.process =
                flat.marketData.makeGeneralizedBlackScholesProcess(flat.today, forceDiscretization);
            flat.option = RKE::Common::makeBonusClassicOption(flat.optionData, flat.today);
            flat.exerciseDate = flat.option->exercise()->lastDate();
            return flat;
        }

        // The smile markets the FD engine prices under local volatility: the same option and
        // date, the process RKE::Common::SmileMarketData builds on its bilinear or bicubic
        // surface, zero curves and a Black variance surface on Actual360 and NullCalendar.
        struct SmileCase {
            RKE::Common::OptionData optionData;
            Date today;
            Date exerciseDate;
            ext::shared_ptr<GeneralizedBlackScholesProcess> process;
            ext::shared_ptr<BonusClassicOption> option;
        };

        SmileCase makeSmileCase(bool isBicubic) {
            auto smile = SmileCase();
            smile.today = RKE::Common::evaluationDate();
            Settings::instance().evaluationDate() = smile.today;
            smile.optionData.ttm = Period(5, Months);
            smile.process = RKE::Common::SmileMarketData().makeGeneralizedBlackScholesProcess(
                smile.today, isBicubic);
            smile.option = RKE::Common::makeBonusClassicOption(smile.optionData, smile.today);
            smile.exerciseDate = smile.option->exercise()->lastDate();
            return smile;
        }

        std::string_view surfaceName(bool isBicubic) {
            return isBicubic ? "bicubic" : "bilinear";
        }

        // The FD engine on the test grid, monitored on the Monte-Carlo grid or continuously,
        // with the probabilities on and the local volatility as asked; the damping steps and
        // the scheme are the defaults, spelled out to reach the flags behind them.
        ext::shared_ptr<FdBlackScholesBonusClassicEngine>
        makeFdEngine(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                     bool isBiased,
                     bool localVol,
                     Size tGrid = fdTimeGrid,
                     Size xGrid = fdSpaceGrid) {
            return ext::make_shared<FdBlackScholesBonusClassicEngine>(
                process, isBiased ? mcTimeStepsPerYear : Null<Size>(), tGrid, xGrid, 0,
                FdmSchemeDesc::TrBDF2(), true, localVol);
        }

        // The Monte-Carlo engine under the Euler step through the local volatility, the
        // reference the FD engine's smile prices are checked against.
        ext::shared_ptr<MCBonusClassicEngine<LowDiscrepancy, Statistics, LocalVolStepSingleVariate>>
        makeLocalVolMcEngine(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                             bool isBiased,
                             Size samples) {
            return ext::make_shared<
                MCBonusClassicEngine<LowDiscrepancy, Statistics, LocalVolStepSingleVariate>>(
                process, mcTimeStepsPerYear, samples, samples + 1, Null<Real>(), isBiased, true,
                42);
        }

        // The bounds of one FD-versus-MC comparison: relative on the price, absolute on the
        // probabilities.
        struct FdVersusMcBounds {
            Real price;
            Real hit;
            Real bonus;
        };

        // Prices the smile under the FD engine and under the Euler Monte-Carlo engine at 50,000
        // paths, in one monitoring mode, requires the price and both probabilities within the
        // bounds, and returns the FD price.
        Real checkFdSmileVersusMc(const SmileCase& smile, bool isBiased, FdVersusMcBounds bounds) {
            const auto fdEngine = makeFdEngine(smile.process, isBiased, true);
            smile.option->setPricingEngine(fdEngine);
            const auto fd = smile.option->NPV();
            const auto fdProbabilities = engineProbabilities(*smile.option);

            // The Euler step through the same LocalVolSurface; about ten seconds for both
            // modes, the surface rebuilding a spline on every lookup.
            const auto mcEngine = makeLocalVolMcEngine(smile.process, isBiased, 50'000);
            smile.option->setPricingEngine(mcEngine);
            const auto mc = smile.option->NPV();
            const auto mcProbabilities = engineProbabilities(*smile.option);

            if (isBiased) {
                // Both engines monitor the same product: the same 42 points after t = 0.
                const auto fdGrid = fdEngine->timeGrid();
                const auto mcGrid = mcEngine->timeGrid();
                BOOST_CHECK_EQUAL(fdGrid.size() - 1, Size(42));
                BOOST_REQUIRE_EQUAL(fdGrid.size(), mcGrid.size());
                for (Size i = 0; i < fdGrid.size(); ++i) {
                    BOOST_CHECK_EQUAL(fdGrid[i], mcGrid[i]);
                }
            }

            BOOST_TEST_MESSAGE((isBiased ? "discrete" : "continuous")
                               << ": FD " << fd << ", MC " << mc << ", relative difference "
                               << std::fabs(fd - mc) / mc);
            BOOST_TEST_MESSAGE("  hit FD " << fdProbabilities.hit << ", MC " << mcProbabilities.hit
                                           << ", absolute difference "
                                           << std::fabs(fdProbabilities.hit - mcProbabilities.hit));
            BOOST_TEST_MESSAGE("  bonus FD "
                               << fdProbabilities.bonus << ", MC " << mcProbabilities.bonus
                               << ", absolute difference "
                               << std::fabs(fdProbabilities.bonus - mcProbabilities.bonus));
            BOOST_CHECK_CLOSE_FRACTION(mc, fd, bounds.price);
            BOOST_CHECK_SMALL(fdProbabilities.hit - mcProbabilities.hit, bounds.hit);
            BOOST_CHECK_SMALL(fdProbabilities.bonus - mcProbabilities.bonus, bounds.bonus);
            return fd;
        }
    }

    BOOST_FIXTURE_TEST_SUITE(RkeQLExtTestSuite, TestSuiteFixture)

    BOOST_AUTO_TEST_SUITE(BonusClassicOptionTests)

    BOOST_AUTO_TEST_CASE(
        testBonusClassicPayoff) { // NOLINT(misc-use-internal-linkage): the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicPayoff test");

        const auto data = RKE::Common::OptionData();
        const auto payoff = BonusClassicPayoff(data.barrier, data.bonusLevel);

        BOOST_CHECK_EQUAL(payoff(60.00), 60.00);
        // Pins the boundary convention: at the barrier itself the bonus is already gone.
        BOOST_CHECK_EQUAL(payoff(data.barrier), data.barrier);
        BOOST_CHECK_EQUAL(payoff(100.00), data.bonusLevel);
        BOOST_CHECK_EQUAL(payoff(125.00), 125.00);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOption) { // NOLINT(misc-use-internal-linkage): the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption test");

        const auto flat = makeFlatCase();

        BOOST_CHECK_EQUAL(flat.option->exercise()->type(), Exercise::European);
        BOOST_CHECK_EQUAL(flat.option->exercise()->lastDate(), Date(22, Nov, 2025));
        BOOST_CHECK_EQUAL(flat.option->barrier(), flat.optionData.barrier);
        BOOST_CHECK_EQUAL(flat.option->bonusLevel(), flat.optionData.bonusLevel);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionEngineGuards) { // NOLINT(misc-use-internal-linkage):
                                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption engines fail with a QuantLib::Error on a null "
                           "process and on timeGrid() before the first NPV()");

        const auto flat = makeFlatCase();

        BOOST_CHECK_THROW(MCBonusClassicEngine<LowDiscrepancy>(nullptr, mcTimeStepsPerYear, 1'000,
                                                               1'001, Null<Real>(), true, true, 42),
                          Error);
        BOOST_CHECK_THROW(FdBlackScholesBonusClassicEngine(nullptr, mcTimeStepsPerYear), Error);
        BOOST_CHECK_THROW(BinomialBonusClassicEngine<CoxRossRubinstein>(nullptr, treeTimeSteps),
                          Error);

        // The arguments, the exercise among them, are empty until an instrument sets them up.
        const MCBonusClassicEngine<LowDiscrepancy> mcEngine(flat.process, mcTimeStepsPerYear, 1'000,
                                                            1'001, Null<Real>(), true, true, 42);
        BOOST_CHECK_THROW(static_cast<void>(mcEngine.timeGrid()), Error);
        const FdBlackScholesBonusClassicEngine fdEngine(flat.process, mcTimeStepsPerYear);
        BOOST_CHECK_THROW(static_cast<void>(fdEngine.timeGrid()), Error);
        const BinomialBonusClassicEngine<CoxRossRubinstein> treeEngine(flat.process, treeTimeSteps);
        BOOST_CHECK_THROW(static_cast<void>(treeEngine.timeGrid()), Error);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionNonPositiveLevels) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption rejects a barrier or bonus level at or below "
                           "zero before any engine runs");

        const auto flat = makeFlatCase();

        // One engine per method; the FD engine in continuous mode is the one whose grid would
        // otherwise take log(0) and fail deep inside the solver.
        const std::vector<ext::shared_ptr<PricingEngine>> engines = {
            ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
                flat.process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), true, true, 42),
            ext::make_shared<FdBlackScholesBonusClassicEngine>(flat.process),
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(flat.process,
                                                                            treeTimeSteps),
        };
        for (const auto& engine : engines) {
            for (const auto& [barrier, bonusLevel] : {
                     std::pair{0.0, flat.optionData.bonusLevel},
                     std::pair{flat.optionData.barrier, 0.0},
                 }) {
                BonusClassicOption option(barrier, bonusLevel, flat.exerciseDate);
                option.setPricingEngine(engine);
                BOOST_CHECK_THROW(static_cast<void>(option.NPV()), Error);
            }
        }
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionValuation) { // NOLINT(misc-use-internal-linkage):
                                                            // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption valuation test");

        const auto flat = makeFlatCase();

        // The Null<Real>() tolerance is mandatory, not a default: with no error estimate under
        // LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and maxSamples
        // never applies.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);

        flat.option->setPricingEngine(mcEngine);
        const auto npv = flat.option->NPV();

        // Regression lock. The low-discrepancy sequence is deterministic for a fixed seed
        // and time grid, so this pins the engine to its own output; it is not an
        // externally validated price. See testBonusClassicOptionReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(119.59926159811715, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionReplication) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption replication test");

        const auto flat = makeFlatCase();

        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        flat.option->setPricingEngine(mcEngine);
        const auto npv = flat.option->NPV();

        // AnalyticBarrierEngine assumes continuous monitoring, while the engine monitors on
        // its time grid only, so the replication takes the Broadie-Glasserman-Kou barrier.
        // Read from the engine's own grid, so the correction always uses the step the paths
        // were monitored on.
        const auto dt = mcEngine->timeGrid().dt(0);
        const auto replication = replicationPrice(
            flat.process, flat.optionData, flat.exerciseDate,
            bgkShiftedBarrier(flat.optionData.barrier, flat.marketData.volatility, dt));

        // Measured residual 1.3e-5 relative; the correction is O(1 / sqrt(steps)) and the
        // grid has 42 steps. Both sides are deterministic, so this is model error, not noise.
        // The bound leaves seven times the residual: the 6.3e-5 sampling error the continuous
        // replication measures on the same paths is in it too.
        BOOST_TEST_MESSAGE("MC " << npv << ", relative residual "
                                 << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionContinuousValuation) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption continuous valuation test");

        const auto flat = makeFlatCase();

        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42);

        flat.option->setPricingEngine(mcEngine);
        const auto npv = flat.option->NPV();

        // Regression lock, as in testBonusClassicOptionValuation: the engine's own output for
        // a fixed seed and grid, not an externally validated price. See
        // testBonusClassicOptionContinuousReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(119.50805355382055, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionContinuousReplication) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption continuous replication test");

        const auto flat = makeFlatCase();

        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42);
        flat.option->setPricingEngine(mcEngine);
        const auto npv = flat.option->NPV();

        // The engine now monitors continuously, as AnalyticBarrierEngine assumes, so the put
        // takes the barrier itself and no Broadie-Glasserman-Kou shift applies.
        const auto replication = replicationPrice(flat.process, flat.optionData, flat.exerciseDate,
                                                  flat.optionData.barrier);

        // With flat r, q and sigma the bridge is exact, so the residual is sampling error
        // alone: measured 6.3e-5 relative at 50,000 paths, 1.2e-5 at 200,000 and 7.3e-6 at
        // 400,000. The bound leaves three times the 50,000-path residual.
        BOOST_TEST_MESSAGE("MC " << npv << ", relative residual "
                                 << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionProbabilities) { // NOLINT(misc-use-internal-linkage):
                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption probabilities test");

        const auto flat = makeFlatCase();

        // The configuration of testBonusClassicOptionValuation's lock.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        flat.option->setPricingEngine(mcEngine);
        static_cast<void>(flat.option->NPV());
        const auto probabilities = engineProbabilities(*flat.option);

        // Touching the barrier and paying the bonus are disjoint, and a path that never
        // touches it and ends at or above the bonus level is in neither.
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);
        BOOST_CHECK_LT(probabilities.hit + probabilities.bonus, 1.0);

        // Regression lock, as in testBonusClassicOptionValuation: the engine's own output for
        // a fixed seed and grid, not externally validated probabilities. The independent check
        // follows below.
        BOOST_CHECK_CLOSE_FRACTION(0.0067000000000000002, probabilities.hit, 1e-8);
        BOOST_CHECK_CLOSE_FRACTION(0.93015999999999999, probabilities.bonus, 1e-8);

        // AnalyticBinaryBarrierEngine assumes continuous monitoring, while the engine monitors
        // on its time grid only, so the reference takes the Broadie-Glasserman-Kou barrier for
        // the step the paths were monitored on.
        const auto dt = mcEngine->timeGrid().dt(0);
        const auto reference = referenceProbabilities(
            flat.process, flat.optionData, flat.today, flat.exerciseDate,
            bgkShiftedBarrier(flat.optionData.barrier, flat.marketData.volatility, dt));

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // Measured residuals 7.57e-5 absolute for hit and 1.02e-4 absolute for bonus. Both mix
        // the correction's own O(1 / sqrt(steps)) error on 42 steps with the sampling error of
        // the indicator estimator, which is not measured separately; the continuous case below
        // measures its own, bridge-weighted estimator. Each bound leaves about three times its
        // residual, rounded up to one digit.
        BOOST_TEST_MESSAGE("MC hit " << probabilities.hit << ", reference " << reference.hit
                                     << ", absolute residual "
                                     << std::fabs(probabilities.hit - reference.hit));
        BOOST_TEST_MESSAGE("MC bonus " << probabilities.bonus << ", reference " << reference.bonus
                                       << ", absolute residual "
                                       << std::fabs(probabilities.bonus - reference.bonus));
        BOOST_CHECK_SMALL(probabilities.hit - reference.hit, 3e-4);
        BOOST_CHECK_SMALL(probabilities.bonus - reference.bonus, 4e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionContinuousProbabilities) { // NOLINT(misc-use-internal-linkage):
                                                         // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption continuous probabilities test");

        const auto flat = makeFlatCase();

        // The configuration of testBonusClassicOptionContinuousValuation's lock.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42);
        flat.option->setPricingEngine(mcEngine);
        static_cast<void>(flat.option->NPV());
        const auto probabilities = engineProbabilities(*flat.option);

        // Each path contributes its bridge probabilities, 1 - P to hit and P * 1{S_T < B} to
        // bonus, so the means are still disjoint probabilities with the no-hit, no-bonus
        // paths' share left over.
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);
        BOOST_CHECK_LT(probabilities.hit + probabilities.bonus, 1.0);

        // Regression lock, as in testBonusClassicOptionContinuousValuation: the engine's own
        // output for a fixed seed and grid, not externally validated probabilities. The
        // independent check follows below.
        BOOST_CHECK_CLOSE_FRACTION(0.0086433804772671641, probabilities.hit, 1e-8);
        BOOST_CHECK_CLOSE_FRACTION(0.92821661952273349, probabilities.bonus, 1e-8);

        // The engine monitors continuously, as AnalyticBinaryBarrierEngine assumes, so the
        // reference takes the barrier itself and no Broadie-Glasserman-Kou shift applies.
        const auto reference = referenceProbabilities(flat.process, flat.optionData, flat.today,
                                                      flat.exerciseDate, flat.optionData.barrier);

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // With flat r, q and sigma the bridge is exact, so the residual is sampling error
        // alone: measured 1.9e-4 absolute for hit and 2.2e-4 absolute for bonus at 50,000
        // paths, 4.3e-5 and 4.4e-5 at 200,000. Each bound leaves three times its 50,000-path
        // residual, rounded up to one digit.
        BOOST_TEST_MESSAGE("MC hit " << probabilities.hit << ", reference " << reference.hit
                                     << ", absolute residual "
                                     << std::fabs(probabilities.hit - reference.hit));
        BOOST_TEST_MESSAGE("MC bonus " << probabilities.bonus << ", reference " << reference.bonus
                                       << ", absolute residual "
                                       << std::fabs(probabilities.bonus - reference.bonus));
        BOOST_CHECK_SMALL(probabilities.hit - reference.hit, 6e-4);
        BOOST_CHECK_SMALL(probabilities.bonus - reference.bonus, 7e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionMonitoringOrder) { // NOLINT(misc-use-internal-linkage):
                                                 // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption monitoring order test");

        const auto flat = makeFlatCase();

        flat.option->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42));
        const auto discrete = flat.option->NPV();
        const auto discreteProbabilities = engineProbabilities(*flat.option);

        flat.option->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), false, true, 42));
        const auto continuous = flat.option->NPV();
        const auto continuousProbabilities = engineProbabilities(*flat.option);

        // Continuous monitoring sees every crossing the grid sees and more, and a knock-out
        // only removes the bonus put, so on the same paths the continuous price is lower.
        BOOST_TEST_MESSAGE("discrete " << discrete << ", continuous " << continuous);
        BOOST_CHECK_LT(continuous, discrete);

        // The bridge adds a crossing probability to every surviving path and takes none away,
        // so the hit probability rises and the bonus probability, its complement on the paths
        // ending below the bonus level, falls.
        BOOST_TEST_MESSAGE("discrete hit " << discreteProbabilities.hit << ", continuous hit "
                                           << continuousProbabilities.hit << "; discrete bonus "
                                           << discreteProbabilities.bonus << ", continuous bonus "
                                           << continuousProbabilities.bonus);
        BOOST_CHECK_GT(continuousProbabilities.hit, discreteProbabilities.hit);
        BOOST_CHECK_LT(continuousProbabilities.bonus, discreteProbabilities.bonus);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionPathGeneratorTraits) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption prices the same under CachedStepSingleVariate and "
                           "SingleVariate");

        const auto flat = makeFlatCase();

        for (const bool isBiased : {true, false}) {
            flat.option->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
                flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), isBiased, true,
                42));
            const auto cached = flat.option->NPV();
            const auto cachedProbabilities = engineProbabilities(*flat.option);

            flat.option->setPricingEngine(
                ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                    flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), isBiased, true,
                    42));
            const auto plain = flat.option->NPV();
            const auto plainProbabilities = engineProbabilities(*flat.option);

            // Exact on purpose: the cached step is the double QuantLib::PathGenerator evolves,
            // so the traits move no price.
            BOOST_TEST_MESSAGE((isBiased ? "discrete " : "continuous ") << cached);
            BOOST_CHECK_EQUAL(cached, plain);

            // The same paths and, in continuous mode, the same step variance the cache
            // reproduces from the process, so the probabilities are the same doubles too.
            BOOST_TEST_MESSAGE("hit " << cachedProbabilities.hit << ", bonus "
                                      << cachedProbabilities.bonus);
            BOOST_CHECK_EQUAL(cachedProbabilities.hit, plainProbabilities.hit);
            BOOST_CHECK_EQUAL(cachedProbabilities.bonus, plainProbabilities.bonus);
        }
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionLocalVolStep) { // NOLINT(misc-use-internal-linkage):
                                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption prices the Euler step under "
                           "LocalVolStepSingleVariate as under SingleVariate");

        const auto today = RKE::Common::evaluationDate();
        Settings::instance().evaluationDate() = today;

        // The forced discretization takes the Euler step through LocalConstantVol, where the
        // paths agree to their rounding.
        checkLocalVolStepPrices(
            today, RKE::Common::MarketData().makeGeneralizedBlackScholesProcess(today, true),
            1.0e-12);
        // The benchmarks' bilinear smile. Under it LocalVolSurface amplifies a last bit along a
        // path: without contraction the prices are the same double; where the compiler
        // contracts to FMAs they differ, by up to 2e-6 relative at production sample counts,
        // as PathGeneration in the common header records.
        checkLocalVolStepPrices(
            today, RKE::Common::SmileMarketData().makeGeneralizedBlackScholesProcess(today, false),
            1.0e-6);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionInexactStep) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption refuses an inexact step under "
                           "CachedStepSingleVariate and prices it under SingleVariate");

        const auto flat = makeFlatCase(true);

        // The volatility's type alone passes for exact; only the probe against evolve()
        // catches the forced discretization, and the engine fails loud instead of falling back.
        flat.option->setPricingEngine(ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), false, true, 42));
        BOOST_CHECK_THROW(flat.option->NPV(), Error);

        flat.option->setPricingEngine(
            ext::make_shared<MCBonusClassicEngine<LowDiscrepancy, Statistics, SingleVariate>>(
                flat.process, mcTimeStepsPerYear, 1'000, 1'001, Null<Real>(), false, true, 42));
        BOOST_CHECK_NO_THROW(flat.option->NPV());
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdContinuousValuation) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD continuous valuation test");

        const auto flat = makeFlatCase();

        flat.option->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, Null<Size>(), fdTimeGrid, fdSpaceGrid));
        const auto npv = flat.option->NPV();

        // Regression lock: the engine's own output on this grid and the default TrBDF2 scheme,
        // not an externally validated price. See testBonusClassicOptionFdContinuousReplication
        // for that.
        BOOST_CHECK_CLOSE_FRACTION(119.50008395648202, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdContinuousReplication) { // NOLINT(misc-use-internal-linkage):
                                                         // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD continuous replication test");

        const auto flat = makeFlatCase();

        // std::exp(std::log(70.0)) lies above 70, so a grid starting at std::log(70.0) would pay
        // the bonus on its first node at maturity; 90 maps back to itself.
        for (const Real barrier : {flat.optionData.barrier, 90.0}) {
            auto optionData = flat.optionData;
            optionData.barrier = barrier;
            const auto bonusClassicOption =
                RKE::Common::makeBonusClassicOption(optionData, flat.today);
            bonusClassicOption->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
                flat.process, Null<Size>(), fdTimeGrid, fdSpaceGrid));
            const auto npv = bonusClassicOption->NPV();

            // Both sides monitor continuously, so the put takes the barrier itself.
            const auto replication =
                replicationPrice(flat.process, flat.optionData, flat.exerciseDate, barrier);

            // Measured residuals 3.3e-6 relative at barrier 70 and 5.2e-6 at 90 under the
            // default TrBDF2 scheme, 2.4e-7 and 1.4e-6 at 800 steps and 1,600 nodes: QuantLib
            // imposes the Dirichlet value on the barrier node after each implicit solve, so the
            // next node is coupled to the unconstrained one. The bound leaves nearly three
            // times the larger residual.
            BOOST_TEST_MESSAGE("barrier " << barrier << ": " << npv << ", relative residual "
                                          << std::fabs(npv - replication) / replication);
            BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1.5e-5);
        }
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionFdValuation) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD valuation test");

        const auto flat = makeFlatCase();

        flat.option->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid));
        const auto npv = flat.option->NPV();

        // Regression lock: the engine's own output on this grid and scheme, not an externally
        // validated price. See testBonusClassicOptionFdReplication and
        // testBonusClassicOptionFdVersusMc for that.
        BOOST_CHECK_CLOSE_FRACTION(119.58777203868031, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdReplication) { // NOLINT(misc-use-internal-linkage):
                                               // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD replication test");

        const auto flat = makeFlatCase();

        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid);
        flat.option->setPricingEngine(fdEngine);
        const auto npv = flat.option->NPV();

        // The engine monitors on its time grid, so the replication takes the
        // Broadie-Glasserman-Kou barrier for the grid's step.
        const auto dt = fdEngine->timeGrid().dt(0);
        const auto replication = replicationPrice(
            flat.process, flat.optionData, flat.exerciseDate,
            bgkShiftedBarrier(flat.optionData.barrier, flat.marketData.volatility, dt));

        // Measured residual 8.3e-5 relative. The engine at 6,400 nodes and 3,200 steps, 5.2e-8
        // from its value at half that grid, is 7.7e-5 below the replication, so nearly all of it
        // is the correction's own error on 42 steps; the Monte Carlo engine's is 1.3e-5. The
        // bound leaves three times the residual.
        BOOST_TEST_MESSAGE("FD " << npv << ", relative residual "
                                 << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 2.5e-4);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionFdVersusMc) { // NOLINT(misc-use-internal-linkage):
                                                             // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD versus MC test");

        const auto flat = makeFlatCase();

        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid);
        flat.option->setPricingEngine(fdEngine);
        const auto fd = flat.option->NPV();

        // The configuration of testBonusClassicOptionValuation's lock.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        flat.option->setPricingEngine(mcEngine);
        const auto mc = flat.option->NPV();

        // Both engines monitor the same product: the same 42 points after t = 0.
        const auto fdGrid = fdEngine->timeGrid();
        const auto mcGrid = mcEngine->timeGrid();
        BOOST_CHECK_EQUAL(fdGrid.size() - 1, Size(42));
        BOOST_REQUIRE_EQUAL(fdGrid.size(), mcGrid.size());
        for (Size i = 0; i < fdGrid.size(); ++i) {
            BOOST_CHECK_EQUAL(fdGrid[i], mcGrid[i]);
        }

        // Measured residual 9.6e-5 relative, the sum of two deterministic errors: the engine at
        // 6,400 nodes and 3,200 steps is 5.5e-6 above this grid's value and 9.1e-5 below the
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

        const auto flat = makeFlatCase();

        const auto discreteEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid);
        flat.option->setPricingEngine(discreteEngine);
        const auto discrete = flat.option->NPV();

        const auto continuousEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, Null<Size>(), fdTimeGrid, fdSpaceGrid);
        flat.option->setPricingEngine(continuousEngine);
        const auto continuous = flat.option->NPV();

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
        testBonusClassicOptionFdProbabilities) { // NOLINT(misc-use-internal-linkage):
                                                 // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD probabilities test");

        const auto flat = makeFlatCase();

        // The grid of testBonusClassicOptionFdValuation's lock with the probabilities on; the
        // damping steps and the scheme are the defaults, spelled out to reach the flag.
        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(),
            true);
        flat.option->setPricingEngine(fdEngine);
        static_cast<void>(flat.option->NPV());
        const auto probabilities = engineProbabilities(*flat.option);

        // Touching the barrier and paying the bonus are disjoint, and a path that never
        // touches it and ends at or above the bonus level is in neither.
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);
        BOOST_CHECK_LT(probabilities.hit + probabilities.bonus, 1.0);

        // Regression lock, as in testBonusClassicOptionFdValuation: the engine's own output on
        // this grid and scheme, not externally validated probabilities. The independent check
        // follows below.
        BOOST_CHECK_CLOSE_FRACTION(0.0069741235334948581, probabilities.hit, 1e-8);
        BOOST_CHECK_CLOSE_FRACTION(0.92987764314443611, probabilities.bonus, 1e-8);

        // AnalyticBinaryBarrierEngine assumes continuous monitoring, while the engine monitors
        // on its time grid only, so the reference takes the Broadie-Glasserman-Kou barrier for
        // the grid's step.
        const auto dt = fdEngine->timeGrid().dt(0);
        const auto reference = referenceProbabilities(
            flat.process, flat.optionData, flat.today, flat.exerciseDate,
            bgkShiftedBarrier(flat.optionData.barrier, flat.marketData.volatility, dt));

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // Measured residuals 1.98e-4 absolute for hit and 1.81e-4 absolute for bonus. Both are
        // deterministic: the engine at 16 times both grids, 6,400 nodes and 3,200 steps, is
        // 1.4e-5 below this grid's hit and 3.4e-6 below its bonus, and still 1.84e-4 off the
        // reference on each, so nearly all of it is the correction's own O(1 / sqrt(steps))
        // error on 42 steps; the bonus indicator's jump at the bonus level lies between nodes
        // and is averaged over its cell. Each bound leaves about three times its residual,
        // rounded up to one digit.
        BOOST_TEST_MESSAGE("FD hit " << probabilities.hit << ", reference " << reference.hit
                                     << ", absolute residual "
                                     << std::fabs(probabilities.hit - reference.hit));
        BOOST_TEST_MESSAGE("FD bonus " << probabilities.bonus << ", reference " << reference.bonus
                                       << ", absolute residual "
                                       << std::fabs(probabilities.bonus - reference.bonus));
        BOOST_CHECK_SMALL(probabilities.hit - reference.hit, 6e-4);
        BOOST_CHECK_SMALL(probabilities.bonus - reference.bonus, 6e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdContinuousProbabilities) { // NOLINT(misc-use-internal-linkage):
                                                           // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD continuous probabilities test");

        const auto flat = makeFlatCase();

        // The grid of testBonusClassicOptionFdContinuousValuation's lock with the
        // probabilities on; the damping steps and the scheme are the defaults, spelled out to
        // reach the flag.
        flat.option->setPricingEngine(ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, Null<Size>(), fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(), true));
        static_cast<void>(flat.option->NPV());
        const auto probabilities = engineProbabilities(*flat.option);

        // Touching the barrier and paying the bonus are disjoint, and a path that never
        // touches it and ends at or above the bonus level is in neither.
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);
        BOOST_CHECK_LT(probabilities.hit + probabilities.bonus, 1.0);

        // Regression lock, as in testBonusClassicOptionFdContinuousValuation: the engine's own
        // output on this grid and scheme, not externally validated probabilities. The
        // independent check follows below.
        BOOST_CHECK_CLOSE_FRACTION(0.0088426988831812846, probabilities.hit, 1e-8);
        BOOST_CHECK_CLOSE_FRACTION(0.92796933820531924, probabilities.bonus, 1e-8);

        // The engine monitors continuously, as AnalyticBinaryBarrierEngine assumes, so the
        // reference takes the barrier itself and no Broadie-Glasserman-Kou shift applies.
        const auto reference = referenceProbabilities(flat.process, flat.optionData, flat.today,
                                                      flat.exerciseDate, flat.optionData.barrier);

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // Both sides monitor continuously, so the residual is the grid's discretisation error
        // alone: measured 9.1e-6 absolute for hit and 3.1e-5 absolute for bonus. The bonus
        // residual is the larger because the indicator's jump at the bonus level lies between
        // nodes and is averaged over its cell, while the hit indicator's jump sits on the node
        // at ln H. Each bound leaves about three times its residual, rounded up to one digit.
        BOOST_TEST_MESSAGE("FD hit " << probabilities.hit << ", reference " << reference.hit
                                     << ", absolute residual "
                                     << std::fabs(probabilities.hit - reference.hit));
        BOOST_TEST_MESSAGE("FD bonus " << probabilities.bonus << ", reference " << reference.bonus
                                       << ", absolute residual "
                                       << std::fabs(probabilities.bonus - reference.bonus));
        BOOST_CHECK_SMALL(probabilities.hit - reference.hit, 3e-5);
        BOOST_CHECK_SMALL(probabilities.bonus - reference.bonus, 1e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdProbabilitiesVersusMc) { // NOLINT(misc-use-internal-linkage):
                                                         // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD probabilities versus MC test");

        const auto flat = makeFlatCase();

        const auto fdEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(),
            true);
        flat.option->setPricingEngine(fdEngine);
        static_cast<void>(flat.option->NPV());
        const auto fd = engineProbabilities(*flat.option);

        // The configuration of testBonusClassicOptionProbabilities' lock.
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            flat.process, mcTimeStepsPerYear, 50'000, 50'001, Null<Real>(), true, true, 42);
        flat.option->setPricingEngine(mcEngine);
        static_cast<void>(flat.option->NPV());
        const auto mc = engineProbabilities(*flat.option);

        // Both engines monitor the same events: the same 42 points after t = 0.
        const auto fdGrid = fdEngine->timeGrid();
        const auto mcGrid = mcEngine->timeGrid();
        BOOST_CHECK_EQUAL(fdGrid.size() - 1, Size(42));
        BOOST_REQUIRE_EQUAL(fdGrid.size(), mcGrid.size());
        for (Size i = 0; i < fdGrid.size(); ++i) {
            BOOST_CHECK_EQUAL(fdGrid[i], mcGrid[i]);
        }

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // Measured residuals 2.74e-4 absolute for hit and 2.82e-4 absolute for bonus, the sum
        // of two deterministic errors: the grid's discretisation error and the Monte Carlo
        // lock's own distance from the discrete probabilities at 50,000 low-discrepancy paths,
        // which testBonusClassicOptionProbabilities records against the analytic reference as
        // 7.57e-5 and 1.02e-4 absolute. They add because the two engines sit on opposite sides
        // of that reference: testBonusClassicOptionFdProbabilities measures the grid 1.98e-4
        // above it for hit and 1.81e-4 below it for bonus, the Monte Carlo lock below and above.
        // Each bound leaves about three times its residual, rounded up to one digit.
        BOOST_TEST_MESSAGE("FD hit " << fd.hit << ", MC hit " << mc.hit << ", absolute difference "
                                     << std::fabs(fd.hit - mc.hit));
        BOOST_TEST_MESSAGE("FD bonus " << fd.bonus << ", MC bonus " << mc.bonus
                                       << ", absolute difference "
                                       << std::fabs(fd.bonus - mc.bonus));
        BOOST_CHECK_SMALL(fd.hit - mc.hit, 9e-4);
        BOOST_CHECK_SMALL(fd.bonus - mc.bonus, 9e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdProbabilitiesOptIn) { // NOLINT(misc-use-internal-linkage):
                                                      // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD engine reports the probabilities only when "
                           "asked, and the same price either way");

        const auto flat = makeFlatCase();

        // The default: the price alone, and reading a probability fails loud instead of
        // returning a stale or zero value.
        const auto plainEngine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid);
        BOOST_CHECK(!plainEngine->calculatesProbabilities());
        flat.option->setPricingEngine(plainEngine);
        const auto plain = flat.option->NPV();
        BOOST_CHECK_THROW(static_cast<void>(flat.option->result<Real>("barrierHitProbability")),
                          Error);
        BOOST_CHECK_THROW(static_cast<void>(flat.option->result<Real>("bonusProbability")), Error);

        const auto engine = ext::make_shared<FdBlackScholesBonusClassicEngine>(
            flat.process, mcTimeStepsPerYear, fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(),
            true);
        BOOST_CHECK(engine->calculatesProbabilities());
        flat.option->setPricingEngine(engine);
        const auto withProbabilities = flat.option->NPV();
        const auto probabilities = engineProbabilities(*flat.option);
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);

        // The probabilities come from two rollbacks of their own; the price's is the same
        // mesher, boundary, knock-out and payoff as before, so it is the same double.
        BOOST_TEST_MESSAGE("without " << plain << ", with " << withProbabilities);
        BOOST_CHECK_EQUAL(withProbabilities, plain);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdLocalVolGuard) { // NOLINT(misc-use-internal-linkage):
                                                 // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD engine refuses a smile without localVol and "
                           "accepts a strike-independent volatility either way");

        const auto flat = makeFlatCase();
        const auto smile = makeSmileCase(false);

        // Off the flag the operator would read the surface at the bonus level and price the
        // smile as a flat market, so the constructor fails loud instead.
        BOOST_CHECK_THROW(FdBlackScholesBonusClassicEngine(smile.process, mcTimeStepsPerYear,
                                                           fdTimeGrid, fdSpaceGrid, 0,
                                                           FdmSchemeDesc::TrBDF2(), false, false),
                          Error);
        BOOST_CHECK_NO_THROW(
            FdBlackScholesBonusClassicEngine(smile.process, mcTimeStepsPerYear, fdTimeGrid,
                                             fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(), false, true));

        // The type test reads the process's local volatility, so the smile is refused as an
        // external local volatility beside a flat Black volatility too, the case the Black
        // volatility's type alone would miss.
        const auto externalSmile = ext::make_shared<GeneralizedBlackScholesProcess>(
            smile.process->stateVariable(), smile.process->dividendYield(),
            smile.process->riskFreeRate(),
            RKE::Common::flatVol(flat.today, flat.marketData.volatility, Actual360()),
            Handle<LocalVolTermStructure>(ext::make_shared<LocalVolSurface>(
                smile.process->blackVolatility(), smile.process->riskFreeRate(),
                smile.process->dividendYield(), smile.process->stateVariable())));
        BOOST_CHECK_THROW(FdBlackScholesBonusClassicEngine(externalSmile, mcTimeStepsPerYear),
                          Error);

        // A constant local volatility passes, derived from the flat Black volatility or given
        // beside it, and so does the curve the process derives from a variance curve.
        BOOST_CHECK_NO_THROW(FdBlackScholesBonusClassicEngine(flat.process, mcTimeStepsPerYear));
        BOOST_CHECK_NO_THROW(FdBlackScholesBonusClassicEngine(
            RKE::Common::externalLocalVolProcess(flat.today), mcTimeStepsPerYear));
        BOOST_CHECK_NO_THROW(FdBlackScholesBonusClassicEngine(
            RKE::Common::varianceCurveProcess(flat.today), mcTimeStepsPerYear));

        const auto engine = makeFdEngine(smile.process, true, true);
        BOOST_CHECK(engine->usesLocalVolatility());
        BOOST_CHECK(!makeFdEngine(flat.process, true, false)->usesLocalVolatility());
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdLocalVolFlat) { // NOLINT(misc-use-internal-linkage):
                                                // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD engine prices the flat market the same with "
                           "and without localVol");

        const auto flat = makeFlatCase();

        for (const bool isBiased : {true, false}) {
            flat.option->setPricingEngine(makeFdEngine(flat.process, isBiased, false));
            const auto plain = flat.option->NPV();
            const auto plainProbabilities = engineProbabilities(*flat.option);

            // The process derives a LocalConstantVol from the flat BlackConstantVol, so both
            // operators carry the same sigma^2 on every node: the flat one as the forward
            // variance over the step divided by its length, the local one squared from the
            // constant. The difference is the rounding of that quotient: measured 2.3e-14
            // relative discrete and 4.9e-14 continuous for the price, 1.3e-14 and 3.7e-14 for
            // hit, 2.3e-14 and 4.9e-14 for bonus. The bound leaves twenty times the largest.
            flat.option->setPricingEngine(makeFdEngine(flat.process, isBiased, true));
            const auto local = flat.option->NPV();
            const auto localProbabilities = engineProbabilities(*flat.option);

            BOOST_TEST_MESSAGE(
                (isBiased ? "discrete " : "continuous ")
                << plain << ", local vol " << local << ", relative difference "
                << std::fabs(local - plain) / plain << "; hit "
                << relativeDifference(localProbabilities.hit, plainProbabilities.hit) << ", bonus "
                << relativeDifference(localProbabilities.bonus, plainProbabilities.bonus));
            BOOST_CHECK_CLOSE_FRACTION(plain, local, 1e-12);
            BOOST_CHECK_LE(relativeDifference(localProbabilities.hit, plainProbabilities.hit),
                           1e-12);
            BOOST_CHECK_LE(relativeDifference(localProbabilities.bonus, plainProbabilities.bonus),
                           1e-12);
        }
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdSmileValuation) { // NOLINT(misc-use-internal-linkage):
                                                  // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD smile valuation test");

        struct Lock {
            bool isBicubic;
            bool isBiased;
            Real npv;
            Probabilities probabilities;
        };
        // Regression locks: the engine's own output on this grid and scheme under local
        // volatility, not externally validated numbers. See
        // testBonusClassicOptionFdSmileVersusMc for that on the bicubic surface; the bilinear
        // values carry the kink defect testBonusClassicOptionFdSmileKink pins, so they lock
        // that defect along with the engine.
        const std::vector<Lock> locks = {
            {false, true, 118.8968756495899, {0.018663548537463093, 0.9249709380056883}},
            {false, false, 118.74078054420303, {0.020580682785420662, 0.93461616443619178}},
            {true, true, 119.01149265518772, {0.015993408894794697, 0.92822438247016947}},
            {true, false, 118.83036726419061, {0.019959179508870793, 0.92425995043463294}},
        };
        for (const auto& lock : locks) {
            const auto smile = makeSmileCase(lock.isBicubic);
            smile.option->setPricingEngine(makeFdEngine(smile.process, lock.isBiased, true));
            const auto npv = smile.option->NPV();
            const auto probabilities = engineProbabilities(*smile.option);

            BOOST_TEST_MESSAGE(surfaceName(lock.isBicubic)
                               << (lock.isBiased ? " discrete " : " continuous ")
                               << std::setprecision(17) << npv << ", hit " << probabilities.hit
                               << ", bonus " << probabilities.bonus);
            BOOST_CHECK_CLOSE_FRACTION(lock.npv, npv, 1e-8);
            BOOST_CHECK_CLOSE_FRACTION(lock.probabilities.hit, probabilities.hit, 1e-8);
            BOOST_CHECK_CLOSE_FRACTION(lock.probabilities.bonus, probabilities.bonus, 1e-8);
        }
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionFdSmileVersusMc) { // NOLINT(misc-use-internal-linkage):
                                                 // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD smile versus MC test");

        // The bicubic surface: smooth in strike, so LocalVolSurface's stencil is sound on every
        // node. The bilinear surface is not, see testBonusClassicOptionFdSmileKink.
        const auto smile = makeSmileCase(true);

        // Measured residuals at 50,000 paths: discrete 6.0e-5 relative for the price, 7.3e-5
        // absolute for hit and 1.0e-4 for bonus; continuous 1.4e-4, 2.8e-4 and 9.7e-5. The grid
        // is converged: at 4 times both grids the FD price moves by 6e-6 relative discrete and
        // 3e-6 continuous, the probabilities by 1.3e-5 and 6e-6 absolute at most. The rest is
        // the Monte Carlo side, the Euler step's bias plus sampling: at 200,000 paths the price
        // sits 2.7e-5 discrete and 1.4e-5 continuous above its 50,000-path value, and still
        // 8.1e-5 and 1.5e-4 above the refined FD price. Each bound leaves about three times its
        // residual, rounded up to one digit.
        const auto discrete = checkFdSmileVersusMc(smile, true, {2e-4, 3e-4, 3e-4});
        const auto continuous = checkFdSmileVersusMc(smile, false, {5e-4, 9e-4, 3e-4});

        // Continuous monitoring knocks out everything the grid knocks out and more, and a
        // knock-out only removes the bonus put, so the continuous price is lower.
        BOOST_TEST_MESSAGE("discrete " << discrete << ", continuous " << continuous);
        BOOST_CHECK_LT(continuous, discrete);
    }

    BOOST_AUTO_TEST_CASE(testBonusClassicOptionFdSmileKink) { // NOLINT(misc-use-internal-linkage):
                                                              // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption FD engine's local volatility on the bilinear "
                           "surface collapses at the barrier node");

        // The mechanism behind the engine's warning, pinned so a change in LocalVolSurface's
        // stencil or in the surface's strikes shows up here and not as a moved lock: the
        // bilinear surface is piecewise linear in strike, and its variance is convex there, so
        // the stencil straddling a strike node sees a huge second derivative and returns a
        // local variance near zero. 70 is both the barrier and a strike node, and in discrete
        // monitoring a grid node. The bicubic surface is smooth in strike and unaffected.
        const auto bilinear = makeSmileCase(false);
        const auto bicubic = makeSmileCase(true);
        const auto barrier = bilinear.optionData.barrier;
        const auto bilinearVol = bilinear.process->localVolatility();
        const auto bicubicVol = bicubic.process->localVolatility();

        // Measured at t = 0.2: 0.036 against 0.263 on the node, a ratio of 0.14, and 0.257 and
        // 0.272 against 0.263 one percent on either side of it, 2.3% below and 3.4% above. The
        // bounds leave about twice the ratio and one and a half times the larger deviation,
        // rounded up to one digit: the gap between 0.14 and 1 is what the test pins, not the
        // digits of either side.
        constexpr Time t = 0.2;
        const auto onNode =
            bilinearVol->localVol(t, barrier, true) / bicubicVol->localVol(t, barrier, true);
        BOOST_TEST_MESSAGE("bilinear over bicubic local volatility at the barrier " << onNode);
        BOOST_CHECK_LT(onNode, 0.25);
        for (const Real level : {barrier * 0.99, barrier * 1.01}) {
            const auto ratio =
                bilinearVol->localVol(t, level, true) / bicubicVol->localVol(t, level, true);
            BOOST_TEST_MESSAGE("bilinear over bicubic local volatility at " << level << ' '
                                                                            << ratio);
            BOOST_CHECK_CLOSE_FRACTION(ratio, 1.0, 0.05);
        }

        // What it does to the price: on the test grid the discrete bilinear FD price sits
        // 8.5e-4 relative below MCBonusClassicEngine under LocalVolStepSingleVariate at
        // 50,000 paths and moves by 1.2e-4 and 1.6e-4 at 2 and 4 times both grids, while the
        // bicubic one sits 6.0e-5 off and moves by 6e-6. Not asserted: it depends on which
        // nodes land in a stencil window, the barrier node always, and others by chance.
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialValuation) { // NOLINT(misc-use-internal-linkage):
                                                   // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial valuation test");

        const auto flat = makeFlatCase();

        const auto engine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            flat.process, treeTimeSteps);
        flat.option->setPricingEngine(engine);
        const auto npv = flat.option->NPV();

        // Boyle-Lau: the first floor(i^2 sigma^2 T / ln^2(S / H)) above 400, at i = 55.
        BOOST_CHECK_EQUAL(engine->timeGrid().size() - 1, Size(404));

        // Regression lock: the engine's own output on this lattice, not an externally validated
        // price. See testBonusClassicOptionBinomialReplication for that.
        BOOST_CHECK_CLOSE_FRACTION(119.50487799078239, npv, 1e-8);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialReplication) { // NOLINT(misc-use-internal-linkage):
                                                     // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial replication test");

        const auto flat = makeFlatCase();

        flat.option->setPricingEngine(
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(flat.process,
                                                                            treeTimeSteps));
        const auto npv = flat.option->NPV();

        // The tree monitors on every step, and Boyle-Lau puts a layer of nodes at 69.9929, just
        // below the barrier, so the put takes the barrier itself and no Broadie-Glasserman-Kou
        // shift applies.
        const auto replication = replicationPrice(flat.process, flat.optionData, flat.exerciseDate,
                                                  flat.optionData.barrier);

        // Measured residual 3.7e-5 relative at 404 steps. It is discretisation error, not
        // noise, and it does not fall steadily with the steps: 8.5e-5 at 104 steps after
        // Boyle-Lau, 1.2e-4 at 224, 2.2e-5 at 813 and 6.5e-6 at 1,616, as the layer the floor
        // leaves below the barrier moves between 1e-6 and 5e-4 of it. The bound leaves 2.7
        // times the 404-step residual.
        BOOST_TEST_MESSAGE("binomial " << npv << ", relative residual "
                                       << std::fabs(npv - replication) / replication);
        BOOST_CHECK_CLOSE_FRACTION(replication, npv, 1e-4);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialBoyleLau) { // NOLINT(misc-use-internal-linkage):
                                                  // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial Boyle-Lau test");

        const auto flat = makeFlatCase();

        const auto replication = replicationPrice(flat.process, flat.optionData, flat.exerciseDate,
                                                  flat.optionData.barrier);

        flat.option->setPricingEngine(
            ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(flat.process,
                                                                            treeTimeSteps));
        const auto withBoyleLau = std::fabs(flat.option->NPV() - replication) / replication;

        // maxTimeSteps = timeSteps disables Boyle-Lau.
        const auto plainEngine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            flat.process, treeTimeSteps, treeTimeSteps);
        flat.option->setPricingEngine(plainEngine);
        const auto withoutBoyleLau = std::fabs(flat.option->NPV() - replication) / replication;
        BOOST_CHECK_EQUAL(plainEngine->timeGrid().size() - 1, treeTimeSteps);

        // Without Boyle-Lau the first knocked-out layer at 400 steps sits at 69.87, an effective
        // barrier 0.19% low; measured residuals 3.7e-5 relative with Boyle-Lau and 1.7e-4
        // without.
        BOOST_TEST_MESSAGE("relative residual with Boyle-Lau " << withBoyleLau << ", without "
                                                               << withoutBoyleLau);
        BOOST_CHECK_LT(withBoyleLau, withoutBoyleLau);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialProbabilities) { // NOLINT(misc-use-internal-linkage):
                                                       // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial probabilities test");

        const auto flat = makeFlatCase();

        // The lattice of testBonusClassicOptionBinomialValuation's lock, with the two
        // indicator legs rolled back beside the certificate.
        const auto engine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            flat.process, treeTimeSteps, 0, true);
        flat.option->setPricingEngine(engine);
        static_cast<void>(flat.option->NPV());
        const auto probabilities = engineProbabilities(*flat.option);

        // Boyle-Lau: the first floor(i^2 sigma^2 T / ln^2(S / H)) above 400, at i = 55.
        BOOST_CHECK_EQUAL(engine->timeGrid().size() - 1, Size(404));

        // Reaching a node at or below the barrier and paying the bonus are disjoint, and a
        // lattice path that never does and ends at or above the bonus level is in neither.
        BOOST_CHECK_GT(probabilities.hit, 0.0);
        BOOST_CHECK_GT(probabilities.bonus, 0.0);
        BOOST_CHECK_LT(probabilities.hit + probabilities.bonus, 1.0);

        // Regression lock, as in testBonusClassicOptionBinomialValuation: the engine's own
        // output on this lattice, not externally validated probabilities. The independent check
        // follows below.
        BOOST_CHECK_CLOSE_FRACTION(0.0087152329237066232, probabilities.hit, 1e-8);
        BOOST_CHECK_CLOSE_FRACTION(0.93348793687522391, probabilities.bonus, 1e-8);

        // The tree monitors on every step, and Boyle-Lau puts a layer of nodes at 69.9929, just
        // below the barrier, so the reference takes the barrier itself and no
        // Broadie-Glasserman-Kou shift applies.
        const auto reference = referenceProbabilities(flat.process, flat.optionData, flat.today,
                                                      flat.exerciseDate, flat.optionData.barrier);

        // Spot 100 against barrier 70 over 5M leaves a small hit probability, so the residuals
        // are absolute, not relative.
        // Measured residuals 1.18e-4 absolute for hit and 5.49e-3 absolute for bonus at 404
        // steps. Both are lattice discretisation error, not noise: the layer the Boyle-Lau
        // floor leaves below the barrier, and for the bonus indicator its jump at B, which
        // falls between lattice nodes, so the nodes next to B carry a whole or no unit instead
        // of their cell's share. That jump is the whole bonus residual: with no reachable
        // barrier the same 404-step lattice puts P(S_T < B) 5.37e-3 above
        // AnalyticEuropeanEngine's cash-or-nothing put, and the residual oscillates with the
        // step count as the nodes move past B, 6.3e-4 at 813 steps, 2.4e-3 at 1616, -4.7e-4 at
        // 3210 and 8.7e-5 at 6409, while the hit residual falls monotonically to 6.1e-7. Each
        // bound leaves about three times its residual, rounded up to one digit.
        BOOST_TEST_MESSAGE("binomial hit " << probabilities.hit << ", reference " << reference.hit
                                           << ", absolute residual "
                                           << std::fabs(probabilities.hit - reference.hit));
        BOOST_TEST_MESSAGE("binomial bonus " << probabilities.bonus << ", reference "
                                             << reference.bonus << ", absolute residual "
                                             << std::fabs(probabilities.bonus - reference.bonus));
        BOOST_CHECK_SMALL(probabilities.hit - reference.hit, 4e-4);
        BOOST_CHECK_SMALL(probabilities.bonus - reference.bonus, 2e-2);
    }

    BOOST_AUTO_TEST_CASE(
        testBonusClassicOptionBinomialProbabilitiesOptIn) { // NOLINT(misc-use-internal-linkage):
                                                            // the struct is the macro's
        BOOST_TEST_MESSAGE("BonusClassicOption binomial engine reports the probabilities only "
                           "with calculateProbabilities on");

        const auto flat = makeFlatCase();

        // The default engine prices as before and sets neither key: Instrument::result()
        // throws on a key additionalResults does not hold.
        const auto plainEngine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            flat.process, treeTimeSteps);
        BOOST_CHECK(!plainEngine->calculatesProbabilities());
        flat.option->setPricingEngine(plainEngine);
        static_cast<void>(flat.option->NPV());
        BOOST_CHECK_THROW(static_cast<void>(flat.option->result<Real>("barrierHitProbability")),
                          Error);
        BOOST_CHECK_THROW(static_cast<void>(flat.option->result<Real>("bonusProbability")), Error);

        const auto engine = ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
            flat.process, treeTimeSteps, 0, true);
        BOOST_CHECK(engine->calculatesProbabilities());
        flat.option->setPricingEngine(engine);
        static_cast<void>(flat.option->NPV());
        BOOST_CHECK_NO_THROW(static_cast<void>(engineProbabilities(*flat.option)));
    }

    BOOST_AUTO_TEST_SUITE_END()

    BOOST_AUTO_TEST_SUITE_END()
}
