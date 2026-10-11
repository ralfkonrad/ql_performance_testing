// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// Prices one BonusClassicOption under every monitoring mode, market, engine and path
// generation rke_common sets up, and prints each price next to its analytic reference on the
// flat market, AnalyticBonusClassicEngine: the asset leg plus a down-and-out put struck at the
// bonus level, priced by QuantLib::AnalyticBarrierEngine. A second table prints, for the same
// rows, the barrier-hit and bonus probabilities the engine reports beside the price, next to
// the ones AnalyticBonusClassicEngine reports from cash-or-nothing barrier options. The
// example counterpart of rke_profile_bonusclassicoption, which prices the same setups for perf
// and valgrind.
//
//     rke_example_bonusclassicoption [--barrier <level>] [--bonus-level <level>]
//                                    [--volatility <decimal>] [--riskfree-rate <decimal>]
//                                    [--dividend-yield <decimal>]
//
// An option left out keeps rke_common's OptionData or FlatMarketData value, so the CTest smoke
// test prices the defaults. The levels reach every row. The volatility and the two rates,
// zero rates continuously compounded on Actual360, set the flat market, and
// RKE::Common::smileMarketDataAround moves the smiles with it: their curves shift in parallel,
// their smile scales with the volatility. A bonus level at or below the barrier is refused,
// since the bonus then never pays; a barrier at or above the spot every engine refuses as
// touched.
//
// A row whose engine throws on the inputs prints "-", its message follows the tables, and the
// example exits 1, so the smoke test still fails on one. At the default levels and rates every
// row prices for a volatility from 0.065 to 0.32, measured in steps of 0.005 to 0.01. Below,
// the finite-difference grids under discrete monitoring no longer reach the barrier; above,
// QuantLib::LocalVolSurface finds a negative local variance on the bilinear smile near its 25
// node, and by 0.48 on the bicubic one near its 10 node too. Monte Carlo generates whole paths
// before the barrier applies, so its rows are refused there as well.
//
// The rows are grouped by monitoring, then market. The flat rows price at a production run's
// 2^16 paths; the smile rows at 2^10, since the example runs as a smoke test on every pull
// request and the bicubic surface under SingleVariate is slow. A smile row prints no
// reference: there is none under local volatility. The smile finite-difference rows are
// deterministic beside Monte-Carlo rows at 2^10 paths; the test suite checks the bicubic ones
// against 50,000 paths. At the default barrier the bilinear finite-difference rows carry the
// defect the engine's header warns about: 70 is a node of SmileMarketData::strikes as well as
// the barrier, and QuantLib::LocalVolSurface's stencil collapses the local volatility on the
// grid node there. A barrier on another strike node does the same; one between them keeps the
// defect off the barrier.

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/AnalyticBonusClassicEngine.hpp>
#include <ql/errors.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <boost/algorithm/cxx11/none_of.hpp>
#include <CLI/CLI.hpp>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace RKE::Common;
using namespace QuantLib;
using RKE::QL::Ext::AnalyticBonusClassicEngine;
using RKE::QL::Ext::BarrierMonitoring;
using RKE::QL::Ext::BonusClassicOption;

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

        // Null<Real>() throughout: the reference of a smile row, or the valuation of a row the
        // engine refused.
        Valuation noValuation() {
            return {Null<Real>(), {Null<Real>(), Null<Real>()}};
        }

        // An engine's valuation, or none and the engine's message where it throws: a command line
        // can leave a grid or a surface behind that an engine refuses.
        struct Outcome {
            Valuation valuation;
            std::string refusal; // Empty where the engine priced.
        };

        struct Row {
            BarrierMonitoring monitoring;
            Market market;
            Pricer pricer;
            Size paths;          // Null<Size>() off the Monte-Carlo engine.
            Valuation valuation; // The engine's, noValuation() where it refused.
            Valuation reference; // noValuation() on the smiles.
            std::string refusal; // The engine's message, empty where it priced.
        };

        bool isRefused(const Row& row) {
            return !row.refusal.empty();
        }

        // Broadie, Glasserman and Kou (1997), "A continuity correction for discrete barrier
        // options", Mathematical Finance 7(4), 325-349: a down barrier monitored every dt prices
        // like a continuously monitored one at H * exp(-beta * sigma * sqrt(dt)), with
        // beta = -zeta(1/2) / sqrt(2 * pi).
        Real bgkShiftedBarrier(Real barrier, Volatility volatility, Time dt) {
            constexpr Real beta = 0.5826;
            return barrier * std::exp(-beta * volatility * std::sqrt(dt));
        }

        // AnalyticBonusClassicEngine on the barrier given here, monitored continuously: the price
        // and both probabilities a flat row is compared with.
        Valuation analyticValuation(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                                    const OptionData& data,
                                    Date exerciseDate,
                                    Real barrier) {
            auto option = BonusClassicOption(barrier, data.bonusLevel, exerciseDate);
            option.setPricingEngine(ext::make_shared<AnalyticBonusClassicEngine>(process, true));
            const auto npv = option.NPV();
            return {
                npv,
                {
                    option.result<Real>("barrierHitProbability"),
                    option.result<Real>("bonusProbability"),
                },
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

        Outcome valueOrRefusal(const BonusClassicOptionSetup& setup) {
            try {
                return {value(setup), {}};
            } catch (const std::exception& e) {
                return {noValuation(), e.what()};
            }
        }

        Outcome price(const OptionData& option,
                      const FlatMarketData& flatMarket,
                      const SmileMarketData& smileMarket,
                      BarrierMonitoring monitoring,
                      Market market,
                      Pricer pricer,
                      Size samples) {
            return valueOrRefusal(makeBonusClassicOptionSetup(
                option, flatMarket, smileMarket, monitoring, pricer.pathGeneration, market, samples,
                pricer.engine, ProbabilityCalculation::On));
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
                case Engine::Analytic:
                    return "analytic";
            }
            QL_FAIL("unknown engine");
        }

        std::string name(BarrierMonitoring monitoring) {
            switch (monitoring) {
                case BarrierMonitoring::Discrete:
                    return "discrete";
                case BarrierMonitoring::Continuous:
                    return "continuous";
            }
            QL_FAIL("unknown barrier monitoring");
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

        void
        printHeader(const OptionData& option, const FlatMarketData& flatMarket, Date exerciseDate) {
            std::cout << "BonusClassicOption as of " << evaluationDate() << ", maturity "
                      << exerciseDate << " (" << option.ttm << ")\n"
                      << "  barrier " << option.barrier << ", bonus level " << option.bonusLevel
                      << ", spot " << flatMarket.spot << '\n'
                      << "  flat market: r " << flatMarket.riskfreeRate * 100 << "%, q "
                      << flatMarket.dividendYield * 100 << "%, continuously compounded, vol "
                      << flatMarket.volatility * 100 << "%, Actual360, NullCalendar\n"
                      << "  smile markets: zero curves and a Black variance surface around the "
                         "same levels, RKE::Common::smileMarketDataAround\n\n";
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
                    previous == nullptr || previous->monitoring != row.monitoring;
                const auto newMarket = newMonitoring || previous->market != row.market;
                previous = &row;

                std::cout << std::left << std::setw(monitoringWidth)
                          << (newMonitoring ? name(row.monitoring) : "") << std::setw(marketWidth)
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

            const auto printPrice = [&](Real price) {
                if (price == Null<Real>()) {
                    std::cout << std::setw(priceWidth) << "-";
                } else {
                    std::cout << std::fixed << std::setprecision(3) << std::setw(priceWidth)
                              << price;
                }
            };

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
                    printPrice(npv);
                    printPrice(reference);
                    if (npv == Null<Real>() || reference == Null<Real>()) {
                        std::cout << std::setw(differenceWidth) << "-";
                    } else {
                        std::cout << std::scientific << std::setprecision(1)
                                  << std::setw(differenceWidth)
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

        // The rows an engine refused, each with its message; nothing if every row priced.
        void printRefusals(const std::vector<Row>& rows) {
            if (boost::algorithm::none_of(rows, isRefused)) {
                return;
            }
            std::cout << "\nRefused for the inputs above:\n";
            for (const auto& row : rows) {
                if (isRefused(row)) {
                    std::cout << "  " << name(row.monitoring) << ", " << name(row.market) << ", "
                              << name(row.pricer) << ": " << row.refusal << '\n';
                }
            }
        }

        // AnalyticBonusClassicEngine monitors continuously. The continuous rows and the tree, whose
        // Boyle-Lau layer sits at the barrier, compare against the barrier itself; the rows
        // monitored every dt against the Broadie-Glasserman-Kou barrier.
        struct References {
            Valuation continuous;
            Valuation discrete;
        };

        Valuation reference(const References& references,
                            BarrierMonitoring monitoring,
                            Market market,
                            Pricer pricer) {
            if (market != Market::Flat) {
                return noValuation();
            }
            if (monitoring == BarrierMonitoring::Discrete && pricer.engine != Engine::Binomial) {
                return references.discrete;
            }
            return references.continuous;
        }

        // The setup rejects the tree and the closed form off the flat market, the tree under
        // continuous monitoring and the closed form under discrete.
        bool admits(BarrierMonitoring monitoring, Market market, Pricer pricer) {
            if (pricer.engine == Engine::Binomial) {
                return market == Market::Flat && monitoring == BarrierMonitoring::Discrete;
            }
            if (pricer.engine == Engine::Analytic) {
                return market == Market::Flat && monitoring == BarrierMonitoring::Continuous;
            }
            return true;
        }

        // Every permutation the setup admits, in the table's order. The flat finite-difference
        // row under discrete monitoring is priced already: its grid gave the step the discrete
        // reference corrects for.
        std::vector<Row> priceRows(const OptionData& option,
                                   const FlatMarketData& flatMarket,
                                   const SmileMarketData& smileMarket,
                                   const References& references,
                                   const Outcome& fdDiscreteOutcome) {
            const auto pricers = std::vector<Pricer>{
                {Engine::MonteCarlo, PathGeneration::CachedStep},
                {Engine::MonteCarlo, PathGeneration::Uncached},
                {Engine::FiniteDifference, PathGeneration::CachedStep},
                {Engine::Binomial, PathGeneration::CachedStep},
                {Engine::Analytic, PathGeneration::CachedStep},
            };
            auto rows = std::vector<Row>();
            for (const auto monitoring :
                 {BarrierMonitoring::Discrete, BarrierMonitoring::Continuous}) {
                for (const auto market :
                     {Market::Flat, Market::SmileBilinear, Market::SmileBicubic}) {
                    for (const auto& pricer : pricers) {
                        if (!admits(monitoring, market, pricer)) {
                            continue;
                        }
                        const auto isMonteCarlo = pricer.engine == Engine::MonteCarlo;
                        const auto isFdDiscrete = pricer.engine == Engine::FiniteDifference &&
                                                  monitoring == BarrierMonitoring::Discrete &&
                                                  market == Market::Flat;
                        const auto samples =
                            market == Market::Flat ? productionSamples : smileSamples;
                        const auto outcome = isFdDiscrete ?
                                                 fdDiscreteOutcome :
                                                 price(option, flatMarket, smileMarket, monitoring,
                                                       market, pricer, samples);
                        rows.push_back({
                            monitoring,
                            market,
                            pricer,
                            isMonteCarlo ? samples : Null<Size>(),
                            outcome.valuation,
                            reference(references, monitoring, market, pricer),
                            outcome.refusal,
                        });
                    }
                }
            }
            return rows;
        }

        // The command line: rke_common's defaults until an option sets a value.
        struct Arguments {
            OptionData option;
            FlatMarketData flatMarket;
        };

        void addOptions(CLI::App& app, Arguments& arguments) {
            app.add_option("--barrier", arguments.option.barrier,
                           "Knock-out barrier, below the spot")
                ->capture_default_str();
            app.add_option("--bonus-level", arguments.option.bonusLevel,
                           "Bonus level, above the barrier")
                ->capture_default_str();
            // BlackConstantVol stores any value, AnalyticBarrierEngine divides by its standard
            // deviation, and smileMarketDataAround scales the smile by it.
            app.add_option("--volatility", arguments.flatMarket.volatility,
                           "Black volatility of the flat market, a decimal; the smile scales "
                           "with it")
                ->capture_default_str()
                ->check(CLI::PositiveNumber);
            app.add_option("--riskfree-rate", arguments.flatMarket.riskfreeRate,
                           "Risk-free zero rate of the flat market, a decimal, continuously "
                           "compounded on Actual360; the smile's curve shifts with it")
                ->capture_default_str();
            app.add_option("--dividend-yield", arguments.flatMarket.dividendYield,
                           "Dividend yield of the flat market, a decimal, continuously "
                           "compounded on Actual360; the smile's curve shifts with it")
                ->capture_default_str();

            // BonusClassicOption requires both levels positive only. With the bonus level at or
            // below the barrier the bonus never pays and BonusClassicPayoff collapses to S_T. The
            // final callback runs after both values are parsed.
            app.final_callback([&arguments] {
                const auto& option = arguments.option;
                QL_REQUIRE(option.bonusLevel > option.barrier,
                           "--bonus-level " << option.bonusLevel << " is not above --barrier "
                                            << option.barrier << ", so the bonus never pays");
            });
        }

        // Whether every row priced. A refused row fails the run, so the smoke test on the
        // defaults still catches an engine that throws.
        bool run(const Arguments& arguments) {
            const auto& option = arguments.option;
            const auto& flatMarket = arguments.flatMarket;
            const auto smileMarket = smileMarketDataAround(flatMarket);

            // The finite-difference engine under discrete monitoring goes first: its grid is
            // the one every discrete flat row monitors on, Monte Carlo's dates merged into the
            // rollback, so its step is the one the continuity correction takes. Where the
            // engine refuses, there is no step and no discrete reference.
            const auto fdDiscrete = makeBonusClassicOptionSetup(
                option, flatMarket, smileMarket, BarrierMonitoring::Discrete,
                PathGeneration::CachedStep, Market::Flat, productionSamples,
                Engine::FiniteDifference, ProbabilityCalculation::On);
            const auto fdDiscreteOutcome = valueOrRefusal(fdDiscrete);
            const auto exerciseDate = fdDiscrete.option->exercise()->lastDate();
            const auto steps =
                fdDiscreteOutcome.refusal.empty() ? monitoringSteps(fdDiscrete) : Null<Size>();

            printHeader(option, flatMarket, exerciseDate);

            const auto shiftedBarrier =
                steps == Null<Size>() ? Null<Real>() :
                                        bgkShiftedBarrier(option.barrier, flatMarket.volatility,
                                                          fdDiscrete.process->time(exerciseDate) /
                                                              static_cast<Real>(steps));
            const auto references = References{
                analyticValuation(fdDiscrete.process, option, exerciseDate, option.barrier),
                shiftedBarrier == Null<Real>() ?
                    noValuation() :
                    analyticValuation(fdDiscrete.process, option, exerciseDate, shiftedBarrier),
            };

            std::cout << std::fixed << std::setprecision(3)
                      << "Reference, AnalyticBonusClassicEngine, the asset leg plus a "
                         "down-and-out put struck at\nthe bonus level:\n"
                      << "  continuous monitoring, barrier " << option.barrier << ": "
                      << references.continuous.npv << '\n';
            if (steps == Null<Size>()) {
                std::cout << "  discrete monitoring: -, the finite-difference engine it takes its "
                             "step from refused\n\n";
            } else {
                std::cout << "  discrete monitoring on " << steps
                          << " steps, Broadie-Glasserman-Kou barrier " << shiftedBarrier << ": "
                          << references.discrete.npv << "\n\n";
            }
            std::cout << std::defaultfloat;

            const auto rows =
                priceRows(option, flatMarket, smileMarket, references, fdDiscreteOutcome);
            printPrices(rows);

            std::cout << "\nProbabilities under the process's measure: hit, the barrier touched "
                         "on the engine's\nmonitoring; bonus, never touched and S_T below the "
                         "bonus level. Reference:\nAnalyticBonusClassicEngine on the barriers "
                         "above.\n\n";
            printProbabilities(rows);

            printRefusals(rows);
            return boost::algorithm::none_of(rows, isRefused);
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        auto app = CLI::App("Prices a BonusClassicOption on every engine and market rke_common "
                            "sets up, beside its analytic references. The market options set the "
                            "flat market and move the smile markets around it.");
        auto arguments = RKE::Example::Arguments();
        RKE::Example::addOptions(app, arguments);
        // Returns from main on a parse error or --help, with CLI11's exit code.
        CLI11_PARSE(app, argc, argv);

        return RKE::Example::run(arguments) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
