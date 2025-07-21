//
// Created by ralf.eckel on 20.03.2023.
//

#include "ql_calendar_testing.hpp"
#include <ql/time/calendars/target.hpp>
#include <vector>

void BM_TestCalendar(benchmark::State& state) {
    for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
        auto target = QuantLib::TARGET();

        auto numberOfDates = QuantLib::Date::maxDate() - QuantLib::Date::minDate() + 1;
        auto isBusinessDate = std::vector<bool>(numberOfDates);

        auto i = 0;
        for (auto date = QuantLib::Date::minDate(); date < QuantLib::Date::maxDate();) {
            isBusinessDate[i++] = target.isBusinessDay(date);
            date++;
        }
    }
}
