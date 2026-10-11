// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BARRIERMONITORING_HPP
#define BARRIERMONITORING_HPP

#include <cstdint>

namespace RKE::QL::Ext {
    //! How MCBonusClassicEngine monitors the barrier
    /*! - Discrete: on the points after \f$ t = 0 \f$ of the engine's time
          grid, maturity included. Crossings between them go unseen; that is
          the product priced, not an error of the estimate.
        - Continuous: at every time, through the Brownian-bridge probability
          of surviving each step of the same grid.

        Discrete is QuantLib::MCBarrierEngine's isBiased = true. The grid is
        the engine's, not an observation schedule of the instrument.
    */
    enum class BarrierMonitoring : std::uint8_t { Discrete, Continuous };
}

#endif // BARRIERMONITORING_HPP
