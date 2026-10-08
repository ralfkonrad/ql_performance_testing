// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "FlatTermStructures.hpp"
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>

using namespace QuantLib;

namespace RKE::Common {
    Handle<YieldTermStructure> flatRate(const Date& today, Rate rate, const DayCounter& dc) {
        return Handle<YieldTermStructure>(ext::make_shared<FlatForward>(today, rate, dc));
    }

    Handle<BlackVolTermStructure> flatVol(const Date& today, Volatility vol, const DayCounter& dc) {
        return Handle<BlackVolTermStructure>(
            ext::make_shared<BlackConstantVol>(today, NullCalendar(), vol, dc));
    }
}
