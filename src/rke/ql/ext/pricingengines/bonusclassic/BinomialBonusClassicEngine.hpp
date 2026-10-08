// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BINOMIALBONUSCLASSICENGINE_HPP
#define BINOMIALBONUSCLASSICENGINE_HPP

#include <rke/ql/ext/instruments/BonusClassicOption.hpp>
#include <rke/ql/ext/pricingengines/bonusclassic/DiscretizedBonusClassicOption.hpp>
#include <ql/exercise.hpp>
#include <ql/math/array.hpp>
#include <ql/math/comparison.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
#include <ql/methods/lattices/bsmlattice.hpp>
#include <ql/pricingengines/greeks.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/timegrid.hpp>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace RKE::QL::Ext {
    //! Binomial-tree engine for bonus certificates
    /*! Rolls DiscretizedBonusClassicOption back on a recombining binomial
        tree of type \p Tree. The tree is built on a
        QuantLib::GeneralizedBlackScholesProcess whose market is flattened
        as QuantLib::BinomialBarrierEngine flattens it: the risk-free rate
        \f$ r \f$ and the dividend yield \f$ q \f$ are the continuously
        compounded zero rates at the exercise date, each in its own curve's
        day counter, and the volatility is the Black volatility at the
        exercise date and the spot \f$ S_0 \f$.
        QuantLib::BlackScholesLattice discounts at \f$ r \f$ per step. The
        asset knocks the certificate out to the asset leg
        \f$ S \, e^{-q (T - t)} \f$ on every lattice time where
        \f$ S \le H \f$, and pays BonusClassicPayoff at maturity \f$ T \f$.

        For QuantLib::CoxRossRubinstein and trees derived from it the step
        count is raised after Boyle and Lau to the first
        \f$ \lfloor i^2 \sigma^2 T / \ln^2(S_0 / H) \rfloor \f$ above
        timeSteps, capped at maxTimeSteps, which puts a layer of nodes at or
        just below the barrier. QuantLib::LeisenReimer and QuantLib::Joshi4
        build an odd number of steps only, so an even count is rounded up.

        Delta and gamma come from the nodes of the first and second lattice
        step, as in QuantLib::BinomialVanillaEngine; theta from the
        Black-Scholes PDE through QuantLib::blackScholesTheta on the
        flattened process, the model the tree prices.

        Time is the risk-free curve's day counter from its reference date.

        See P. Boyle and S.H. Lau, <i>Bumping up against the barrier with
        the binomial method</i>, Journal of Derivatives, 1994.

        \tparam Tree the lattice type, as in QuantLib::BinomialBarrierEngine.

        \warning the barrier is monitored on every lattice step, and the
                 step count is the engine's own, not MCBonusClassicEngine's
                 grid; timeGrid() reports it.
        \warning Boyle-Lau raises the step count up to maxTimeSteps, by
                 default max(1000, 5 * timeSteps), and an even count is
                 rounded up for QuantLib::LeisenReimer and QuantLib::Joshi4.
                 A benchmark against another engine passes
                 maxTimeSteps = timeSteps or reads timeGrid().
        \warning the dividend curve is flattened, so under a sloped curve the
                 asset leg disagrees with MCBonusClassicEngine and
                 FdBlackScholesBonusClassicEngine.
        \warning the volatility is read at the spot, not at the bonus level
                 as FdBlackScholesBonusClassicEngine reads it. Neither local
                 volatility nor discrete dividends are supported.
        \warning delta, gamma and theta are Null<Real>() when the tree's up
                 and down factors round to the same double.

        \ingroup barrierengines

        \test the value is regression-locked for QuantLib::CoxRossRubinstein
              at 400 steps, 442 after Boyle-Lau.
        \test the value is checked against the asset plus a down-and-out put
              struck at the bonus level, priced with
              QuantLib::AnalyticBarrierEngine.
        \test the Boyle-Lau count is checked to price closer to that
              replication than the uncorrected one.
    */
    template <class Tree>
    class BinomialBonusClassicEngine : public BonusClassicOption::engine {
      public:
        /*! \param process      the underlying; the engine registers with it
            \param timeSteps    lattice steps before Boyle-Lau, at least 2
            \param maxTimeSteps cap on the Boyle-Lau count; 0 means
                                max(1000, 5 * timeSteps), and timeSteps
                                disables Boyle-Lau
            \pre \p process is not null; QL_REQUIRE checks this.
        */
        BinomialBonusClassicEngine(
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
            QuantLib::Size timeSteps,
            QuantLib::Size maxTimeSteps = 0);

        void calculate() const override;

        //! the lattice's grid, one monitoring time per step, after Boyle-Lau and odd rounding
        /*! \pre the instrument has set up the arguments, i.e. NPV() has run once;
                 QL_REQUIRE checks this. */
        [[nodiscard]] QuantLib::TimeGrid timeGrid() const;

      private:
        //! the process with rate, yield and volatility flattened at maturity
        struct FlatMarket {
            QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process;
            QuantLib::Rate r = 0.0;
            QuantLib::Rate q = 0.0;
            QuantLib::Volatility v = 0.0;
            QuantLib::Time maturity = 0.0;
        };

        [[nodiscard]] QuantLib::ext::shared_ptr<BonusClassicPayoff> checkedPayoff() const;
        [[nodiscard]] FlatMarket flatten() const;
        [[nodiscard]] QuantLib::Size effectiveTimeSteps(const FlatMarket& flat) const;
        static QuantLib::Size resolvedMaxTimeSteps(QuantLib::Size timeSteps,
                                                   QuantLib::Size maxTimeSteps);
        static QuantLib::Size boyleLauSteps(QuantLib::Size timeSteps,
                                            QuantLib::Size maxTimeSteps,
                                            QuantLib::Real s0,
                                            QuantLib::Real barrier,
                                            QuantLib::Volatility v,
                                            QuantLib::Time maturity);

        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process_;
        QuantLib::Size timeSteps_;
        QuantLib::Size maxTimeSteps_;
    };


    template <class Tree>
    BinomialBonusClassicEngine<Tree>::BinomialBonusClassicEngine(
        QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess> process,
        QuantLib::Size timeSteps,
        QuantLib::Size maxTimeSteps)
    : process_(std::move(process)), timeSteps_(timeSteps),
      maxTimeSteps_(resolvedMaxTimeSteps(timeSteps, maxTimeSteps)) {
        QL_REQUIRE(process_, "null process given");
        // Without this, NPV() keeps returning the first price.
        registerWith(process_);
    }

    template <class Tree>
    void BinomialBonusClassicEngine<Tree>::calculate() const {
        const auto payoff = checkedPayoff();
        const auto flat = flatten();
        const auto steps = effectiveTimeSteps(flat);
        const QuantLib::TimeGrid grid(flat.maturity, steps);

        // The bonus level is the strike LeisenReimer and Joshi4 centre their nodes on.
        const auto tree = QuantLib::ext::make_shared<Tree>(flat.process, flat.maturity, steps,
                                                           payoff->bonusLevel());
        const auto lattice = QuantLib::ext::make_shared<QuantLib::BlackScholesLattice<Tree>>(
            tree, flat.r, flat.maturity, steps);

        DiscretizedBonusClassicOption option(arguments_, flat.q, flat.maturity);
        option.initialize(lattice, flat.maturity);

        // Delta and gamma from the nodes of the first and second step, as in
        // QuantLib::BinomialVanillaEngine.
        option.rollback(grid[2]);
        const QuantLib::Array va2(option.values());
        QL_ENSURE(va2.size() == 3, "Expect 3 nodes in grid at second step");
        const auto s2u = lattice->underlying(2, 2);
        const auto s2m = lattice->underlying(2, 1);
        const auto s2d = lattice->underlying(2, 0);

        // A tree whose up and down factors round to the same double puts every node of a step at
        // one price, so these differences are zero. The value is kept, the Greeks are absent.
        QuantLib::Real gamma = QuantLib::Null<QuantLib::Real>();
        if (s2u > s2m && s2m > s2d) {
            const auto delta2u = (va2[2] - va2[1]) / (s2u - s2m);
            const auto delta2d = (va2[1] - va2[0]) / (s2m - s2d);
            gamma = (delta2u - delta2d) / ((s2u - s2d) / 2.0);
        }

        option.rollback(grid[1]);
        const QuantLib::Array va1(option.values());
        QL_ENSURE(va1.size() == 2, "Expect 2 nodes in grid at first step");
        const auto s1u = lattice->underlying(1, 1);
        const auto s1d = lattice->underlying(1, 0);
        const QuantLib::Real delta =
            (s1u > s1d) ? (va1[1] - va1[0]) / (s1u - s1d) : QuantLib::Null<QuantLib::Real>();

        option.rollback(0.0);
        results_.value = option.presentValue();
        results_.delta = delta;
        results_.gamma = gamma;
        // The PDE holds at the spot, which lies above the barrier, on the flattened process: the
        // model the tree prices, whose dividend yield the asset leg already takes.
        results_.theta =
            (delta == QuantLib::Null<QuantLib::Real>() ||
             gamma == QuantLib::Null<QuantLib::Real>()) ?
                QuantLib::Null<QuantLib::Real>() :
                QuantLib::blackScholesTheta(flat.process, results_.value, delta, gamma);
    }

    template <class Tree>
    QuantLib::TimeGrid BinomialBonusClassicEngine<Tree>::timeGrid() const {
        // Empty until the instrument's setupArguments() has run, i.e. before the first NPV().
        QL_REQUIRE(arguments_.exercise, "no exercise given");
        const auto flat = flatten();
        // QuantLib::TimeGrid has an initializer_list<Time> constructor, which a braced
        // return selects over TimeGrid(Time, Size), narrowing steps to a Time.
        return QuantLib::TimeGrid( // NOLINT(modernize-return-braced-init-list)
            flat.maturity, effectiveTimeSteps(flat));
    }

    template <class Tree>
    QuantLib::ext::shared_ptr<BonusClassicPayoff>
    BinomialBonusClassicEngine<Tree>::checkedPayoff() const {
        const auto payoff =
            QuantLib::ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-bonus-classic payoff given");
        QL_REQUIRE(arguments_.exercise->type() == QuantLib::Exercise::European,
                   "only european style option are supported");

        const auto s0 = process_->x0();
        QL_REQUIRE(s0 > 0.0, "negative or null underlying given");
        QL_REQUIRE(!triggered(s0), "barrier touched");
        return payoff;
    }

    template <class Tree>
    typename BinomialBonusClassicEngine<Tree>::FlatMarket
    BinomialBonusClassicEngine<Tree>::flatten() const {
        const auto& riskFree = process_->riskFreeRate();
        const auto& dividends = process_->dividendYield();
        const auto& volatility = process_->blackVolatility();
        const auto rfdc = riskFree->dayCounter();
        const auto divdc = dividends->dayCounter();

        const auto maturityDate = arguments_.exercise->lastDate();
        const auto referenceDate = riskFree->referenceDate();
        // QuantLib::BinomialBarrierEngine's choices: the date overload, read at the spot.
        const auto v = volatility->blackVol(maturityDate, process_->x0());
        const auto r =
            riskFree->zeroRate(maturityDate, rfdc, QuantLib::Continuous, QuantLib::NoFrequency)
                .rate();
        const auto q =
            dividends->zeroRate(maturityDate, divdc, QuantLib::Continuous, QuantLib::NoFrequency)
                .rate();

        const QuantLib::Handle<QuantLib::YieldTermStructure> flatRiskFree(
            QuantLib::ext::make_shared<QuantLib::FlatForward>(referenceDate, r, rfdc));
        const QuantLib::Handle<QuantLib::YieldTermStructure> flatDividends(
            QuantLib::ext::make_shared<QuantLib::FlatForward>(referenceDate, q, divdc));
        const QuantLib::Handle<QuantLib::BlackVolTermStructure> flatVolatility(
            QuantLib::ext::make_shared<QuantLib::BlackConstantVol>(
                referenceDate, volatility->calendar(), v, volatility->dayCounter()));

        return {
            QuantLib::ext::make_shared<QuantLib::GeneralizedBlackScholesProcess>(
                process_->stateVariable(), flatDividends, flatRiskFree, flatVolatility),
            r,
            q,
            v,
            // The same number as process_->time(maturityDate).
            rfdc.yearFraction(referenceDate, maturityDate),
        };
    }

    template <class Tree>
    QuantLib::Size
    BinomialBonusClassicEngine<Tree>::effectiveTimeSteps(const FlatMarket& flat) const {
        if constexpr (std::is_base_of_v<QuantLib::CoxRossRubinstein, Tree>) {
            return (maxTimeSteps_ > timeSteps_) ?
                       boyleLauSteps(timeSteps_, maxTimeSteps_, process_->x0(), arguments_.barrier,
                                     flat.v, flat.maturity) :
                       timeSteps_;
        } else if constexpr (std::is_base_of_v<QuantLib::LeisenReimer, Tree> ||
                             std::is_base_of_v<QuantLib::Joshi4, Tree>) {
            // Both trees build steps + 1 for an even count, while BlackScholesLattice and its
            // TimeGrid would keep it: a dt mismatch. Rounded up before anything is built.
            return (timeSteps_ % 2 == 0) ? timeSteps_ + 1 : timeSteps_;
        } else {
            return timeSteps_;
        }
    }

    template <class Tree>
    QuantLib::Size
    BinomialBonusClassicEngine<Tree>::resolvedMaxTimeSteps(QuantLib::Size timeSteps,
                                                           QuantLib::Size maxTimeSteps) {
        // The Greeks read the nodes of the lattice's second step.
        QL_REQUIRE(timeSteps >= 2, "at least 2 time steps required, " << timeSteps << " provided");
        QL_REQUIRE(maxTimeSteps == 0 || maxTimeSteps >= timeSteps,
                   "maxTimeSteps must be zero or greater than or equal to timeSteps, "
                       << maxTimeSteps << " not allowed");
        return (maxTimeSteps == 0) ? std::max<QuantLib::Size>(1000, 5 * timeSteps) : maxTimeSteps;
    }

    template <class Tree>
    QuantLib::Size BinomialBonusClassicEngine<Tree>::boyleLauSteps(QuantLib::Size timeSteps,
                                                                   QuantLib::Size maxTimeSteps,
                                                                   QuantLib::Real s0,
                                                                   QuantLib::Real barrier,
                                                                   QuantLib::Volatility v,
                                                                   QuantLib::Time maturity) {
        // With n steps a CRR layer i steps below the spot sits at s0 exp(-i v sqrt(T / n)); the
        // largest n that puts it at or below the barrier is the floor below.
        const auto divisor = std::pow(std::log(s0 / barrier), 2);
        if (QuantLib::close(divisor, 0.0)) {
            return timeSteps;
        }
        // QuantLib casts each candidate to Size first, which is undefined once the quotient
        // leaves Size's range, as it does with the spot just above the barrier. In Real until
        // capped, the count is QuantLib's wherever that cast is defined.
        const auto steps = static_cast<QuantLib::Real>(timeSteps);
        const auto cap = static_cast<QuantLib::Real>(maxTimeSteps);
        for (QuantLib::Size i = 1; i < timeSteps; ++i) {
            const auto layers = static_cast<QuantLib::Real>(i);
            const auto optimum = std::floor(layers * layers * v * v * maturity / divisor);
            if (optimum > steps) {
                return (optimum > cap) ? maxTimeSteps : static_cast<QuantLib::Size>(optimum);
            }
        }
        return timeSteps;
    }
}

#endif // BINOMIALBONUSCLASSICENGINE_HPP
