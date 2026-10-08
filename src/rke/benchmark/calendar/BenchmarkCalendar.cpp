// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <ql/time/calendars/target.hpp>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <vector>

namespace RKE::Benchmark {
    namespace {
        void BM_TestCalendar(benchmark::State& state) {
            // All TARGET instances share one static Impl, and the result vector is allocated
            // once here, so only isBusinessDay is timed. The loop runs over serial numbers
            // because incrementing a Date past maxDate() throws.
            const auto target = QuantLib::TARGET();
            const auto first = QuantLib::Date::minDate().serialNumber();
            const auto last = QuantLib::Date::maxDate().serialNumber();
            auto isBusinessDate = std::vector<bool>(static_cast<std::size_t>(last - first + 1));

            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                for (auto serial = first; serial <= last; ++serial) {
                    isBusinessDate[static_cast<std::size_t>(serial - first)] =
                        target.isBusinessDay(QuantLib::Date(serial));
                }
                benchmark::DoNotOptimize(isBusinessDate);
            }
        }

        BENCHMARK(BM_TestCalendar)->Name("TARGET.isBusinessDay();")->Unit(benchmark::kMillisecond);
    }
}

BENCHMARK_MAIN();
