// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BONUSCLASSICOPTIONSETUP_HPP
#define BONUSCLASSICOPTIONSETUP_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/instrument.hpp>
#include <ql/pricingengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <cstdint>

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
