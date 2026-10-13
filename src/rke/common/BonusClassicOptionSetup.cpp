// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BonusClassicOptionSetup.hpp"
#include "LowDiscrepancyJoeKuoD7.hpp"
#include "MarketData.hpp"
#include <rke/ql/ext/pricingengines/bonusclassic/AnalyticBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BinomialBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
#include <ql/methods/montecarlo/mctraits.hpp>
#include <ql/settings.hpp>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::Common {
    ext::shared_ptr<BonusClassicOption> makeBonusClassicOption(const OptionData& data, Date today) {
        return ext::make_shared<BonusClassicOption>(data.barrier, data.bonusLevel,
                                                    today + data.ttm);
    }

    namespace {
        template <template <class> class MC>
        ext::shared_ptr<PricingEngine>
        makeEngine(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                   Size timeStepsPerYear,
                   Size samples,
                   BarrierMonitoring monitoring) {
            // maxSamples is McSimulation's own no-bound default; samples + 1 would wrap for the
            // largest count the --samples options admit.
            return ext::make_shared<MCBonusClassicEngine<MC, LowDiscrepancyJoeKuoD7>>(
                process, timeStepsPerYear, true, samples, Null<Real>(), QL_MAX_INTEGER, monitoring,
                42);
        }

        // The combinations an engine cannot price, refused before a process is built.
        void requireSupported(BarrierMonitoring monitoring, Market market, Engine engine) {
            QL_REQUIRE(engine != Engine::Binomial || market == Market::Flat,
                       "the binomial engine flattens the volatility, so it prices the flat "
                       "market only");
            QL_REQUIRE(engine != Engine::Binomial || monitoring == BarrierMonitoring::Discrete,
                       "the binomial engine monitors on every step, never continuously");
            // The engine refuses the smiles itself; this says why.
            QL_REQUIRE(engine != Engine::Analytic || market == Market::Flat,
                       "the analytic engine needs a constant volatility, so it prices the flat "
                       "market only");
            QL_REQUIRE(engine != Engine::Analytic || monitoring == BarrierMonitoring::Continuous,
                       "the analytic engine monitors continuously, never discretely");
        }
    }

    BonusClassicOptionSetup
    makeBonusClassicOptionSetup(const OptionData& optionData,
                                const FlatMarketData& flatMarketData,
                                const SmileMarketData& smileMarketData,
                                BarrierMonitoring monitoring,
                                PathGeneration pathGeneration,
                                Market market,
                                Size samples,
                                Engine engine,
                                ProbabilityCalculation probabilityCalculation) {
        requireSupported(monitoring, market, engine);
        const auto calculateProbabilities = probabilityCalculation == ProbabilityCalculation::On;

        const auto today = evaluationDate();
        Settings::instance().evaluationDate() = today;

        const auto process = market == Market::Flat ?
                                 flatMarketData.makeGeneralizedBlackScholesProcess(today) :
                                 smileMarketData.makeGeneralizedBlackScholesProcess(
                                     today, market == Market::SmileBicubic);
        const auto pricingEngine = [&]() -> ext::shared_ptr<PricingEngine> {
            if (engine == Engine::FiniteDifference) {
                // Discrete monitoring on the Monte-Carlo grid, so the two prices compare; the
                // rollback merges those dates into its own tGrid steps. No damping steps and
                // TrBDF2 are the engine's own defaults, spelled out to reach the flags behind
                // them. The smiles take the local volatility, the surface the Monte-Carlo
                // engine steps through; the engine refuses them without it.
                return ext::make_shared<FdBlackScholesBonusClassicEngine>(
                    process,
                    monitoring == BarrierMonitoring::Discrete ? monitoringStepsPerYear :
                                                                Null<Size>(),
                    fdTimeGrid, fdSpaceGrid, 0, FdmSchemeDesc::TrBDF2(), market != Market::Flat,
                    calculateProbabilities);
            }
            if (engine == Engine::Binomial) {
                // Boyle-Lau on, maxTimeSteps at its default: without it the first knocked-out
                // layer at 400 steps sits 0.19% below the barrier and the price 1.7e-4 off the
                // replication, see testBonusClassicOptionBinomialBoyleLau. The lattice is
                // therefore larger than treeTimeSteps; monitoringSteps() reports it.
                return ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
                    process, treeTimeSteps, 0, calculateProbabilities);
            }
            if (engine == Engine::Analytic) {
                return ext::make_shared<AnalyticBonusClassicEngine>(process,
                                                                    calculateProbabilities);
            }
            // The Null<Real>() tolerance is mandatory, not a default: with no error estimate
            // under LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and
            // maxSamples never applies. The grid is a production run's whatever the path count.
            if (pathGeneration == PathGeneration::Uncached) {
                return makeEngine<SingleVariate>(process, monitoringStepsPerYear, samples,
                                                 monitoring);
            }
            return market == Market::Flat ?
                       makeEngine<CachedStepSingleVariate>(process, monitoringStepsPerYear, samples,
                                                           monitoring) :
                       makeEngine<LocalVolStepSingleVariate>(process, monitoringStepsPerYear,
                                                             samples, monitoring);
        }();

        const auto bonusClassicOption = makeBonusClassicOption(optionData, today);
        bonusClassicOption->setPricingEngine(pricingEngine);

        return {process, pricingEngine, bonusClassicOption};
    }

    Size monitoringSteps(const BonusClassicOptionSetup& setup) {
        if (const auto fd =
                ext::dynamic_pointer_cast<FdBlackScholesBonusClassicEngine>(setup.engine)) {
            return fd->monitorsContinuously() ? Null<Size>() : fd->timeGrid().size() - 1;
        }
        if (const auto tree =
                ext::dynamic_pointer_cast<BinomialBonusClassicEngine<CoxRossRubinstein>>(
                    setup.engine)) {
            return tree->timeGrid().size() - 1;
        }
        return Null<Size>();
    }
}
