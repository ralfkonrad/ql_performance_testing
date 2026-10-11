// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "MarketData.hpp"
#include "FlatTermStructures.hpp"
#include <ql/errors.hpp>
#include <ql/math/interpolations/bicubicsplineinterpolation.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>
#include <ql/math/matrix.hpp>
#include <ql/processes/eulerdiscretization.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <cmath>
#include <vector>

using namespace QuantLib;

namespace RKE::Common {
    ext::shared_ptr<GeneralizedBlackScholesProcess>
    FlatMarketData::makeGeneralizedBlackScholesProcess(Date today, bool forceDiscretization) const {
        const auto dc = Actual360();
        const auto spotQuote = ext::make_shared<SimpleQuote>(spot);
        const auto qTS = flatRate(today, dividendYield, dc);
        const auto rTS = flatRate(today, riskfreeRate, dc);
        const auto volTS = flatVol(today, volatility, dc);

        return ext::make_shared<BlackScholesMertonProcess>(
            Handle<Quote>(spotQuote), qTS, rTS, volTS, ext::make_shared<EulerDiscretization>(),
            forceDiscretization);
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

    SmileMarketData smileMarketDataAround(const FlatMarketData& flat) {
        QL_REQUIRE(flat.volatility > 0.0,
                   "the smile scales with the volatility, which has to be positive, not "
                       << flat.volatility);
        const auto defaults = FlatMarketData();
        auto smile = SmileMarketData();
        smile.spot = flat.spot;
        for (auto& rate : smile.riskfreeRates) {
            rate += flat.riskfreeRate - defaults.riskfreeRate;
        }
        for (auto& yield : smile.dividendYields) {
            yield += flat.dividendYield - defaults.dividendYield;
        }
        const auto scale = flat.volatility / smile.atmVolatility;
        smile.atmVolatility = flat.volatility;
        smile.skew *= scale;
        return smile;
    }
}
