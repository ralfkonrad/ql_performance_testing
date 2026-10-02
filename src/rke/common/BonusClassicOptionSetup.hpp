// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BONUSCLASSICOPTIONSETUP_HPP
#define BONUSCLASSICOPTIONSETUP_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/instrument.hpp>
#include <ql/pricingengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>

namespace RKE::Common {
    struct BonusClassicOptionSetup {
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process;
        QuantLib::ext::shared_ptr<QuantLib::PricingEngine> engine;
        QuantLib::ext::shared_ptr<RKE::QL::Ext::BonusClassicOption> option;
    };

    // Sets the evaluation date to 22 Jun 2025 and prices with MCBonusClassicEngine under
    // LowDiscrepancy; isBiased selects discrete monitoring, otherwise continuous.
    [[nodiscard]] BonusClassicOptionSetup makeBonusClassicOptionSetup(bool isBiased);

    // The measured work. recalculate() is the point: NPV() alone returns the cached value, so a
    // loop would time one pricing and the rest cache reads.
    [[nodiscard]] inline QuantLib::Real reprice(QuantLib::Instrument& instrument) {
        instrument.recalculate();
        return instrument.NPV();
    }
}

#endif // BONUSCLASSICOPTIONSETUP_HPP
