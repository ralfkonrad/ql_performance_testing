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
        through the same process calls in the same order as evolve().

        evolve() returns the process's step up to rounding, not bit for bit. Its
        arithmetic is QuantLib's, compiled in another translation unit, where
        inlining and contraction into FMAs may round the last bit differently:
        gcc at -march=x86-64-v3 with -D_GLIBCXX_ASSERTIONS moves it by up to
        \f$ 4.3 \cdot 10^{-16} \f$ relative, measured.

        The process does not expose which branch it takes: a forced
        discretization and an external local volatility both turn it into an
        Euler scheme whatever the Black volatility's type. The constructor
        therefore compares the step with the process's evolve() on every step
        at two fixed points, to the relative stepTolerance. The Euler step misses
        by terms that do not vanish with the rounding, except on flat curves
        under a constant volatility, where it coincides with the exact step in
        exact arithmetic and passes on fine grids; see stepTolerance.
        reproducesEvolve() is false if the volatility is of any other type or a
        check fails, and nothing is cached then.
    */
    class BlackScholesStepCache {
      public:
        //! relative tolerance between evolve() and the process's step
        /*! Rounding moves one step by up to \f$ 4.3 \cdot 10^{-16} \f$, measured
            with gcc at -march=x86-64-v3 and -D_GLIBCXX_ASSERTIONS; this leaves
            230 times that. On flat curves under a constant volatility the Euler
            step differs from the exact one only by the rounding of its drift's
            forward rate over \f$ 10^{-4} \f$ years, about
            \f$ 4 \cdot 10^{-12}\,\Delta t \f$ relative, measured: a forced
            discretization or an external local volatility passes there where
            \f$ \Delta t < 0.024 \f$ and is refused on coarser grids. */
        static constexpr QuantLib::Real stepTolerance = 1.0e-13;

        /*! \pre \p process is not null and \p grid has at least one step;
                 QL_REQUIRE checks both. */
        BlackScholesStepCache(
            const QuantLib::ext::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>& process,
            const QuantLib::TimeGrid& grid);

        //! whether the cached terms reproduce the process's evolve()
        [[nodiscard]] bool reproducesEvolve() const { return reproducesEvolve_; }

        //! number of cached steps, 0 unless reproducesEvolve()
        [[nodiscard]] QuantLib::Size size() const { return variance_.size(); }

        //! integrated variance of step i
        /*! \pre reproducesEvolve() */
        [[nodiscard]] QuantLib::Real variance(QuantLib::Size i) const { return variance_[i]; }

        //! the process's evolve() over step i, from \p x0 with increment \p dw
        /*! Within stepTolerance of the process's step.
            \pre reproducesEvolve() */
        [[nodiscard]] QuantLib::Real
        evolve(QuantLib::Size i, QuantLib::Real x0, QuantLib::Real dw) const {
            // The shape of GeneralizedBlackScholesProcess::evolve() and apply(), which is the
            // same double where both are compiled alike.
            return x0 * std::exp((stdDeviation_[i] * dw) + drift_[i]);
        }

      private:
        bool reproducesEvolve_ = false;
        std::vector<QuantLib::Real> variance_;
        std::vector<QuantLib::Real> stdDeviation_;
        std::vector<QuantLib::Real> drift_;
    };
}

#endif // BLACKSCHOLESSTEPCACHE_HPP
