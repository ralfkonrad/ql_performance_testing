// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <rke/ql/ext/processes/StrikeIndependentVolatility.hpp>
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
#include <ql/methods/finitedifferences/utilities/fdmdirichletboundary.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdiscountdirichletboundary.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/min_element.hpp>
#include <cmath>
#include <limits>
#include <list>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace QuantLib;

namespace RKE::QL::Ext {
    namespace {
        using KnockedOutValue = FdmBonusClassicKnockOutCondition::KnockedOutValue;

        // 1 on (lower, upper] and 0 elsewhere: the indicator of an event at maturity, rolled
        // back like a payoff. The hit indicator is (0, H], the barrier being inclusive; the
        // bonus indicator (H, B], where at S = B itself the certificate pays max(S, B) = S
        // either way, so the closed upper end moves nothing. FdmLogInnerValue averages it over
        // each cell, so the node on H takes its cell's share below H, the weight the knock-out
        // condition uses, up to SimpsonIntegral's quadrature error on a step; where that
        // integral does not converge FdmCellAveragingInnerValue falls back to the point value.
        class IndicatorPayoff : public Payoff {
          public:
            IndicatorPayoff(Real lower, Real upper) : lower_(lower), upper_(upper) {}

            [[nodiscard]] std::string name() const override { return "Indicator"; }
            [[nodiscard]] std::string description() const override {
                std::ostringstream out;
                out << name() << " on (" << lower_ << ", " << upper_ << "]";
                return out.str();
            }
            Real operator()(Real price) const override {
                return (lower_ < price && price <= upper_) ? 1.0 : 0.0;
            }
            void accept(AcyclicVisitor& acyclic_visitor) override {
                auto* visitor = dynamic_cast<Visitor<IndicatorPayoff>*>(&acyclic_visitor);
                if (visitor != nullptr) {
                    visitor->visit(*this);
                } else {
                    Payoff::accept(acyclic_visitor);
                }
            }

          private:
            Real lower_;
            Real upper_;
        };

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

        // The curve a knocked-out value is grown or discounted on from maturity: the dividend
        // curve carries the asset leg, the risk-free curve a unit of cash, and nothing needs
        // none.
        ext::shared_ptr<YieldTermStructure>
        knockedOutCurve(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                        KnockedOutValue knockedOutValue) {
            switch (knockedOutValue) {
                case KnockedOutValue::AssetLeg:
                    return process->dividendYield().currentLink();
                case KnockedOutValue::Cash:
                    return process->riskFreeRate().currentLink();
                case KnockedOutValue::Nothing:
                    return nullptr;
            }
            QL_FAIL("unknown knocked-out value");
        }

        // The knocked-out value at the lowest node, where the certificate is knocked out in
        // either monitoring mode: the asset leg A(S, t) = S * P_q(T) / P_q(t) it still
        // delivers, a unit of cash P_r(T) / P_r(t), or zero.
        FdmBoundaryConditionSet
        lowerBoundary(const ext::shared_ptr<FdmMesher>& mesher,
                      const ext::shared_ptr<GeneralizedBlackScholesProcess>& process,
                      Time maturity,
                      KnockedOutValue knockedOutValue) {
            if (knockedOutValue == KnockedOutValue::Nothing) {
                return {
                    ext::make_shared<FdmDirichletBoundary>(mesher, 0.0, 0,
                                                           FdmDirichletBoundary::Lower),
                };
            }
            const auto unit = knockedOutValue == KnockedOutValue::AssetLeg ?
                                  std::exp(mesher->locations(0).front()) :
                                  1.0;
            return {
                ext::make_shared<FdmDiscountDirichletBoundary>(
                    mesher, knockedOutCurve(process, knockedOutValue), maturity, unit, 0,
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
                           const BonusClassicPayoff& payoff,
                           KnockedOutValue knockedOutValue) {
            std::vector<Time> monitoringTimes(grid.begin() + 1, grid.end() - 1);
            const auto condition = ext::make_shared<FdmBonusClassicKnockOutCondition>(
                mesher, monitoringTimes, payoff.barrier(), grid.back(), knockedOutValue,
                knockedOutCurve(process, knockedOutValue));
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
        const FdmSchemeDesc& schemeDesc,
        bool calculateProbabilities,
        bool localVol)
    : process_(std::move(process)), monitoringStepsPerYear_(monitoringStepsPerYear), tGrid_(tGrid),
      xGrid_(xGrid), dampingSteps_(dampingSteps), schemeDesc_(schemeDesc),
      calculateProbabilities_(calculateProbabilities), localVol_(localVol) {
        QL_REQUIRE(process_, "null process given");
        // Without the flag the operator reads one Black variance at the bonus level, which
        // prices a smile as a flat market; see the class description.
        QL_REQUIRE(localVol_ || hasStrikeIndependentVolatility(*process_),
                   "a spot-dependent local volatility needs localVol = true");
        // Without this, NPV() keeps returning the first price.
        registerWith(process_);
    }

    bool FdBlackScholesBonusClassicEngine::monitorsContinuously() const {
        return monitoringStepsPerYear_ == Null<Size>();
    }

    bool FdBlackScholesBonusClassicEngine::calculatesProbabilities() const {
        return calculateProbabilities_;
    }

    bool FdBlackScholesBonusClassicEngine::usesLocalVolatility() const {
        return localVol_;
    }

    TimeGrid FdBlackScholesBonusClassicEngine::timeGrid() const {
        // Empty until the instrument's setupArguments() has run, i.e. before the first NPV().
        QL_REQUIRE(arguments_.exercise, "no exercise given");
        if (!monitorsContinuously()) {
            return monitoringGrid(process_->time(arguments_.exercise->lastDate()),
                                  monitoringStepsPerYear_);
        }
        QL_FAIL("the barrier is monitored continuously, on no time grid");
    }

    ext::shared_ptr<FdmBlackScholesSolver>
    FdBlackScholesBonusClassicEngine::makeSolver(const BonusClassicPayoff& certificate,
                                                 Time maturity,
                                                 const ext::shared_ptr<Payoff>& innerValue,
                                                 KnockedOutValue knockedOutValue) const {
        const auto mesher = monitorsContinuously() ?
                                continuousMesher(xGrid_, process_, maturity, certificate) :
                                discreteMesher(xGrid_, process_, maturity, certificate);
        const auto boundaries = lowerBoundary(mesher, process_, maturity, knockedOutValue);
        const auto conditions =
            monitorsContinuously() ?
                ext::make_shared<FdmStepConditionComposite>(
                    std::list<std::vector<Time>>(), FdmStepConditionComposite::Conditions()) :
                knockOutConditions(mesher, timeGrid(), process_, certificate, knockedOutValue);
        const auto calculator = ext::make_shared<FdmLogInnerValue>(innerValue, mesher, 0);

        const FdmSolverDesc solverDesc = {
            mesher, boundaries, conditions, calculator, maturity, tGrid_, dampingSteps_,
        };
        // The bonus level is the strike the operator reads the Black variance at with localVol
        // off; with it on the operator squares the process's local volatility at every node
        // and step instead, and the solver's overwrite for a negative local variance stays at
        // its default, so such a surface throws.
        return ext::make_shared<FdmBlackScholesSolver>(
            Handle<GeneralizedBlackScholesProcess>(process_), certificate.bonusLevel(), solverDesc,
            schemeDesc_, localVol_);
    }

    void FdBlackScholesBonusClassicEngine::calculate() const {
        const auto payoff = ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-bonus-classic payoff given");
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European,
                   "only european style option are supported");

        const auto spot = process_->x0();
        QL_REQUIRE(spot > 0.0, "negative or null underlying given");
        // In discrete mode t = 0 is no monitoring time, so this is a restriction rather than a
        // knock-out; see the class warning.
        QL_REQUIRE(!triggered(spot), "barrier touched");

        const auto maturity = process_->time(arguments_.exercise->lastDate());

        const auto solver = makeSolver(*payoff, maturity, payoff, KnockedOutValue::AssetLeg);
        // valueAt() runs the rollback; thetaAt() reads its snapshot without triggering it, so
        // it has to come after one of the other three.
        const auto value = solver->valueAt(spot);
        const auto delta = solver->deltaAt(spot);
        const auto gamma = solver->gammaAt(spot);
        const auto theta = solver->thetaAt(spot);

        results_.value = value;
        results_.delta = delta;
        results_.gamma = gamma;
        results_.theta = theta;

        if (!calculateProbabilities_) {
            return;
        }

        // Each indicator rolls back under the same PDE, which discounts at r, so its value at
        // the spot is E[P_r(T) 1_A] = P_r(T) P(A) for the deterministic curve; the division
        // takes the discount factor off. The hit is worth one unit at maturity once it has
        // happened, the bonus nothing, and the lowest node holds the same.
        const auto discount = process_->riskFreeRate()->discount(maturity);
        const auto hitSolver =
            makeSolver(*payoff, maturity, ext::make_shared<IndicatorPayoff>(0.0, payoff->barrier()),
                       KnockedOutValue::Cash);
        const auto bonusSolver =
            makeSolver(*payoff, maturity,
                       ext::make_shared<IndicatorPayoff>(payoff->barrier(), payoff->bonusLevel()),
                       KnockedOutValue::Nothing);
        const Real barrierHitProbability = hitSolver->valueAt(spot) / discount;
        const Real bonusProbability = bonusSolver->valueAt(spot) / discount;

        results_.additionalResults["barrierHitProbability"] = barrierHitProbability;
        results_.additionalResults["bonusProbability"] = bonusProbability;
    }

    FdmBonusClassicKnockOutCondition::FdmBonusClassicKnockOutCondition(
        const ext::shared_ptr<FdmMesher>& mesher,
        std::vector<Time> monitoringTimes,
        Real barrier,
        Time maturity,
        KnockedOutValue knockedOutValue,
        ext::shared_ptr<YieldTermStructure> curve)
    : spots_(Exp(mesher->locations(0))), monitoringTimes_(std::move(monitoringTimes)),
      maturity_(maturity), knockedOutValue_(knockedOutValue), curve_(std::move(curve)) {
        QL_REQUIRE(mesher->layout()->dim().size() == 1, "one-dimensional mesher required");
        QL_REQUIRE(knockedOutValue_ == KnockedOutValue::Nothing || curve_,
                   "null curve given for the knocked-out value");

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

        // The knocked-out value is unit(i) * factor: the spot grown on the dividend curve for
        // the asset leg, one unit discounted on the risk-free curve for cash, and zero for
        // nothing, whose curve is never read. The products keep the asset leg's association,
        // weight_ * spot * growth, so the price path is unchanged to the double.
        const auto factor = knockedOutValue_ == KnockedOutValue::Nothing ?
                                0.0 :
                                curve_->discount(maturity_) / curve_->discount(t);
        const auto unit = [this](Size i) {
            return knockedOutValue_ == KnockedOutValue::AssetLeg ? spots_[i] : 1.0;
        };
        for (Size i = 0; i < barrierIndex_; ++i) {
            a[i] = unit(i) * factor;
        }
        a[barrierIndex_] =
            (weight_ * unit(barrierIndex_) * factor) + ((1.0 - weight_) * a[barrierIndex_]);
    }
}
