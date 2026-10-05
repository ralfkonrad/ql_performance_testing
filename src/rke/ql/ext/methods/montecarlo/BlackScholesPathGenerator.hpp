// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BLACKSCHOLESPATHGENERATOR_HPP
#define BLACKSCHOLESPATHGENERATOR_HPP

#include <rke/ql/ext/methods/montecarlo/BlackScholesStepCache.hpp>
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
    //! Path generator for a QuantLib::GeneralizedBlackScholesProcess
    /*! A drop-in for QuantLib::PathGenerator that takes the exact step from a
        BlackScholesStepCache built once for its grid, and calls the process's
        evolve() only where the cache is not exact. The paths are the same
        doubles QuantLib::PathGenerator produces from the same sequence.
    */
    template <class GSG>
    class BlackScholesPathGenerator {
      public:
        using sample_type = QuantLib::Sample<QuantLib::Path>;

        BlackScholesPathGenerator(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::TimeGrid timeGrid,
            GSG generator,
            bool brownianBridge);

        [[nodiscard]] const sample_type& next() const { return next(false); }
        [[nodiscard]] const sample_type& antithetic() const { return next(true); }
        [[nodiscard]] QuantLib::Size size() const { return dimension_; }
        [[nodiscard]] const QuantLib::TimeGrid& timeGrid() const { return timeGrid_; }
        //! whether paths are built from the cached exact step
        [[nodiscard]] bool isExact() const { return cache_.isExact(); }

      private:
        const sample_type& next(bool antithetic) const;

        bool brownianBridge_;
        GSG generator_;
        QuantLib::Size dimension_;
        QuantLib::TimeGrid timeGrid_;
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        BlackScholesStepCache cache_;
        mutable sample_type next_;
        mutable std::vector<QuantLib::Real> temp_;
        QuantLib::BrownianBridge bb_;
    };

    //! Monte Carlo traits that pair QuantLib::SingleVariate with BlackScholesPathGenerator
    template <class RNG = QuantLib::PseudoRandom>
    struct BlackScholesSingleVariate {
        using rng_traits = RNG;
        using path_type = QuantLib::Path;
        using path_pricer_type = QuantLib::PathPricer<path_type>;
        using rsg_type = typename RNG::rsg_type;
        using path_generator_type = BlackScholesPathGenerator<rsg_type>;
        static constexpr bool allowsErrorEstimate = RNG::allowsErrorEstimate != 0;
    };


    template <class GSG>
    BlackScholesPathGenerator<GSG>::BlackScholesPathGenerator(
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
    }

    template <class GSG>
    const typename BlackScholesPathGenerator<GSG>::sample_type&
    BlackScholesPathGenerator<GSG>::next(bool antithetic) const {
        // The body of QuantLib::PathGenerator::next(bool), with the step taken from the cache.
        const auto& sequence = antithetic ? generator_.lastSequence() : generator_.nextSequence();

        if (brownianBridge_) {
            bb_.transform(sequence.value.begin(), sequence.value.end(), temp_.begin());
        }
        const auto& increments = brownianBridge_ ? temp_ : sequence.value;

        next_.weight = sequence.weight;

        QuantLib::Path& path = next_.value;
        path.front() = process_->x0();

        if (cache_.isExact()) {
            for (QuantLib::Size i = 1; i < path.length(); i++) {
                path[i] = cache_.evolve(i - 1, path[i - 1],
                                        antithetic ? -increments[i - 1] : increments[i - 1]);
            }
        } else {
            for (QuantLib::Size i = 1; i < path.length(); i++) {
                path[i] = process_->evolve(timeGrid_[i - 1], path[i - 1], timeGrid_.dt(i - 1),
                                           antithetic ? -increments[i - 1] : increments[i - 1]);
            }
        }

        return next_;
    }
}

#endif // BLACKSCHOLESPATHGENERATOR_HPP
