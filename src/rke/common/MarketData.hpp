// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef MARKETDATA_HPP
#define MARKETDATA_HPP

#include <ql/processes/blackscholesprocess.hpp>
#include <ql/time/date.hpp>
#include <ql/time/period.hpp>
#include <ql/types.hpp>
#include <vector>

namespace RKE::Common {
    // The reference date every test, benchmark and profile prices as of.
    [[nodiscard]] inline QuantLib::Date evaluationDate() {
        return {22, QuantLib::Jun, 2025};
    }

    // The levels every market shares: the spot, and the volatility, flat on FlatMarketData and
    // at the money on SmileMarketData.
    inline constexpr QuantLib::Real referenceSpot = 100.0;
    inline constexpr QuantLib::Volatility referenceVolatility = 0.20;

    // The flat reference market: the tests validate it, the benchmarks and profiles measure it.
    // Flat forward curves and a constant Black volatility, Actual360 and NullCalendar
    // throughout, under EulerDiscretization. forceDiscretization = true makes evolve() take
    // Euler steps over the flat volatility, which coincide with the exact step in exact
    // arithmetic.
    struct FlatMarketData {
        QuantLib::Real spot = referenceSpot;
        QuantLib::Real riskfreeRate = 0.01;
        QuantLib::Real dividendYield = 0.03;
        QuantLib::Volatility volatility = referenceVolatility;

        [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
        makeGeneralizedBlackScholesProcess(QuantLib::Date today,
                                           bool forceDiscretization = false) const;
    };

    // Around FlatMarketData's levels, shaped so every call in the Euler step does real work:
    // zero rates linear between the nodes, continuously compounded, and a Black variance surface
    // bilinear or bicubic in time and strike. Actual360 and NullCalendar throughout.
    struct SmileMarketData {
        QuantLib::Real spot = referenceSpot;
        std::vector<QuantLib::Period> curveTenors = {
            QuantLib::Period(3, QuantLib::Months),
            QuantLib::Period(6, QuantLib::Months),
            QuantLib::Period(1, QuantLib::Years),
            QuantLib::Period(2, QuantLib::Years),
        };
        std::vector<QuantLib::Rate> riskfreeRates = {0.008, 0.009, 0.010, 0.011, 0.012};
        std::vector<QuantLib::Rate> dividendYields = {0.032, 0.031, 0.030, 0.029, 0.028};

        std::vector<QuantLib::Period> volTenors = {
            QuantLib::Period(1, QuantLib::Months),  QuantLib::Period(3, QuantLib::Months),
            QuantLib::Period(6, QuantLib::Months),  QuantLib::Period(1, QuantLib::Years),
            QuantLib::Period(18, QuantLib::Months), QuantLib::Period(2, QuantLib::Years),
        };
        // Far beyond any path, so the surface's flat strike extrapolation, whose kink
        // LocalVolSurface's finite differences would turn into a negative local variance, is
        // never reached.
        std::vector<QuantLib::Real> strikes = {
            10.0, 25.0, 50.0, 70.0, 85.0, 100.0, 115.0, 130.0, 160.0, 220.0, 400.0,
        };
        // sigma(K) = atmVolatility - skew * ln(K / spot) on every date. Total variance
        // sigma(K)^2 * T rises in time, and the skew is mild enough that LocalVolSurface's
        // Dupire denominator stays positive on every strike a path reaches.
        QuantLib::Volatility atmVolatility = referenceVolatility;
        QuantLib::Real skew = 0.08;

        [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
        makeGeneralizedBlackScholesProcess(QuantLib::Date today, bool isBicubic) const;
    };
}

#endif // MARKETDATA_HPP
