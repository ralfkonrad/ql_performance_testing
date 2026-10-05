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

    // Sets the evaluation date to 22 Jun 2025 and prices with MCBonusClassicEngine under
    // LowDiscrepancy; isBiased selects discrete monitoring, otherwise continuous.
    [[nodiscard]] BonusClassicOptionSetup
    makeBonusClassicOptionSetup(bool isBiased,
                                PathGeneration pathGeneration = PathGeneration::CachedStep,
                                Market market = Market::Flat);

    // The measured work. recalculate() is the point: NPV() alone returns the cached value, so a
    // loop would time one pricing and the rest cache reads.
    [[nodiscard]] inline QuantLib::Real reprice(QuantLib::Instrument& instrument) {
        instrument.recalculate();
        return instrument.NPV();
    }
}

#endif // BONUSCLASSICOPTIONSETUP_HPP
