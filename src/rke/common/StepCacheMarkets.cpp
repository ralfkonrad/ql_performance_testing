// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "StepCacheMarkets.hpp"
#include "BonusClassicOptionSetup.hpp"
#include "FlatTermStructures.hpp"
#include <ql/math/matrix.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancecurve.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/termstructures/volatility/equityfx/localconstantvol.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <vector>

using namespace QuantLib;

namespace RKE::Common {
    namespace {
        Handle<Quote> spotQuote() {
            return Handle<Quote>(ext::make_shared<SimpleQuote>(MarketData().spot));
        }
    }

    Handle<YieldTermStructure> zeroCurve(Date today, const std::vector<Rate>& rates) {
        const std::vector<Date> curveDates = {
            today,
            today + Period(3, Months),
            today + Period(1, Years),
            today + Period(2, Years),
        };
        return Handle<YieldTermStructure>(
            ext::make_shared<ZeroCurve>(curveDates, rates, Actual360(), NullCalendar()));
    }

    Handle<YieldTermStructure> zeroRiskFree(Date today) {
        return zeroCurve(today, {0.010, 0.012, 0.018, 0.022});
    }

    Handle<YieldTermStructure> zeroDividend(Date today) {
        return zeroCurve(today, {0.030, 0.028, 0.025, 0.024});
    }

    ext::shared_ptr<GeneralizedBlackScholesProcess> varianceCurveProcess(Date today) {
        const std::vector<Date> volDates = {
            today + Period(3, Months),
            today + Period(1, Years),
            today + Period(2, Years),
        };
        const auto vol = Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceCurve>(
            today, volDates, std::vector<Volatility>{0.25, 0.21, 0.19}, Actual360()));
        return ext::make_shared<BlackScholesMertonProcess>(spotQuote(), zeroDividend(today),
                                                           zeroRiskFree(today), vol);
    }

    ext::shared_ptr<GeneralizedBlackScholesProcess> smileProcess(Date today) {
        const std::vector<Date> dates = {
            today + Period(6, Months),
            today + Period(1, Years),
            today + Period(2, Years),
        };
        const std::vector<Real> strikes = {60.0, 80.0, 100.0, 120.0, 140.0};
        // Rows are strikes, columns dates.
        Matrix vols(strikes.size(), dates.size());
        const std::vector<Volatility> skew = {0.28, 0.24, 0.20, 0.19, 0.19};
        for (Size i = 0; i < strikes.size(); ++i) {
            for (Size j = 0; j < dates.size(); ++j) {
                vols(i, j) = skew[i];
            }
        }
        const auto surface = Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceSurface>(
            today, NullCalendar(), dates, strikes, vols, Actual360(),
            BlackVarianceSurface::ConstantExtrapolation,
            BlackVarianceSurface::ConstantExtrapolation));
        return ext::make_shared<BlackScholesMertonProcess>(spotQuote(), zeroDividend(today),
                                                           zeroRiskFree(today), surface);
    }

    ext::shared_ptr<GeneralizedBlackScholesProcess> externalLocalVolProcess(Date today) {
        const auto data = MarketData();
        const auto dc = Actual360();
        const auto localVol = Handle<LocalVolTermStructure>(
            ext::make_shared<LocalConstantVol>(today, data.volatility, dc));
        return ext::make_shared<GeneralizedBlackScholesProcess>(
            spotQuote(), flatRate(today, data.dividendYield, dc),
            flatRate(today, data.riskfreeRate, dc), flatVol(today, data.volatility, dc), localVol);
    }
}
