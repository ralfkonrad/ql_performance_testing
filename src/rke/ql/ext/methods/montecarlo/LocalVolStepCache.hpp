// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef LOCALVOLSTEPCACHE_HPP
#define LOCALVOLSTEPCACHE_HPP

#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/volatility/equityfx/localvoltermstructure.hpp>
#include <ql/timegrid.hpp>
#include <cmath>
#include <vector>

namespace RKE::QL::Ext {
    //! Path-independent terms of the Euler Black-Scholes step, once per time grid
    /*! With a smile, a forced discretization or an external local volatility,
        QuantLib::GeneralizedBlackScholesProcess::evolve() takes the Euler step
        through QuantLib::EulerDiscretization
        \f[
            x_{i+1} = x_i \exp\left((r_i - q_i - \sigma_i^2/2)\,\Delta t_i
                                    + \sigma_i \sqrt{\Delta t_i}\, dw\right),
            \qquad \sigma_i = \sigma_{loc}(t_i, x_i),
        \f]
        in which the forward rates \f$ r_i, q_i \f$ over \f$ [t_i, t_i + 10^{-4}] \f$
        depend on the grid only. The process evaluates them on every path step, and
        the local volatility twice, once for the drift and once for the diffusion.
        This class computes \f$ r_i - q_i \f$ and \f$ \sqrt{\Delta t_i} \f$ once,
        and evolve() evaluates the process's own local volatility once per step, so
        it returns the same double as the process.

        The process exposes neither its branch nor its discretization. The
        constructor therefore checks the step bit for bit against the process's
        evolve() on every step at two fixed points. The exact lognormal step, which
        evolve() takes with different arithmetic, fails the check, and so does any
        discretization other than QuantLib::EulerDiscretization. reproducesEvolve() is false
        if a check fails, and nothing is cached then.
    */
    class LocalVolStepCache {
      public:
        LocalVolStepCache(
            const QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>& process,
            const QuantLib::TimeGrid& grid);

        //! whether the cached terms reproduce the process's evolve()
        [[nodiscard]] bool reproducesEvolve() const { return reproducesEvolve_; }

        //! number of cached steps, 0 unless reproducesEvolve()
        [[nodiscard]] QuantLib::Size size() const { return rateDrift_.size(); }

        //! the process's evolve() over step i, from \p x0 with increment \p dw
        /*! \pre reproducesEvolve() */
        [[nodiscard]] QuantLib::Real
        evolve(QuantLib::Size i, QuantLib::Real x0, QuantLib::Real dw) const {
            // The shape of GeneralizedBlackScholesProcess::drift(), EulerDiscretization's
            // drift() and diffusion(), and apply(): a different expression could round
            // differently and change the last bit.
            const auto sigma = localVolatility_->localVol(time_[i], x0, true);
            return x0 * std::exp(((rateDrift_[i] - (0.5 * sigma * sigma)) * dt_[i]) +
                                 (sigma * sqrtDt_[i] * dw));
        }

      private:
        bool reproducesEvolve_ = false;
        QuantLib::ext::shared_ptr<QuantLib::LocalVolTermStructure> localVolatility_;
        std::vector<QuantLib::Time> time_;
        std::vector<QuantLib::Time> dt_;
        std::vector<QuantLib::Real> sqrtDt_;
        std::vector<QuantLib::Rate> rateDrift_;
    };
}

#endif // LOCALVOLSTEPCACHE_HPP
