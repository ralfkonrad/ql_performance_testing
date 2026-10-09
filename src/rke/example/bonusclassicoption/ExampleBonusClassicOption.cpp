// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// Prices one BonusClassicOption under every monitoring mode, market, engine and path
// generation rke_common sets up, and prints each price next to its analytic replication: the
// asset leg plus a down-and-out put struck at the bonus level, priced by
// QuantLib::AnalyticBarrierEngine on the flat market. A second table prints, for the same
// rows, the barrier-hit and bonus probabilities the engine reports beside the price, next to
// their analytic reference: cash-or-nothing barrier options priced by
// QuantLib::AnalyticBinaryBarrierEngine over the discount factor from maturity. The example
// counterpart of rke_profile_bonusclassicoption, which prices the same setups for perf and
// valgrind.
//
//     rke_example_bonusclassicoption
//
// The rows are grouped by monitoring, then market. The flat rows price at a production run's
// 2^16 paths; the smile rows at 2^10, since the example runs as a smoke test on every pull
// request and the bicubic surface under SingleVariate is slow. A smile row prints no
// reference: there is none under local volatility. The smile finite-difference rows are
// deterministic beside Monte-Carlo rows at 2^10 paths; the test suite checks the bicubic ones
// against 50,000 paths. The bilinear finite-difference rows carry the defect the engine's
// header warns about: 70 is a strike node of the surface as well as the barrier, and
// QuantLib::LocalVolSurface's stencil collapses the local volatility on the grid node there.

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/pricingengines/barrier/analyticbinarybarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace RKE::Common;
using namespace QuantLib;

namespace RKE::Example {
    namespace {
        // Enough paths to show the smile markets priced. The bicubic surface rebuilds a spline on
        // every lookup, and SingleVariate asks it for every step again: at productionSamples the
        // four bicubic rows alone would take minutes, at 2^12 still 11 s.
        constexpr Size smileSamples = Size{1} << 10U;

        // An engine as the table names it: the path generation is read by Monte Carlo only.
        struct Pricer {
            Engine engine;
            PathGeneration pathGeneration;
        };

        // The probability that the barrier is touched and the probability that it never is and
        // S_T < B, i.e. that the bonus is paid: the two results every engine reports beside the
        // price once asked to, and what the analytic reference gives for them.
        struct Probabilities {
            Real hit;
            Real bonus;
        };

        // What an engine reports for a row, and what the references give for it.
        struct Valuation {
            Real npv;
            Probabilities probabilities;
        };

        struct Row {
            bool isDiscrete;
            Market market;
            Pricer pricer;
            Size paths;          // Null<Size>() off the Monte-Carlo engine.
            Valuation valuation; // The engine's.
            Valuation reference; // Null<Real>() throughout on the smiles.
        };

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
                               const OptionData& data,
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

        // Both references on one barrier.
        Valuation referenceValuation(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                                     const OptionData& data,
                                     Date today,
                                     Date exerciseDate,
                                     Real barrier) {
            return {
                replicationPrice(process, data, exerciseDate, barrier),
                referenceProbabilities(process, data, today, exerciseDate, barrier),
            };
        }

        // The price and, after it has run, the two probabilities the engine reports; result()
        // throws if a key is missing, so the setup has to have asked for them.
        Valuation value(const BonusClassicOptionSetup& setup) {
            const auto npv = reprice(*setup.option);
            return {
                npv,
                {
                    setup.option->result<Real>("barrierHitProbability"),
                    setup.option->result<Real>("bonusProbability"),
                },
            };
        }

        Valuation price(bool isDiscrete, Market market, Pricer pricer, Size samples) {
            const auto setup = makeBonusClassicOptionSetup(isDiscrete, pricer.pathGeneration,
                                                           market, samples, pricer.engine, true);
            return value(setup);
        }

        std::string name(Pricer pricer) {
            switch (pricer.engine) {
                case Engine::MonteCarlo:
                    return pricer.pathGeneration == PathGeneration::CachedStep ?
                               "Monte Carlo, step cache" :
                               "Monte Carlo, SingleVariate";
                case Engine::FiniteDifference:
                    return "finite differences";
                case Engine::Binomial:
                    return "binomial CRR, Boyle-Lau";
            }
            QL_FAIL("unknown engine");
        }

        std::string name(bool isDiscrete) {
            return isDiscrete ? "discrete" : "continuous";
        }

        std::string name(Market market) {
            switch (market) {
                case Market::Flat:
                    return "flat";
                case Market::SmileBilinear:
                    return "smile, bilinear";
                case Market::SmileBicubic:
                    return "smile, bicubic";
            }
            QL_FAIL("unknown market");
        }

        void printHeader(const OptionData& option, const MarketData& market, Date exerciseDate) {
            std::cout << "BonusClassicOption as of " << evaluationDate() << ", maturity "
                      << exerciseDate << " (" << option.ttm << ")\n"
                      << "  barrier " << option.barrier << ", bonus level " << option.bonusLevel
                      << ", spot " << market.spot << '\n'
                      << "  flat market: r " << market.riskfreeRate * 100 << "%, q "
                      << market.dividendYield * 100 << "%, continuously compounded, vol "
                      << market.volatility * 100 << "%, Actual360, NullCalendar\n"
                      << "  smile markets: zero curves and a Black variance surface around the "
                         "same levels, RKE::Common::SmileMarketData\n\n";
        }

        // The label columns both tables share, 57 characters.
        constexpr int monitoringWidth = 12;
        constexpr int marketWidth = 17;
        constexpr int engineWidth = 28;

        // One table over the rows: the label columns, then printColumns for the header and
        // printCells for each row, both starting right-aligned. Grouped by monitoring, then
        // market: a label repeats only where its group changes.
        template <class PrintColumns, class PrintCells>
        void
        printTable(const std::vector<Row>& rows, PrintColumns printColumns, PrintCells printCells) {
            std::cout << std::left << std::setw(monitoringWidth) << "monitoring"
                      << std::setw(marketWidth) << "market" << std::setw(engineWidth) << "engine"
                      << std::right;
            printColumns();
            std::cout << '\n';

            const Row* previous = nullptr;
            for (const auto& row : rows) {
                const auto newMonitoring =
                    previous == nullptr || previous->isDiscrete != row.isDiscrete;
                const auto newMarket = newMonitoring || previous->market != row.market;
                previous = &row;

                std::cout << std::left << std::setw(monitoringWidth)
                          << (newMonitoring ? name(row.isDiscrete) : "") << std::setw(marketWidth)
                          << (newMarket ? name(row.market) : "") << std::setw(engineWidth)
                          << name(row.pricer) << std::right;
                printCells(row);
                std::cout << std::defaultfloat << '\n';
            }
        }

        void printPrices(const std::vector<Row>& rows) {
            constexpr int pathsWidth = 7;
            constexpr int priceWidth = 10;
            constexpr int differenceWidth = 10;

            printTable(
                rows,
                [&] {
                    std::cout << std::setw(pathsWidth) << "paths" << std::setw(priceWidth) << "NPV"
                              << std::setw(priceWidth) << "reference" << std::setw(differenceWidth)
                              << "rel.diff";
                },
                [&](const Row& row) {
                    std::cout << std::setw(pathsWidth);
                    if (row.paths == Null<Size>()) {
                        std::cout << "-";
                    } else {
                        std::cout << row.paths;
                    }
                    const auto npv = row.valuation.npv;
                    const auto reference = row.reference.npv;
                    std::cout << std::fixed << std::setprecision(3) << std::setw(priceWidth) << npv;
                    if (reference == Null<Real>()) {
                        std::cout << std::setw(priceWidth) << "-" << std::setw(differenceWidth)
                                  << "-";
                    } else {
                        std::cout << std::setw(priceWidth) << reference << std::scientific
                                  << std::setprecision(1) << std::setw(differenceWidth)
                                  << std::fabs(npv - reference) / reference;
                    }
                });
        }

        void printProbabilities(const std::vector<Row>& rows) {
            constexpr int probabilityWidth = 10;

            const auto printProbability = [&](Real probability) {
                if (probability == Null<Real>()) {
                    std::cout << std::setw(probabilityWidth) << "-";
                } else {
                    std::cout << std::fixed << std::setprecision(4) << std::setw(probabilityWidth)
                              << probability;
                }
            };

            printTable(
                rows,
                [&] {
                    std::cout << std::setw(probabilityWidth) << "P(hit)"
                              << std::setw(probabilityWidth) << "reference"
                              << std::setw(probabilityWidth) << "P(bonus)"
                              << std::setw(probabilityWidth) << "reference";
                },
                [&](const Row& row) {
                    printProbability(row.valuation.probabilities.hit);
                    printProbability(row.reference.probabilities.hit);
                    printProbability(row.valuation.probabilities.bonus);
                    printProbability(row.reference.probabilities.bonus);
                });
        }

        // AnalyticBarrierEngine and AnalyticBinaryBarrierEngine assume continuous monitoring.
        // The continuous rows and the tree, whose Boyle-Lau layer sits at the barrier, compare
        // against the barrier itself; the rows monitored every dt against the
        // Broadie-Glasserman-Kou barrier.
        struct References {
            Valuation continuous;
            Valuation discrete;
        };

        Valuation
        reference(const References& references, bool isDiscrete, Market market, Pricer pricer) {
            if (market != Market::Flat) {
                return {Null<Real>(), {Null<Real>(), Null<Real>()}};
            }
            if (isDiscrete && pricer.engine != Engine::Binomial) {
                return references.discrete;
            }
            return references.continuous;
        }

        // The setup rejects the tree off the flat market and under continuous monitoring.
        bool admits(bool isDiscrete, Market market, Pricer pricer) {
            return pricer.engine != Engine::Binomial || (market == Market::Flat && isDiscrete);
        }

        // Every permutation the setup admits, in the table's order. The flat finite-difference
        // row under discrete monitoring is priced already: its grid gave the step the discrete
        // reference corrects for.
        std::vector<Row> priceRows(const References& references,
                                   const Valuation& fdDiscreteValuation) {
            const auto pricers = std::vector<Pricer>{
                {Engine::MonteCarlo, PathGeneration::CachedStep},
                {Engine::MonteCarlo, PathGeneration::Uncached},
                {Engine::FiniteDifference, PathGeneration::CachedStep},
                {Engine::Binomial, PathGeneration::CachedStep},
            };
            auto rows = std::vector<Row>();
            for (const bool isDiscrete : {true, false}) {
                for (const auto market :
                     {Market::Flat, Market::SmileBilinear, Market::SmileBicubic}) {
                    for (const auto& pricer : pricers) {
                        if (!admits(isDiscrete, market, pricer)) {
                            continue;
                        }
                        const auto isMonteCarlo = pricer.engine == Engine::MonteCarlo;
                        const auto isFdDiscrete = pricer.engine == Engine::FiniteDifference &&
                                                  isDiscrete && market == Market::Flat;
                        const auto samples =
                            market == Market::Flat ? productionSamples : smileSamples;
                        rows.push_back({
                            isDiscrete,
                            market,
                            pricer,
                            isMonteCarlo ? samples : Null<Size>(),
                            isFdDiscrete ? fdDiscreteValuation :
                                           price(isDiscrete, market, pricer, samples),
                            reference(references, isDiscrete, market, pricer),
                        });
                    }
                }
            }
            return rows;
        }

        void run() {
            const auto option = OptionData();
            const auto market = MarketData();

            // The finite-difference engine under discrete monitoring goes first: its grid is
            // the one every discrete flat row monitors on, Monte Carlo's dates merged into the
            // rollback, so its step is the one the continuity correction takes.
            const auto fdDiscrete =
                makeBonusClassicOptionSetup(true, PathGeneration::CachedStep, Market::Flat,
                                            productionSamples, Engine::FiniteDifference, true);
            const auto fdDiscreteValuation = value(fdDiscrete);
            const auto today = evaluationDate();
            const auto exerciseDate = fdDiscrete.option->exercise()->lastDate();
            const auto dt = fdDiscrete.process->time(exerciseDate) /
                            static_cast<Real>(monitoringSteps(fdDiscrete));

            printHeader(option, market, exerciseDate);

            const auto shiftedBarrier = bgkShiftedBarrier(option.barrier, market.volatility, dt);
            const auto references = References{
                referenceValuation(fdDiscrete.process, option, today, exerciseDate, option.barrier),
                referenceValuation(fdDiscrete.process, option, today, exerciseDate, shiftedBarrier),
            };

            std::cout << std::fixed << std::setprecision(3)
                      << "Replication, asset leg plus a down-and-out put struck at the bonus "
                         "level, AnalyticBarrierEngine:\n"
                      << "  continuous monitoring, barrier " << option.barrier << ": "
                      << references.continuous.npv << '\n'
                      << "  discrete monitoring on " << monitoringSteps(fdDiscrete)
                      << " steps, Broadie-Glasserman-Kou barrier " << shiftedBarrier << ": "
                      << references.discrete.npv << "\n\n"
                      << std::defaultfloat;

            const auto rows = priceRows(references, fdDiscreteValuation);
            printPrices(rows);

            std::cout << "\nProbabilities under the process's measure: hit, the barrier touched "
                         "on the engine's\nmonitoring; bonus, never touched and S_T below the "
                         "bonus level. Reference: cash-or-nothing\nbarrier options on the "
                         "barriers above, AnalyticBinaryBarrierEngine, over the discount factor\n"
                         "from maturity.\n\n";
            printProbabilities(rows);
        }
    }
}

int main() {
    try {
        RKE::Example::run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
