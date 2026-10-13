// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BarrierOptionSetup.hpp"
#include "LowDiscrepancyJoeKuoD7.hpp"
#include "MarketData.hpp"
#include <ql/exercise.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/pricingengines/barrier/mcbarrierengine.hpp>
#include <ql/settings.hpp>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::Common {
    ext::shared_ptr<BarrierOption> makeBarrierOption(const BarrierOptionData& data, Date today) {
        return ext::make_shared<BarrierOption>(
            Barrier::DownOut, data.barrier, data.rebate,
            ext::make_shared<PlainVanillaPayoff>(Option::Put, data.strike),
            ext::make_shared<EuropeanExercise>(today + data.ttm));
    }

    BarrierOptionSetup makeBarrierOptionSetup(const BarrierOptionData& data,
                                              const FlatMarketData& flatMarketData,
                                              const SmileMarketData& smileMarketData,
                                              BarrierMonitoring monitoring,
                                              Market market,
                                              Size samples) {
        const auto today = evaluationDate();
        Settings::instance().evaluationDate() = today;

        const auto process = market == Market::Flat ?
                                 flatMarketData.makeGeneralizedBlackScholesProcess(today) :
                                 smileMarketData.makeGeneralizedBlackScholesProcess(
                                     today, market == Market::SmileBicubic);

        // timeSteps is Null<Size>() so that timeStepsPerYear applies; the engine truncates it
        // times the year fraction, as BonusClassicOption::engine::monitoringGrid does. The
        // Null<Real>() tolerance is mandatory, not a default: with no error estimate under
        // LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and maxSamples
        // never applies. maxSamples is McSimulation's own no-bound default; samples + 1 would
        // wrap for the largest count the --samples options admit.
        const auto engine = ext::make_shared<MCBarrierEngine<LowDiscrepancyJoeKuoD7>>(
            process, Null<Size>(), monitoringStepsPerYear, true, false, samples, Null<Real>(),
            QL_MAX_INTEGER, monitoring == BarrierMonitoring::Discrete, 42);

        const auto option = makeBarrierOption(data, today);
        option->setPricingEngine(engine);

        return {process, engine, option};
    }
}
