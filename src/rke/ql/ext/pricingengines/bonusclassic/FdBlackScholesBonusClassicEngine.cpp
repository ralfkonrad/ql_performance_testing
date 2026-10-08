// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <rke/ql/ext/pricingengines/bonusclassic/FdBlackScholesBonusClassicEngine.hpp>
#include <ql/exercise.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/solvers/fdmblackscholessolver.hpp>
#include <ql/methods/finitedifferences/solvers/fdmsolverdesc.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/utilities/fdmboundaryconditionset.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdiscountdirichletboundary.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <cmath>
#include <limits>
#include <list>
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
        QL_REQUIRE(monitorsContinuously(), "discrete monitoring is not implemented");
        // Without this, NPV() keeps returning the first price.
        registerWith(process_);
    }

    bool FdBlackScholesBonusClassicEngine::monitorsContinuously() const {
        return monitoringStepsPerYear_ == Null<Size>();
    }

    void FdBlackScholesBonusClassicEngine::calculate() const {
        const auto payoff = ext::dynamic_pointer_cast<BonusClassicPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-bonus-classic payoff given");
        QL_REQUIRE(payoff->bonusLevel() > 0.0, "bonus level less/equal zero not allowed");
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European,
                   "only european style option are supported");

        const auto spot = process_->x0();
        QL_REQUIRE(spot > 0.0, "negative or null underlying given");
        QL_REQUIRE(!triggered(spot), "barrier touched");

        const auto maturity = process_->time(arguments_.exercise->lastDate());

        const auto mesher = continuousMesher(xGrid_, process_, maturity, *payoff);
        const auto boundaries = assetLegBoundary(mesher, process_, maturity);
        const auto conditions = ext::make_shared<FdmStepConditionComposite>(
            std::list<std::vector<Time>>(), FdmStepConditionComposite::Conditions());
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
}
