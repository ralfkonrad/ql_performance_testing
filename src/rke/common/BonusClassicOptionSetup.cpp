// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BonusClassicOptionSetup.hpp"
#include "FlatTermStructures.hpp"
#include <rke/ql/ext/pricingengines/bonusclassic/BinomialBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/math/interpolations/bicubicsplineinterpolation.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>
#include <ql/math/matrix.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/math/randomnumbers/sobolrsg.hpp>
#include <ql/math/statistics/statistics.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
#include <ql/methods/montecarlo/mctraits.hpp>
#include <ql/processes/eulerdiscretization.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <cmath>
#include <vector>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::Common {
    ext::shared_ptr<GeneralizedBlackScholesProcess>
    MarketData::makeGeneralizedBlackScholesProcess(Date today, bool forceDiscretization) const {
        const auto dc = Actual360();
        const auto spotQuote = ext::make_shared<SimpleQuote>(spot);
        const auto qTS = flatRate(today, dividendYield, dc);
        const auto rTS = flatRate(today, riskfreeRate, dc);
        const auto volTS = flatVol(today, volatility, dc);

        return ext::make_shared<BlackScholesMertonProcess>(
            Handle<Quote>(spotQuote), qTS, rTS, volTS, ext::make_shared<EulerDiscretization>(),
            forceDiscretization);
    }

    ext::shared_ptr<BonusClassicOption> makeBonusClassicOption(const OptionData& data, Date today) {
        return ext::make_shared<BonusClassicOption>(data.barrier, data.bonusLevel,
                                                    today + data.ttm);
    }

    ext::shared_ptr<GeneralizedBlackScholesProcess>
    SmileMarketData::makeGeneralizedBlackScholesProcess(Date today, bool isBicubic) const {
        const auto dc = Actual360();
        const auto spotQuote = ext::make_shared<SimpleQuote>(spot);

        auto curveDates = std::vector<Date>{today};
        for (const auto& tenor : curveTenors) {
            curveDates.push_back(today + tenor);
        }
        const auto qTS =
            ext::make_shared<ZeroCurve>(curveDates, dividendYields, dc, NullCalendar());
        const auto rTS = ext::make_shared<ZeroCurve>(curveDates, riskfreeRates, dc, NullCalendar());

        auto volDates = std::vector<Date>();
        for (const auto& tenor : volTenors) {
            volDates.push_back(today + tenor);
        }
        // Rows are strikes, columns dates.
        auto vols = Matrix(strikes.size(), volDates.size());
        for (Size i = 0; i < strikes.size(); ++i) {
            for (Size j = 0; j < volDates.size(); ++j) {
                vols(i, j) = atmVolatility - (skew * std::log(strikes[i] / spot));
            }
        }
        const auto volTS =
            ext::make_shared<BlackVarianceSurface>(today, NullCalendar(), volDates, strikes, vols,
                                                   dc, BlackVarianceSurface::ConstantExtrapolation,
                                                   BlackVarianceSurface::ConstantExtrapolation);
        if (isBicubic) {
            volTS->setInterpolation<Bicubic>();
        } else {
            volTS->setInterpolation<Bilinear>();
        }

        return ext::make_shared<BlackScholesMertonProcess>(
            Handle<Quote>(spotQuote), Handle<YieldTermStructure>(qTS),
            Handle<YieldTermStructure>(rTS), Handle<BlackVolTermStructure>(volTS));
    }

    namespace {
        // LowDiscrepancy with Joe-Kuo D7 direction integers, tabulated up to dimension 1898,
        // instead of SobolRsg's default Jaeckel ones, tabulated up to 32 and drawn from the seed
        // beyond. One dimension per time step, so a daily grid needs the former. Otherwise the
        // factory is GenericLowDiscrepancy's, icInstance included.
        struct LowDiscrepancyJoeKuoD7 : LowDiscrepancy {
            static rsg_type make_sequence_generator(Size dimension, BigNatural seed) {
                const auto g = SobolRsg(dimension, seed, SobolRsg::JoeKuoD7);
                return icInstance ? rsg_type(g, *icInstance) : rsg_type(g);
            }
        };

        template <template <class> class MC>
        ext::shared_ptr<PricingEngine>
        makeEngine(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                   Size timeStepsPerYear,
                   Size samples,
                   bool isBiased) {
            // maxSamples is McSimulation's own no-bound default; samples + 1 would wrap for the
            // largest count the --samples options admit.
            return ext::make_shared<MCBonusClassicEngine<LowDiscrepancyJoeKuoD7, Statistics, MC>>(
                process, timeStepsPerYear, samples, QL_MAX_INTEGER, Null<Real>(), isBiased, true,
                42);
        }
    }

    BonusClassicOptionSetup makeBonusClassicOptionSetup(
        bool isBiased, PathGeneration pathGeneration, Market market, Size samples, Engine engine) {
        QL_REQUIRE(engine == Engine::MonteCarlo || market == Market::Flat,
                   "only the Monte-Carlo engine prices a smile market");
        QL_REQUIRE(engine != Engine::Binomial || isBiased,
                   "the binomial engine monitors on every step, never continuously");

        const auto option_data = OptionData();
        const auto market_data = MarketData();

        const auto today = evaluationDate();
        Settings::instance().evaluationDate() = today;

        const auto process = market == Market::Flat ?
                                 market_data.makeGeneralizedBlackScholesProcess(today) :
                                 SmileMarketData().makeGeneralizedBlackScholesProcess(
                                     today, market == Market::SmileBicubic);
        const auto pricingEngine = [&]() -> ext::shared_ptr<PricingEngine> {
            if (engine == Engine::FiniteDifference) {
                // Discrete monitoring on the Monte-Carlo grid, so the two prices compare; the
                // rollback merges those dates into its own tGrid steps.
                return ext::make_shared<FdBlackScholesBonusClassicEngine>(
                    process, isBiased ? monitoringStepsPerYear : Null<Size>(), fdTimeGrid,
                    fdSpaceGrid);
            }
            if (engine == Engine::Binomial) {
                // Boyle-Lau on, maxTimeSteps at its default: without it the first knocked-out
                // layer at 400 steps sits 0.55% below the barrier and the price 0.5% off the
                // replication, see testBonusClassicOptionBinomialBoyleLau. The lattice is
                // therefore larger than treeTimeSteps; monitoringSteps() reports it.
                return ext::make_shared<BinomialBonusClassicEngine<CoxRossRubinstein>>(
                    process, treeTimeSteps);
            }
            // The Null<Real>() tolerance is mandatory, not a default: with no error estimate
            // under LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and
            // maxSamples never applies. The grid is a production run's whatever the path count.
            if (pathGeneration == PathGeneration::Uncached) {
                return makeEngine<SingleVariate>(process, monitoringStepsPerYear, samples,
                                                 isBiased);
            }
            return market == Market::Flat ?
                       makeEngine<CachedStepSingleVariate>(process, monitoringStepsPerYear, samples,
                                                           isBiased) :
                       makeEngine<LocalVolStepSingleVariate>(process, monitoringStepsPerYear,
                                                             samples, isBiased);
        }();

        const auto bonusClassicOption = makeBonusClassicOption(option_data, today);
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
