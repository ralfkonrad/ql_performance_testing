// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "ql_calendar_testing.hpp"
#include <ql/time/calendars/target.hpp>
#include <vector>

namespace RKE::Benchmark {
    void BM_TestCalendar(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            // Not setup leaking into the measurement: all TARGET instances share one static Impl,
            // so only isBusinessDay is timed.
            const auto target = QuantLib::TARGET();

            const auto numberOfDates = QuantLib::Date::maxDate() - QuantLib::Date::minDate() + 1;
            auto isBusinessDate = std::vector<bool>(numberOfDates);

            auto i = 0;
            for (auto date = QuantLib::Date::minDate(); date < QuantLib::Date::maxDate();) {
                isBusinessDate[i++] = target.isBusinessDay(date);
                date++;
            }
        }
    }
}
