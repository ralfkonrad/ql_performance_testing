// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BonusClassicOptionSetup.hpp"
#include "FlatTermStructures.hpp"
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/math/interpolations/bicubicsplineinterpolation.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>
#include <ql/math/matrix.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/math/randomnumbers/sobolrsg.hpp>
#include <ql/math/statistics/statistics.hpp>
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

        struct OptionData {
            Real barrier = 90.0;
            Real bonusLevel = 120.00;
            // The low end of a bonus certificate's usual one to two years; the tests price 5M.
            Period ttm = Period(1, Years);
        };

        // Around MarketData's levels, shaped so every call in the Euler step does real work:
        // zero rates linear between the nodes, continuously compounded, and a Black variance
        // surface bilinear or bicubic in time and strike. Actual360 and NullCalendar throughout.
        struct SmileMarketData {
            Real spot = 100.00;
            std::vector<Period> curveTenors = {
                Period(3, Months),
                Period(6, Months),
                Period(1, Years),
                Period(2, Years),
            };
            std::vector<Rate> riskfreeRates = {0.008, 0.009, 0.010, 0.011, 0.012};
            std::vector<Rate> dividendYields = {0.032, 0.031, 0.030, 0.029, 0.028};

            std::vector<Period> volTenors = {
                Period(1, Months), Period(3, Months),  Period(6, Months),
                Period(1, Years),  Period(18, Months), Period(2, Years),
            };
            // Far beyond any path, so the surface's flat strike extrapolation, whose kink
            // LocalVolSurface's finite differences would turn into a negative local variance,
            // is never reached.
            std::vector<Real> strikes = {
                10.0, 25.0, 50.0, 70.0, 85.0, 100.0, 115.0, 130.0, 160.0, 220.0, 400.0,
            };
            // sigma(K) = atmVolatility - skew * ln(K / spot) on every date. Total variance
            // sigma(K)^2 * T rises in time, and the skew is mild enough that LocalVolSurface's
            // Dupire denominator stays positive on every strike a path reaches.
            Volatility atmVolatility = 0.20;
            Real skew = 0.08;

            [[nodiscard]] ext::shared_ptr<GeneralizedBlackScholesProcess>
            makeGeneralizedBlackScholesProcess(Date today, bool isBicubic) const {
                const auto dc = Actual360();
                const auto spotQuote = ext::make_shared<SimpleQuote>(spot);

                auto curveDates = std::vector<Date>{today};
                for (const auto& tenor : curveTenors) {
                    curveDates.push_back(today + tenor);
                }
                const auto qTS =
                    ext::make_shared<ZeroCurve>(curveDates, dividendYields, dc, NullCalendar());
                const auto rTS =
                    ext::make_shared<ZeroCurve>(curveDates, riskfreeRates, dc, NullCalendar());

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
                const auto volTS = ext::make_shared<BlackVarianceSurface>(
                    today, NullCalendar(), volDates, strikes, vols, dc,
                    BlackVarianceSurface::ConstantExtrapolation,
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
        };
    }

    BonusClassicOptionSetup makeBonusClassicOptionSetup(bool isBiased,
                                                        PathGeneration pathGeneration,
                                                        Market market,
                                                        Size samples) {
        const auto option_data = OptionData();
        const auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market == Market::Flat ?
                                 market_data.makeGeneralizedBlackScholesProcess(today) :
                                 SmileMarketData().makeGeneralizedBlackScholesProcess(
                                     today, market == Market::SmileBicubic);
        // The Null<Real>() tolerance is mandatory, not a default: with no error estimate under
        // LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and maxSamples
        // never applies. The grid is a production run's whatever the path count: about one step
        // per business day, 255 over the Actual360 year fraction 365/360, though not on the
        // business days themselves.
        constexpr Size timeStepsPerYear = 252;
        const auto mcEngine = [&] {
            if (pathGeneration == PathGeneration::Uncached) {
                return makeEngine<SingleVariate>(process, timeStepsPerYear, samples, isBiased);
            }
            return market == Market::Flat ?
                       makeEngine<CachedStepSingleVariate>(process, timeStepsPerYear, samples,
                                                           isBiased) :
                       makeEngine<LocalVolStepSingleVariate>(process, timeStepsPerYear, samples,
                                                             isBiased);
        }();

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        bonusClassicOption->setPricingEngine(mcEngine);

        return {process, mcEngine, bonusClassicOption};
    }
}
