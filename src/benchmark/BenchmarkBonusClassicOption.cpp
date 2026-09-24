//
// Created by ralf.eckel on 25.06.2025.
//

#include "BenchmarkBonusClassicOption.hpp"
#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/MCBonusClassicEngine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual360.hpp>

using namespace QuantLib;

namespace RKE::QL::External {
    namespace {
        ext::shared_ptr<YieldTermStructure>
        flatRate(const Date& today, const ext::shared_ptr<Quote>& forward, const DayCounter& dc) {
            return ext::make_shared<FlatForward>(today, Handle<Quote>(forward), dc);
        }

        ext::shared_ptr<BlackVolTermStructure>
        flatVol(const Date& today, const ext::shared_ptr<Quote>& vol, const DayCounter& dc) {
            return ext::make_shared<BlackConstantVol>(today, NullCalendar(), Handle<Quote>(vol),
                                                      dc);
        }

        struct OptionData {
            Real barrier = 90.0;
            Real bonusLevel = 120.00;
            Period ttm = Period(5, Months);
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

    void BM_BonusClassicOption(benchmark::State& state) {
        const auto option_data = OptionData();
        auto market_data = MarketData();

        const auto today = Date(22, Jun, 2025);
        Settings::instance().evaluationDate() = today;

        const auto exerciseDate = today + option_data.ttm;

        const auto process = market_data.makeGeneralizedBlackScholesProcess(today);
        const auto mcEngine = ext::make_shared<MCBonusClassicEngine<LowDiscrepancy>>(
            process, 100, 50'000, 50'001, Null<Real>(), true, true, 42);

        const auto bonusClassicOption = ext::make_shared<BonusClassicOption>(
            option_data.barrier, option_data.bonusLevel, exerciseDate);

        bonusClassicOption->setPricingEngine(mcEngine);

        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            bonusClassicOption->recalculate();
            auto npv = bonusClassicOption->NPV();
            benchmark::DoNotOptimize(npv);
        }
    }
}