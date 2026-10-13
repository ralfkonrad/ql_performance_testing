// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef LOWDISCREPANCYJOEKUOD7_HPP
#define LOWDISCREPANCYJOEKUOD7_HPP

#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/math/randomnumbers/sobolrsg.hpp>
#include <ql/types.hpp>

namespace RKE::Common {
    // LowDiscrepancy with Joe-Kuo D7 direction integers, tabulated up to dimension 1898,
    // instead of SobolRsg's default Jaeckel ones, tabulated up to 32 and drawn from the seed
    // beyond. One dimension per time step, so a daily grid needs the former. Otherwise the
    // factory is GenericLowDiscrepancy's, icInstance included.
    struct LowDiscrepancyJoeKuoD7 : QuantLib::LowDiscrepancy {
        static rsg_type make_sequence_generator(QuantLib::Size dimension,
                                                QuantLib::BigNatural seed) {
            const auto g = QuantLib::SobolRsg(dimension, seed, QuantLib::SobolRsg::JoeKuoD7);
            return icInstance ? rsg_type(g, *icInstance) : rsg_type(g);
        }
    };
}

#endif // LOWDISCREPANCYJOEKUOD7_HPP
