// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef STEPCACHEMARKETS_HPP
#define STEPCACHEMARKETS_HPP

#include <ql/handle.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/time/date.hpp>
#include <ql/types.hpp>
#include <vector>

namespace RKE::Common {
    // Markets that exercise one branch of the step caches each, beside MarketData's flat one:
    // a variance curve on zero curves for the exact step off curves, a smile for the one
    // BlackScholesStepCache refuses and LocalVolStepCache takes through LocalVolSurface, and an
    // external local volatility for the Euler step the type check alone would miss. Spot is
    // MarketData's, Actual360 and NullCalendar throughout, referenced to today.

    // Zero rates linear between the nodes today, 3M, 1Y and 2Y, continuously compounded.
    [[nodiscard]] QuantLib::Handle<QuantLib::YieldTermStructure>
    zeroCurve(QuantLib::Date today, const std::vector<QuantLib::Rate>& rates);

    [[nodiscard]] QuantLib::Handle<QuantLib::YieldTermStructure> zeroRiskFree(QuantLib::Date today);

    [[nodiscard]] QuantLib::Handle<QuantLib::YieldTermStructure> zeroDividend(QuantLib::Date today);

    // A Black variance curve, 3M, 1Y and 2Y, on the zero curves.
    [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
    varianceCurveProcess(QuantLib::Date today);

    // A mild skew, flat in time, bilinear in time and strike, on the zero curves.
    [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
    smileProcess(QuantLib::Date today);

    // Euler steps over a BlackConstantVol on MarketData's flat curves, which the type check
    // alone would miss.
    [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
    externalLocalVolProcess(QuantLib::Date today);
}

#endif // STEPCACHEMARKETS_HPP
