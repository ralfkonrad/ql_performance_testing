// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <ql/exercise.hpp>
#include <ql/math/comparison.hpp>
#include <ql/methods/finitedifferences/meshers/concentrating1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/solvers/fdmblackscholessolver.hpp>
#include <ql/methods/finitedifferences/solvers/fdmsolverdesc.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/utilities/fdmboundaryconditionset.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdiscountdirichletboundary.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/min_element.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <tuple>
#include <utility>
#include <vector>

using namespace QuantLib;

namespace RKE::QL::Ext {
    namespace {
        // The largest log-spot whose exponential does not exceed level. A grid node at
        // std::log(level) maps back above level for about a third of all levels, and the
        // payoff's point value there would be the bonus, not the knocked-out asset.
        Real logAtOrBelow(Real level) {
            Real x = std::log(level);
            while (std::exp(x) > level) {
                x = std::nextafter(x, -std::numeric_limits<Real>::infinity());
            }
            return x;
        }

        // The asset leg A(S, t) = S * P_q(T) / P_q(t) at the lowest node: there the certificate
        // is knocked out and worth the asset it still delivers.
        FdmBoundaryConditionSet
        assetLegBoundary(const ext::shared_ptr<FdmMesher>& mesher,
                         const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                         Time maturity) {
            const auto lowestSpot = std::exp(mesher->locations(0).front());
            return {
                ext::make_shared<FdmDiscountDirichletBoundary>(
                    mesher, process->dividendYield().currentLink(), maturity, lowestSpot, 0,
                    FdmDiscountDirichletBoundary::Lower),
            };
        }

        // A log-spot grid whose first node is the barrier, uniform above it, its upper end
        // chosen by FdmBlackScholesMesher around the bonus level.
        ext::shared_ptr<FdmMesher>
        continuousMesher(Size xGrid,
                         const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                         Time maturity,
                         const BonusClassicPayoff& payoff) {
            return ext::make_shared<FdmMesherComposite>(ext::make_shared<FdmBlackScholesMesher>(
                xGrid, process, maturity, payoff.bonusLevel(), logAtOrBelow(payoff.barrier())));
        }

        // FdmBlackScholesMesher's range around the bonus level, concentrated at the barrier,
        // which gets a node of its own, and at the payoff's kink at the bonus level.
        ext::shared_ptr<FdmMesher>
        discreteMesher(Size xGrid,
                       const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                       Time maturity,
                       const BonusClassicPayoff& payoff) {
            const FdmBlackScholesMesher range(xGrid, process, maturity, payoff.bonusLevel());
            const auto xMin = range.locations().front();
            const auto xMax = range.locations().back();
            const auto logBarrier = std::log(payoff.barrier());
            // Concentrating1dMesher silently drops a required point on or outside its ends.
            QL_REQUIRE(xMin < logBarrier && logBarrier < xMax,
                       "barrier " << payoff.barrier() << " outside the grid's range ["
                                  << std::exp(xMin) << ", " << std::exp(xMax) << "]");

            // The density is relative to the range, as FdBlackScholesVanillaEngine passes it.
            constexpr Real density = 0.1;
            const std::vector<std::tuple<Real, Real, bool>> cPoints = {
                {logBarrier, density, true},
                {std::log(payoff.bonusLevel()), density, false},
            };
            return ext::make_shared<FdmMesherComposite>(
                ext::make_shared<Concentrating1dMesher>(xMin, xMax, xGrid, cPoints));
        }

        // The knock-out on the monitoring grid's points strictly between zero and maturity:
        // FdmLogInnerValue's cell average already monitors maturity, and a stopping time at
        // zero would leave Fdm1DimSolver without a theta.
        ext::shared_ptr<FdmStepConditionComposite>
        knockOutConditions(const ext::shared_ptr<FdmMesher>& mesher,
                           const TimeGrid& grid,
                           const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                           const BonusClassicPayoff& payoff) {
            std::vector<Time> monitoringTimes(grid.begin() + 1, grid.end() - 1);
            const auto condition = ext::make_shared<FdmBonusClassicKnockOutCondition>(
                mesher, monitoringTimes, payoff.barrier(), grid.back(),
                process->dividendYield().currentLink());
            return ext::make_shared<FdmStepConditionComposite>(
                std::list<std::vector<Time>>{std::move(monitoringTimes)},
                FdmStepConditionComposite::Conditions{condition});
        }
    }

    FdBlackScholesBonusClassicEngine::FdBlackScholesBonusClassicEngine(
        ext::shared_ptr<GeneralizedBlackScholesProcess> process,
        Size monitoringStepsPerYear,
        Size tGrid,
        Size xGrid,
        Size dampingSteps,
        const FdmSchemeDesc& schemeDesc)
    : process_(std::move(process)), monitoringStepsPerYear_(monitoringStepsPerYear), tGrid_(tGrid),
      xGrid_(xGrid), dampingSteps_(dampingSteps), schemeDesc_(schemeDesc) {
        QL_REQUIRE(process_, "null process given");
        // Without this, NPV() keeps returning the first price.
        registerWith(process_);
    }

    bool FdBlackScholesBonusClassicEngine::monitorsContinuously() const {
        return monitoringStepsPerYear_ == Null<Size>();
    }

    TimeGrid FdBlackScholesBonusClassicEngine::timeGrid() const {
        // Empty until the instrument's setupArguments() has run, i.e. before the first NPV().
        QL_REQUIRE(arguments_.exercise, "no exercise given");
        const auto residualTime = process_->time(arguments_.exercise->lastDate());
        if (!monitorsContinuously()) {
            // MCBonusClassicEngine's rule, so that both engines price the same product. A short
            // residual time truncates steps to 0, and TimeGrid(end, 0) divides by zero.
            const auto steps =
                static_cast<Size>(static_cast<Real>(monitoringStepsPerYear_) * residualTime);
            // QuantLib::TimeGrid has an initializer_list<Time> constructor, which a braced
            // return selects over TimeGrid(Time, Size), narrowing steps to a Time.
            return TimeGrid( // NOLINT(modernize-return-braced-init-list)
                residualTime, std::max<Size>(steps, 1));
        }
        QL_FAIL("the barrier is monitored continuously, on no time grid");
    }

    void FdBlackScholesBonusClassicEngine::calculate() const {
        const auto payoff = ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-bonus-classic payoff given");
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European,
                   "only european style option are supported");

        const auto spot = process_->x0();
        QL_REQUIRE(spot > 0.0, "negative or null underlying given");
        QL_REQUIRE(!triggered(spot), "barrier touched");

        const auto maturity = process_->time(arguments_.exercise->lastDate());

        const auto mesher = monitorsContinuously() ?
                                continuousMesher(xGrid_, process_, maturity, *payoff) :
                                discreteMesher(xGrid_, process_, maturity, *payoff);
        const auto boundaries = assetLegBoundary(mesher, process_, maturity);
        const auto conditions =
            monitorsContinuously() ?
                ext::make_shared<FdmStepConditionComposite>(
                    std::list<std::vector<Time>>(), FdmStepConditionComposite::Conditions()) :
                knockOutConditions(mesher, timeGrid(), process_, *payoff);
        const auto calculator = ext::make_shared<FdmLogInnerValue>(payoff, mesher, 0);

        const FdmSolverDesc solverDesc = {
            mesher, boundaries, conditions, calculator, maturity, tGrid_, dampingSteps_,
        };
        // The bonus level is the strike at which the operator reads the volatility.
        const auto solver = ext::make_shared<FdmBlackScholesSolver>(
            Handle<GeneralizedBlackScholesProcess>(process_), payoff->bonusLevel(), solverDesc,
            schemeDesc_);

        // valueAt() runs the rollback; thetaAt() reads its snapshot without triggering it, so
        // it has to come after one of the other three.
        results_.value = solver->valueAt(spot);
        results_.delta = solver->deltaAt(spot);
        results_.gamma = solver->gammaAt(spot);
        results_.theta = solver->thetaAt(spot);
    }

    FdmBonusClassicKnockOutCondition::FdmBonusClassicKnockOutCondition(
        const ext::shared_ptr<FdmMesher>& mesher,
        std::vector<Time> monitoringTimes,
        Real barrier,
        Time maturity,
        ext::shared_ptr<YieldTermStructure> dividendYield)
    : spots_(Exp(mesher->locations(0))), monitoringTimes_(std::move(monitoringTimes)),
      maturity_(maturity), dividendYield_(std::move(dividendYield)) {
        QL_REQUIRE(mesher->layout()->dim().size() == 1, "one-dimensional mesher required");
        QL_REQUIRE(dividendYield_, "null dividend curve given");

        const auto x = mesher->locations(0);
        const auto logBarrier = std::log(barrier);
        const auto* const nearest = boost::range::min_element(x, [logBarrier](Real l, Real r) {
            return std::fabs(l - logBarrier) < std::fabs(r - logBarrier);
        });
        barrierIndex_ = static_cast<Size>(nearest - x.begin());
        QL_REQUIRE(close_enough(*nearest, logBarrier), "no grid node on the barrier " << barrier);
        QL_REQUIRE(barrierIndex_ > 0 && barrierIndex_ + 1 < x.size(),
                   "the barrier " << barrier << " is not an inner grid node");

        // The share of the node's cell [x_k - dx_k^- / 2, x_k + dx_k^+ / 2] below the barrier.
        const auto dminus = x[barrierIndex_] - x[barrierIndex_ - 1];
        const auto dplus = x[barrierIndex_ + 1] - x[barrierIndex_];
        weight_ = dminus / (dminus + dplus);
    }

    void FdmBonusClassicKnockOutCondition::applyTo(Array& a, Time t) const {
        // The model also calls this after every ordinary step, with times of its own grid; and
        // a damping rollback ending on a monitoring time hands it on to the main rollback,
        // which applies its first stopping time again. The overwrite of the barrier node is not
        // idempotent, so each monitoring time acts once.
        if (t == lastApplied_ ||
            boost::range::find(monitoringTimes_, t) == monitoringTimes_.end()) {
            return;
        }
        lastApplied_ = t;

        const auto growth = dividendYield_->discount(maturity_) / dividendYield_->discount(t);
        for (Size i = 0; i < barrierIndex_; ++i) {
            a[i] = spots_[i] * growth;
        }
        a[barrierIndex_] =
            (weight_ * spots_[barrierIndex_] * growth) + ((1.0 - weight_) * a[barrierIndex_]);
    }
}
