// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BLACKSCHOLESSTEPCACHE_HPP
#define BLACKSCHOLESSTEPCACHE_HPP

#include <ql/processes/blackscholesprocess.hpp>
#include <ql/timegrid.hpp>
#include <cmath>
#include <vector>

namespace RKE::QL::Ext {
    //! Path-independent terms of the exact Black-Scholes step, once per time grid
    /*! With a strike-independent volatility (QuantLib::BlackConstantVol or
        QuantLib::BlackVarianceCurve) and no forced discretization,
        QuantLib::GeneralizedBlackScholesProcess::evolve() takes the exact
        lognormal step
        \f[
            x_{i+1} = x_i \exp\left(\sqrt{v_i}\, dw + (r_i - q_i)\,\Delta t_i - v_i/2\right),
        \f]
        in which the variance \f$ v_i \f$ and the forward rates \f$ r_i, q_i \f$
        over the step depend on the grid only. This class computes them once,
        through the same process calls in the same order as evolve(), so
        evolve(i, x, dw) returns the same double as the process.

        The process does not expose which branch it takes: a forced
        discretization and an external local volatility both turn it into an
        Euler scheme whatever the Black volatility's type. The constructor
        therefore checks the cached step bit for bit against the process's
        evolve() on every step at two fixed points. isExact() is false if the
        volatility is of any other type or a check fails, and nothing is
        cached then.
    */
    class BlackScholesStepCache {
      public:
        BlackScholesStepCache(
            const QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>& process,
            const QuantLib::TimeGrid& grid);

        //! whether the cached terms reproduce the process's evolve()
        [[nodiscard]] bool isExact() const { return isExact_; }

        //! number of cached steps, 0 unless isExact()
        [[nodiscard]] QuantLib::Size size() const { return variance_.size(); }

        //! integrated variance of step i
        /*! \pre isExact() */
        [[nodiscard]] QuantLib::Real variance(QuantLib::Size i) const { return variance_[i]; }

        //! the process's evolve() over step i, from \p x0 with increment \p dw
        /*! \pre isExact() */
        [[nodiscard]] QuantLib::Real
        evolve(QuantLib::Size i, QuantLib::Real x0, QuantLib::Real dw) const {
            // The shape of GeneralizedBlackScholesProcess::evolve() and apply(): a different
            // expression could be contracted differently and change the last bit.
            return x0 * std::exp((stdDeviation_[i] * dw) + drift_[i]);
        }

      private:
        bool isExact_ = false;
        std::vector<QuantLib::Real> variance_;
        std::vector<QuantLib::Real> stdDeviation_;
        std::vector<QuantLib::Real> drift_;
    };
}

#endif // BLACKSCHOLESSTEPCACHE_HPP
