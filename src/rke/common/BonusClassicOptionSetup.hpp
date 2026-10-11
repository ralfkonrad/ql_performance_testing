// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BONUSCLASSICOPTIONSETUP_HPP
#define BONUSCLASSICOPTIONSETUP_HPP

#include <rke/common/MarketData.hpp>
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/BarrierMonitoring.hpp>
#include <ql/instrument.hpp>
#include <ql/pricingengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/time/date.hpp>
#include <ql/time/period.hpp>
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

    // MonteCarlo is MCBonusClassicEngine under LowDiscrepancy on monitoringStepsPerYear, in
    // either monitoring mode and on every market, and the only engine that reads pathGeneration
    // and samples. FiniteDifference is FdBlackScholesBonusClassicEngine on fdTimeGrid x
    // fdSpaceGrid, monitored continuously or on the Monte-Carlo grid, under the local
    // volatility on the smiles; on the bilinear smile its price carries the defect the engine's
    // header warns about, a strike node of the surface on the barrier. Binomial is
    // BinomialBonusClassicEngine<QuantLib::CoxRossRubinstein> on treeTimeSteps with Boyle-Lau,
    // monitoring on every step, so discretely only; it flattens the volatility, so it prices
    // the flat market only. Analytic is AnalyticBonusClassicEngine, the closed form of the
    // continuously monitored certificate under a constant volatility, so it prices the flat
    // market under continuous monitoring only.
    enum class Engine : std::uint8_t { MonteCarlo, FiniteDifference, Binomial, Analytic };

    // Whether FiniteDifference, Binomial and Analytic report the barrier-hit and bonus
    // probabilities beside the price; MonteCarlo reports them always.
    enum class ProbabilityCalculation : std::uint8_t { Off, On };

    struct OptionData {
        QuantLib::Real barrier = 70.0;
        QuantLib::Real bonusLevel = 120.00;
        // The low end of a bonus certificate's usual one to two years; the tests price 5M.
        QuantLib::Period ttm = QuantLib::Period(1, QuantLib::Years);
    };

    // The certificate on data's levels, exercisable at today + data.ttm, with no engine set.
    [[nodiscard]] QuantLib::ext::shared_ptr<RKE::QL::Ext::BonusClassicOption>
    makeBonusClassicOption(const OptionData& data, QuantLib::Date today);

    // A production run's path count: a power of two, where a Sobol sequence is balanced.
    // SobolRsg skips the zero point, so these are points 1 to 2^16, the net with one point
    // swapped.
    inline constexpr QuantLib::Size productionSamples = QuantLib::Size{1} << 16U;

    // The Monte-Carlo monitoring grid, and the finite-difference engine's under discrete
    // monitoring: about one step per business day, 255 over the Actual360 year fraction
    // 365/360, though not on the business days themselves.
    inline constexpr QuantLib::Size monitoringStepsPerYear = 252;

    // The FD grid QuantLib's own barrier tests price Haug's table on.
    inline constexpr QuantLib::Size fdTimeGrid = 200;
    inline constexpr QuantLib::Size fdSpaceGrid = 400;

    // The step count QuantLib's own barrier tests price Haug's table with, before Boyle-Lau.
    inline constexpr QuantLib::Size treeTimeSteps = 400;

    // Sets the evaluation date to 22 Jun 2025 and prices the certificate on optionData's levels
    // with the engine on the market, the barrier monitored as monitoring says: flatMarketData on
    // Market::Flat, smileMarketData on the smiles. pathGeneration and samples are read by
    // MonteCarlo only; a smoke test passes fewer samples than productionSamples, on the same time
    // grid. The benchmarks and profiles pass ProbabilityCalculation::Off, so they keep measuring
    // the price alone. QL_REQUIRE rejects Binomial and Analytic off the flat market, Binomial under
    // continuous monitoring and Analytic under discrete.
    [[nodiscard]] BonusClassicOptionSetup
    makeBonusClassicOptionSetup(const OptionData& optionData,
                                const FlatMarketData& flatMarketData,
                                const SmileMarketData& smileMarketData,
                                RKE::QL::Ext::BarrierMonitoring monitoring,
                                PathGeneration pathGeneration,
                                Market market,
                                QuantLib::Size samples,
                                Engine engine,
                                ProbabilityCalculation probabilityCalculation);

    // The steps of the engine's monitoring grid: the finite-difference engine's discrete grid,
    // or the lattice after Boyle-Lau. Null<Size>() where there is nothing to report, i.e. under
    // continuous monitoring, the analytic engine's only mode, and for the Monte-Carlo engine,
    // whose grid is monitoringStepsPerYear by construction. The engines build the grid from the
    // instrument's arguments, so the setup has to have priced once.
    [[nodiscard]] QuantLib::Size monitoringSteps(const BonusClassicOptionSetup& setup);

    // The measured work. recalculate() is the point: NPV() alone returns the cached value, so a
    // loop would time one pricing and the rest cache reads.
    [[nodiscard]] inline QuantLib::Real reprice(QuantLib::Instrument& instrument) {
        instrument.recalculate();
        return instrument.NPV();
    }
}

#endif // BONUSCLASSICOPTIONSETUP_HPP
