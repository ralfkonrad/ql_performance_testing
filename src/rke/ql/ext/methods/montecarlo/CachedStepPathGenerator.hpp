// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef CACHEDSTEPPATHGENERATOR_HPP
#define CACHEDSTEPPATHGENERATOR_HPP

#include <rke/ql/ext/methods/montecarlo/BlackScholesStepCache.hpp>
#include <rke/ql/ext/methods/montecarlo/LocalVolStepCache.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/methods/montecarlo/brownianbridge.hpp>
#include <ql/methods/montecarlo/path.hpp>
#include <ql/methods/montecarlo/pathpricer.hpp>
#include <ql/methods/montecarlo/sample.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/timegrid.hpp>
#include <utility>
#include <vector>

namespace RKE::QL::Ext {
    //! Path generator taking the Black-Scholes step from a step cache
    /*! A drop-in for QuantLib::PathGenerator over a
        QuantLib::GeneralizedBlackScholesProcess whose step on the grid a
        StepCache, built once, reproduces: BlackScholesStepCache for the exact
        lognormal step, LocalVolStepCache for the Euler step through the local
        volatility. The constructor refuses any other process; use
        QuantLib::PathGenerator for it, i.e. QuantLib::SingleVariate. The paths
        are the same doubles QuantLib::PathGenerator produces from the same
        sequence.
    */
    template <class GSG, class StepCache = BlackScholesStepCache>
    class CachedStepPathGenerator {
      public:
        using sample_type = QuantLib::Sample<QuantLib::Path>;

        /*! \pre the StepCache reproduces the process's evolve() on \p timeGrid,
                 see BlackScholesStepCache::isExact() and LocalVolStepCache::isExact(). */
        CachedStepPathGenerator(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::TimeGrid timeGrid,
            GSG generator,
            bool brownianBridge);

        [[nodiscard]] const sample_type& next() const { return next(false); }
        [[nodiscard]] const sample_type& antithetic() const { return next(true); }
        [[nodiscard]] QuantLib::Size size() const { return dimension_; }
        [[nodiscard]] const QuantLib::TimeGrid& timeGrid() const { return timeGrid_; }

      private:
        const sample_type& next(bool antithetic) const;

        bool brownianBridge_;
        GSG generator_;
        QuantLib::Size dimension_;
        QuantLib::TimeGrid timeGrid_;
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        StepCache cache_;
        mutable sample_type next_;
        mutable std::vector<QuantLib::Real> temp_;
        QuantLib::BrownianBridge bb_;
    };

    //! Monte Carlo traits that pair QuantLib::SingleVariate with CachedStepPathGenerator
    template <class RNG = QuantLib::PseudoRandom>
    struct CachedStepSingleVariate {
        using rng_traits = RNG;
        using path_type = QuantLib::Path;
        using path_pricer_type = QuantLib::PathPricer<path_type>;
        using rsg_type = typename RNG::rsg_type;
        using path_generator_type = CachedStepPathGenerator<rsg_type>;
        static constexpr bool allowsErrorEstimate = RNG::allowsErrorEstimate != 0;
    };

    //! Monte Carlo traits that pair QuantLib::SingleVariate with a LocalVolStepCache
    template <class RNG = QuantLib::PseudoRandom>
    struct LocalVolStepSingleVariate {
        using rng_traits = RNG;
        using path_type = QuantLib::Path;
        using path_pricer_type = QuantLib::PathPricer<path_type>;
        using rsg_type = typename RNG::rsg_type;
        using path_generator_type = CachedStepPathGenerator<rsg_type, LocalVolStepCache>;
        static constexpr bool allowsErrorEstimate = RNG::allowsErrorEstimate != 0;
    };


    template <class GSG, class StepCache>
    CachedStepPathGenerator<GSG, StepCache>::CachedStepPathGenerator(
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
        QuantLib::TimeGrid timeGrid,
        GSG generator,
        bool brownianBridge)
    : brownianBridge_(brownianBridge), generator_(std::move(generator)),
      dimension_(generator_.dimension()), timeGrid_(std::move(timeGrid)),
      process_(std::move(process)), cache_(process_, timeGrid_),
      next_(QuantLib::Path(timeGrid_), 1.0), temp_(dimension_), bb_(timeGrid_) {
        QL_REQUIRE(dimension_ == timeGrid_.size() - 1, "sequence generator dimensionality ("
                                                           << dimension_ << ") != timeSteps ("
                                                           << timeGrid_.size() - 1 << ")");
        QL_REQUIRE(cache_.isExact(), "the step cache does not reproduce the process's step on "
                                     "this grid; use QuantLib::PathGenerator");
    }

    template <class GSG, class StepCache>
    const typename CachedStepPathGenerator<GSG, StepCache>::sample_type&
    CachedStepPathGenerator<GSG, StepCache>::next(bool antithetic) const {
        // The body of QuantLib::PathGenerator::next(bool), with the step taken from the cache.
        const auto& sequence = antithetic ? generator_.lastSequence() : generator_.nextSequence();

        if (brownianBridge_) {
            bb_.transform(sequence.value.begin(), sequence.value.end(), temp_.begin());
        }
        const auto& increments = brownianBridge_ ? temp_ : sequence.value;

        next_.weight = sequence.weight;

        QuantLib::Path& path = next_.value;
        path.front() = process_->x0();
        for (QuantLib::Size i = 1; i < path.length(); i++) {
            path[i] = cache_.evolve(i - 1, path[i - 1],
                                    antithetic ? -increments[i - 1] : increments[i - 1]);
        }

        return next_;
    }
}

#endif // CACHEDSTEPPATHGENERATOR_HPP
