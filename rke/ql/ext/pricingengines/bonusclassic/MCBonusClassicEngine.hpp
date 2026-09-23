//
// Created by ralf.eckel on 23.06.2025.
//

#ifndef MCBONUSCLASSICENGINE_HPP
#define MCBONUSCLASSICENGINE_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <ql/pricingengines/barrier/mcbarrierengine.hpp>
#include <ql/pricingengines/mcsimulation.hpp>
#include <ql/processes/blackscholesprocess.hpp>

namespace RKE::QL::External {
    template <class RNG = QuantLib::PseudoRandom, class S = QuantLib::Statistics>
    class MCBonusClassicEngine : public BonusClassicOption::engine,
                                 public QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S> {
      public:
        using path_generator_type = typename QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S>::path_generator_type;
        using path_pricer_type = typename QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S>::path_pricer_type;
        using stats_type = typename QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S>::stats_type;

        MCBonusClassicEngine(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::Size timeStepsPerYear,
            QuantLib::Size requiredSamples,
            QuantLib::Size maxSamples,
            QuantLib::Real requiredTolerance,
            bool isBiased,
            bool brownianBridge,
            QuantLib::BigNatural seed);

        void calculate() const override;
        QuantLib::ext::shared_ptr<path_pricer_type> pathPricer() const override;
        // Public so that a caller can read the grid the price was sampled on instead of
        // rebuilding it; valid once the instrument has passed its arguments to the engine.
        QuantLib::TimeGrid timeGrid() const override;

      private:
        // McSimulation implementation
        QuantLib::ext::shared_ptr<path_generator_type> pathGenerator() const override {
            QuantLib::TimeGrid const grid = timeGrid();
            typename RNG::rsg_type const gen = RNG::make_sequence_generator(grid.size() - 1, seed_);
            return QuantLib::ext::make_shared<path_generator_type>(process_, grid, gen,
                                                                   brownianBridge_);
        }

        // data members
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        QuantLib::Size timeStepsPerYear_;
        QuantLib::Size requiredSamples_;
        QuantLib::Size maxSamples_;
        QuantLib::Real requiredTolerance_;
        bool brownianBridge_;
        QuantLib::BigNatural seed_;
    };

    class BiasedBonusClassicPathPricer : public QuantLib::PathPricer<QuantLib::Path> {
      public:
        BiasedBonusClassicPathPricer(BonusClassicPayoff payoff,
                                     QuantLib::DiscountFactor discountFactor);
        QuantLib::Real operator()(const QuantLib::Path& path) const override;

      private:
        BonusClassicPayoff payoff_;
        QuantLib::DiscountFactor discountFactor_;
    };


    // template definitions
    template <class RNG, class S>
    MCBonusClassicEngine<RNG, S>::MCBonusClassicEngine(
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
        QuantLib::Size timeStepsPerYear,
        QuantLib::Size requiredSamples,
        QuantLib::Size maxSamples,
        QuantLib::Real requiredTolerance,
        bool isBiased,
        bool brownianBridge,
        QuantLib::BigNatural seed)
    : QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S>(false, false),
      process_(std::move(process)), timeStepsPerYear_(timeStepsPerYear),
      requiredSamples_(requiredSamples), maxSamples_(maxSamples),
      requiredTolerance_(requiredTolerance), brownianBridge_(brownianBridge), seed_(seed) {
        QL_REQUIRE(isBiased, "only biased path pricer are supported");
        registerWith(process_);
    }

    template <class RNG, class S>
    void MCBonusClassicEngine<RNG, S>::calculate() const {
        auto spot = process_->x0();
        QL_REQUIRE(spot > 0.0, "negative or null underlying given");
        QL_REQUIRE(!triggered(spot), "barrier touched");
        QuantLib::McSimulation<QuantLib::SingleVariate, RNG, S>::calculate(
            requiredTolerance_, requiredSamples_, maxSamples_);
        results_.value = this->mcModel_->sampleAccumulator().mean();
        if constexpr (RNG::allowsErrorEstimate) {
            results_.errorEstimate = this->mcModel_->sampleAccumulator().errorEstimate();
}
    }

    template <class RNG, class S>
    QuantLib::ext::shared_ptr<typename MCBonusClassicEngine<RNG, S>::path_pricer_type>
    MCBonusClassicEngine<RNG, S>::pathPricer() const {
        auto payoff = QuantLib::ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-plain payoff given");

        auto grid = timeGrid();
        auto discountFactor = process_->riskFreeRate()->discount(grid.back());

        // From the payoff, not from arguments_: the two carry the same barrier and bonus
        // level, and only this way does the object checked above price the paths.
        return QuantLib::ext::shared_ptr<path_pricer_type>(
            new BiasedBonusClassicPathPricer(*payoff, discountFactor));
    }


    template <class RNG, class S>
    QuantLib::TimeGrid MCBonusClassicEngine<RNG, S>::timeGrid() const {
        auto residualTime = process_->time(arguments_.exercise->lastDate());
        if (timeStepsPerYear_ != QuantLib::Null<QuantLib::Size>()) {
            auto steps = static_cast<QuantLib::Size>(timeStepsPerYear_ * residualTime);
            // QuantLib::TimeGrid has an initializer_list<Time> constructor, which a braced
            // return selects over TimeGrid(Time, Size), narrowing steps to a Time.
            return QuantLib::TimeGrid( // NOLINT(modernize-return-braced-init-list)
                residualTime, std::max<QuantLib::Size>(steps, 1));
        }
        QL_FAIL("time steps not specified");
    }
}
#endif // MCBONUSCLASSICENGINE_HPP
