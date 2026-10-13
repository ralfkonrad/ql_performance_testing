// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BARRIEROPTIONSETUP_HPP
#define BARRIEROPTIONSETUP_HPP

#include <rke/common/BonusClassicOptionSetup.hpp>
#include <rke/common/MarketData.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BarrierMonitoring.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/pricingengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/time/date.hpp>
#include <ql/time/period.hpp>

namespace RKE::Common {
    struct BarrierOptionSetup {
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process;
        QuantLib::ext::shared_ptr<QuantLib::PricingEngine> engine;
        QuantLib::ext::shared_ptr<QuantLib::BarrierOption> option;
    };

    // The put leg of the certificate OptionData describes: a down-and-out put struck at the
    // bonus level and knocked out at the barrier, without rebate. It shares the certificate's
    // knock-out, monitoring grid and paths, and prices through QuantLib alone.
    struct BarrierOptionData {
        QuantLib::Real barrier = 70.0;
        QuantLib::Real strike = 120.0;
        QuantLib::Real rebate = 0.0;
        QuantLib::Period ttm = QuantLib::Period(1, QuantLib::Years);
    };

    // The down-and-out put on data's levels, a PlainVanillaPayoff exercisable at
    // today + data.ttm, with no engine set.
    [[nodiscard]] QuantLib::ext::shared_ptr<QuantLib::BarrierOption>
    makeBarrierOption(const BarrierOptionData& data, QuantLib::Date today);

    // Sets the evaluation date to 22 Jun 2025 and prices the put on data's levels with
    // QuantLib::MCBarrierEngine<LowDiscrepancyJoeKuoD7> on the market: flatMarketData on
    // Market::Flat, smileMarketData on the smiles. The grid is monitoringStepsPerYear over the
    // process's Actual360 year fraction, 255 steps on 1Y as for the certificate; Brownian bridge
    // on, antithetic paths off, seed 42, and samples paths in the fixed-sample branch.
    // Discrete monitoring is the engine's isBiased = true, BiasedBarrierPathPricer, which knocks
    // out at or below the barrier on the grid points after t = 0. Continuous is
    // isBiased = false, BarrierPathPricer, which draws each step's Brownian-bridge minimum from
    // the process's diffusion() at the step's start and a MersenneTwister of its own, seeded 5.
    [[nodiscard]] BarrierOptionSetup
    makeBarrierOptionSetup(const BarrierOptionData& data,
                           const FlatMarketData& flatMarketData,
                           const SmileMarketData& smileMarketData,
                           RKE::QL::Ext::BarrierMonitoring monitoring,
                           Market market,
                           QuantLib::Size samples);
}

#endif // BARRIEROPTIONSETUP_HPP
