// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BonusClassicOptionSetup.hpp"
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/math/randomnumbers/sobolrsg.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>

using namespace RKE::QL::Ext;
using namespace QuantLib;

namespace RKE::Common {
    namespace {
        // Duplicates test-suite/utilities.hpp, which compiles into the test target only; the
        // market and its conventions are the tests', the maturity and the simulation are not.
        ext::shared_ptr<YieldTermStructure>
        flatRate(const Date& today, const ext::shared_ptr<Quote>& forward, const DayCounter& dc) {
            return ext::make_shared<FlatForward>(today, Handle<Quote>(forward), dc);
        }

        ext::shared_ptr<BlackVolTermStructure>
        flatVol(const Date& today, const ext::shared_ptr<Quote>& vol, const DayCounter& dc) {
            return ext::make_shared<BlackConstantVol>(today, NullCalendar(), Handle<Quote>(vol),
                                                      dc);
        }

        // LowDiscrepancy with Joe-Kuo D7 direction integers, tabulated up to dimension 1898,
        // instead of SobolRsg's default Jaeckel ones, tabulated up to 32 and drawn from the seed
        // beyond. One dimension per time step, so a daily grid needs the former.
        struct LowDiscrepancyJoeKuoD7 : LowDiscrepancy {
            static rsg_type make_sequence_generator(Size dimension, BigNatural seed) {
                return rsg_type(SobolRsg(dimension, seed, SobolRsg::JoeKuoD7));
            }
        };

        struct OptionData {
            Real barrier = 90.0;
            Real bonusLevel = 120.00;
            // The low end of a bonus certificate's usual one to two years; the tests price 5M.
            Period ttm = Period(1, Years);
        };

        struct MarketData {
            Real spot = 100.00;
            Real riskfreeRate = 0.01;
            Real dividendYield = 0.03;
            Real volatility = 0.20;

            ext::shared_ptr<GeneralizedBlackScholesProcess>
            makeGeneralizedBlackScholesProcess(Date today) {
                const auto dc = Actual360();
                const auto spotQuote = ext::make_shared<SimpleQuote>(spot);

                const auto qH_SME = ext::make_shared<SimpleQuote>(dividendYield);
                const auto qTS = flatRate(today, qH_SME, dc);

                const auto rH_SME = ext::make_shared<SimpleQuote>(riskfreeRate);
                const auto rTS = flatRate(today, rH_SME, dc);

                const auto volaQuote = ext::make_shared<SimpleQuote>(volatility);
                const auto volTS = flatVol(today, volaQuote, dc);

                return ext::make_shared<BlackScholesMertonProcess>(
                    Handle<Quote>(spotQuote), Handle(qTS), Handle(rTS), Handle(volTS));
            }
        };
    }

    BonusClassicOptionSetup makeBonusClassicOptionSetup(bool isBiased) {
        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        // The Null<Real>() tolerance is mandatory, not a default: with no error estimate under
        // LowDiscrepancy, McSimulation::calculate takes the fixed-sample branch and maxSamples
        // never applies. Sized like a production run: about one step per business day, 255
        // over the Actual360 year fraction 365/360, though not on the business days themselves,
        // and a power of two of paths, where a Sobol sequence is balanced. SobolRsg skips the
        // zero point, so these are points 1 to 2^16, the net with one point swapped.
        constexpr Size timeStepsPerYear = 252;
        constexpr Size samples = Size{1} << 16U;
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancyJoeKuoD7>>(
            process, timeStepsPerYear, samples, samples + 1, Null<Real>(), isBiased, true, 42);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        bonusClassicOption->setPricingEngine(mcEngine);

        return {process, mcEngine, bonusClassicOption};
    }
}
