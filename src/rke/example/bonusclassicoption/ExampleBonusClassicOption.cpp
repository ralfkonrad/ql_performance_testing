// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

// Prices one BonusClassicOption under every monitoring mode, market, engine and path
// generation rke_common sets up, and prints each price next to its analytic replication: the
// asset leg plus a down-and-out put struck at the bonus level, priced by
// QuantLib::AnalyticBarrierEngine on the flat market. The example counterpart of
// rke_profile_bonusclassicoption, which prices the same setups for perf and valgrind.
//
//     rke_example_bonusclassicoption
//
// The rows are grouped by monitoring, then market. The flat rows price at a production run's
// 2^16 paths; the smile rows at 2^10, since the example runs as a smoke test on every pull
// request and the bicubic surface under SingleVariate is slow. A smile row prints no
// reference: there is none under local volatility.

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
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

        struct Row {
            bool isDiscrete;
            Market market;
            Pricer pricer;
            Size paths; // Null<Size>() off the Monte-Carlo engine.
            Real npv;
            Real reference; // Null<Real>() on the smiles.
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

        Real price(bool isDiscrete, Market market, Pricer pricer, Size samples) {
            const auto setup = makeBonusClassicOptionSetup(isDiscrete, pricer.pathGeneration,
                                                           market, samples, pricer.engine);
            return reprice(*setup.option);
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

        // Grouped by monitoring, then market: a label repeats only where its group changes.
        void printRows(const std::vector<Row>& rows) {
            constexpr int monitoringWidth = 12;
            constexpr int marketWidth = 17;
            constexpr int engineWidth = 28;
            constexpr int pathsWidth = 7;
            constexpr int priceWidth = 10;
            constexpr int differenceWidth = 10;

            std::cout << std::left << std::setw(monitoringWidth) << "monitoring"
                      << std::setw(marketWidth) << "market" << std::setw(engineWidth) << "engine"
                      << std::right << std::setw(pathsWidth) << "paths" << std::setw(priceWidth)
                      << "NPV" << std::setw(priceWidth) << "reference" << std::setw(differenceWidth)
                      << "rel.diff" << '\n';

            const Row* previous = nullptr;
            for (const auto& row : rows) {
                const auto newMonitoring =
                    previous == nullptr || previous->isDiscrete != row.isDiscrete;
                const auto newMarket = newMonitoring || previous->market != row.market;
                previous = &row;

                std::cout << std::left << std::setw(monitoringWidth)
                          << (newMonitoring ? name(row.isDiscrete) : "") << std::setw(marketWidth)
                          << (newMarket ? name(row.market) : "") << std::setw(engineWidth)
                          << name(row.pricer) << std::right << std::setw(pathsWidth);
                if (row.paths == Null<Size>()) {
                    std::cout << "-";
                } else {
                    std::cout << row.paths;
                }
                std::cout << std::fixed << std::setprecision(3) << std::setw(priceWidth) << row.npv;
                if (row.reference == Null<Real>()) {
                    std::cout << std::setw(priceWidth) << "-" << std::setw(differenceWidth) << "-";
                } else {
                    std::cout << std::setw(priceWidth) << row.reference << std::scientific
                              << std::setprecision(1) << std::setw(differenceWidth)
                              << std::fabs(row.npv - row.reference) / row.reference;
                }
                std::cout << std::defaultfloat << '\n';
            }
        }

        // AnalyticBarrierEngine assumes continuous monitoring. The continuous rows and the tree,
        // whose Boyle-Lau layer sits at the barrier, compare against the barrier itself; the
        // rows monitored every dt against the Broadie-Glasserman-Kou barrier.
        struct References {
            Real continuous;
            Real discrete;
        };

        Real
        reference(const References& references, bool isDiscrete, Market market, Pricer pricer) {
            if (market != Market::Flat) {
                return Null<Real>();
            }
            if (isDiscrete && pricer.engine != Engine::Binomial) {
                return references.discrete;
            }
            return references.continuous;
        }

        // The setup rejects the other engines off the flat market and the tree under
        // continuous monitoring.
        bool admits(bool isDiscrete, Market market, Pricer pricer) {
            const auto isFlat = market == Market::Flat;
            return (pricer.engine == Engine::MonteCarlo || isFlat) &&
                   (pricer.engine != Engine::Binomial || isDiscrete);
        }

        // Every permutation the setup admits, in the table's order. The finite-difference row
        // under discrete monitoring is priced already: its grid gave the step the discrete
        // reference corrects for.
        std::vector<Row> priceRows(const References& references, Real fdDiscreteNpv) {
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
                        const auto isFdDiscrete =
                            pricer.engine == Engine::FiniteDifference && isDiscrete;
                        const auto samples =
                            market == Market::Flat ? productionSamples : smileSamples;
                        rows.push_back({
                            isDiscrete,
                            market,
                            pricer,
                            isMonteCarlo ? samples : Null<Size>(),
                            isFdDiscrete ? fdDiscreteNpv :
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
                                            productionSamples, Engine::FiniteDifference);
            const auto fdDiscreteNpv = reprice(*fdDiscrete.option);
            const auto exerciseDate = fdDiscrete.option->exercise()->lastDate();
            const auto dt = fdDiscrete.process->time(exerciseDate) /
                            static_cast<Real>(monitoringSteps(fdDiscrete));

            printHeader(option, market, exerciseDate);

            const auto shiftedBarrier = bgkShiftedBarrier(option.barrier, market.volatility, dt);
            const auto references = References{
                replicationPrice(fdDiscrete.process, option, exerciseDate, option.barrier),
                replicationPrice(fdDiscrete.process, option, exerciseDate, shiftedBarrier),
            };

            std::cout << std::fixed << std::setprecision(3)
                      << "Replication, asset leg plus a down-and-out put struck at the bonus "
                         "level, AnalyticBarrierEngine:\n"
                      << "  continuous monitoring, barrier " << option.barrier << ": "
                      << references.continuous << '\n'
                      << "  discrete monitoring on " << monitoringSteps(fdDiscrete)
                      << " steps, Broadie-Glasserman-Kou barrier " << shiftedBarrier << ": "
                      << references.discrete << "\n\n"
                      << std::defaultfloat;

            printRows(priceRows(references, fdDiscreteNpv));
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
