// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BONUSCLASSICOPTIONSETUP_HPP
#define BONUSCLASSICOPTIONSETUP_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/instrument.hpp>
#include <ql/pricingengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/time/date.hpp>
#include <ql/time/period.hpp>
#include <cstdint>
#include <vector>

namespace RKE::Common {
    struct BonusClassicOptionSetup {
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process;
        QuantLib::ext::shared_ptr<QuantLib::PricingEngine> engine;
        QuantLib::ext::shared_ptr<RKE::QL::Ext::BonusClassicOption> option;
    };

    // The MC traits of MCBonusClassicEngine: the step cache of the market's branch,
    // CachedStepSingleVariate on the flat market and LocalVolStepSingleVariate on the smiles, or
    // QuantLib::SingleVariate, which asks the process for every step again. They price
    // bit-identically, except on the smiles where the compiler contracts to FMAs: there the
    // cached step may round differently, and the prices differ by up to 2e-6 relative.
    enum class PathGeneration : std::uint8_t { CachedStep, Uncached };

    // Flat: flat curves and a constant volatility, the exact step. The smiles: zero curves and a
    // volatility surface, the Euler step through LocalVolSurface that a production market takes,
    // with the surface bilinear or bicubic in time and strike. The bicubic surface rebuilds a
    // spline on every lookup, which dominates its profile.
    enum class Market : std::uint8_t { Flat, SmileBilinear, SmileBicubic };

    // The reference date every test, benchmark and profile prices as of.
    [[nodiscard]] inline QuantLib::Date evaluationDate() {
        return {22, QuantLib::Jun, 2025};
    }

    struct OptionData {
        QuantLib::Real barrier = 90.0;
        QuantLib::Real bonusLevel = 120.00;
        // The low end of a bonus certificate's usual one to two years; the tests price 5M.
        QuantLib::Period ttm = QuantLib::Period(1, QuantLib::Years);
    };

    // The flat reference market: the tests validate it, the benchmarks and profiles measure it.
    // Flat forward curves and a constant Black volatility, Actual360 and NullCalendar
    // throughout, under EulerDiscretization. forceDiscretization = true makes evolve() take
    // Euler steps over the flat volatility, which BlackScholesStepCache cannot reproduce.
    struct MarketData {
        QuantLib::Real spot = 100.00;
        QuantLib::Real riskfreeRate = 0.01;
        QuantLib::Real dividendYield = 0.03;
        QuantLib::Volatility volatility = 0.20;

        [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
        makeGeneralizedBlackScholesProcess(QuantLib::Date today,
                                           bool forceDiscretization = false) const;
    };

    // Around MarketData's levels, shaped so every call in the Euler step does real work: zero
    // rates linear between the nodes, continuously compounded, and a Black variance surface
    // bilinear or bicubic in time and strike. Actual360 and NullCalendar throughout.
    struct SmileMarketData {
        QuantLib::Real spot = 100.00;
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
        QuantLib::Volatility atmVolatility = 0.20;
        QuantLib::Real skew = 0.08;

        [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>
        makeGeneralizedBlackScholesProcess(QuantLib::Date today, bool isBicubic) const;
    };

    // The certificate on data's levels, exercisable at today + data.ttm, with no engine set.
    [[nodiscard]] QuantLib::ext::shared_ptr<RKE::QL::Ext::BonusClassicOption>
    makeBonusClassicOption(const OptionData& data, QuantLib::Date today);

    // A production run's path count: a power of two, where a Sobol sequence is balanced.
    // SobolRsg skips the zero point, so these are points 1 to 2^16, the net with one point
    // swapped.
    inline constexpr QuantLib::Size productionSamples = QuantLib::Size{1} << 16U;

    // Sets the evaluation date to 22 Jun 2025 and prices with MCBonusClassicEngine under
    // LowDiscrepancy; isBiased selects discrete monitoring, otherwise continuous. A smoke test
    // passes fewer samples than productionSamples, on the same time grid.
    [[nodiscard]] BonusClassicOptionSetup
    makeBonusClassicOptionSetup(bool isBiased,
                                PathGeneration pathGeneration = PathGeneration::CachedStep,
                                Market market = Market::Flat,
                                QuantLib::Size samples = productionSamples);

    // The measured work. recalculate() is the point: NPV() alone returns the cached value, so a
    // loop would time one pricing and the rest cache reads.
    [[nodiscard]] inline QuantLib::Real reprice(QuantLib::Instrument& instrument) {
        instrument.recalculate();
        return instrument.NPV();
    }
}

#endif // BONUSCLASSICOPTIONSETUP_HPP
