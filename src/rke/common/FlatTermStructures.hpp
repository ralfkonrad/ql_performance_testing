// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef FLATTERMSTRUCTURES_HPP
#define FLATTERMSTRUCTURES_HPP

#include <ql/handle.hpp>
#include <ql/termstructures/volatility/equityfx/blackvoltermstructure.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/time/date.hpp>
#include <ql/time/daycounter.hpp>
#include <ql/types.hpp>

namespace RKE::Common {
    // A FlatForward at the given rate, continuously compounded, referenced to today.
    [[nodiscard]] QuantLib::Handle<QuantLib::YieldTermStructure>
    flatRate(const QuantLib::Date& today, QuantLib::Rate rate, const QuantLib::DayCounter& dc);

    // A BlackConstantVol over a NullCalendar, referenced to today.
    [[nodiscard]] QuantLib::Handle<QuantLib::BlackVolTermStructure>
    flatVol(const QuantLib::Date& today, QuantLib::Volatility vol, const QuantLib::DayCounter& dc);
}

#endif // FLATTERMSTRUCTURES_HPP
