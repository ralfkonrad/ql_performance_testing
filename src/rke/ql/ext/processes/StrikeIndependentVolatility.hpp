// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef STRIKEINDEPENDENTVOLATILITY_HPP
#define STRIKEINDEPENDENTVOLATILITY_HPP

#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/volatility/equityfx/localconstantvol.hpp>
#include <ql/termstructures/volatility/equityfx/localvolcurve.hpp>

namespace RKE::QL::Ext {
    //! whether the process's local volatility is of a type that does not depend on the spot
    /*! QuantLib::GeneralizedBlackScholesProcess::localVolatility() returns an
        external local volatility if one was given, and otherwise builds a
        QuantLib::LocalConstantVol from a QuantLib::BlackConstantVol, a
        QuantLib::LocalVolCurve from a QuantLib::BlackVarianceCurve, and a
        QuantLib::LocalVolSurface from any other Black volatility. The first
        two types are strike-independent; anything else, a surface or an
        external structure of another type, counts as a smile. Reading the
        local volatility rather than the Black volatility is what covers the
        external case. */
    [[nodiscard]] inline bool
    hasStrikeIndependentVolatility(const QuantLib::GeneralizedBlackScholesProcess& process) {
        const auto& vol = *process.localVolatility();
        return QuantLib::ext::dynamic_pointer_cast<QuantLib::LocalConstantVol>(vol) != nullptr ||
               QuantLib::ext::dynamic_pointer_cast<QuantLib::LocalVolCurve>(vol) != nullptr;
    }

    //! whether the process's Black and local volatility are both constant
    /*! Stricter than hasStrikeIndependentVolatility(): the QuantLib::LocalVolCurve
        derived from a QuantLib::BlackVarianceCurve varies in time and fails.
        The Black volatility is tested as well, because a closed form such as
        QuantLib::AnalyticBarrierEngine reads it, and an external
        QuantLib::LocalConstantVol beside a Black volatility of another type
        passes a test of the local one alone. */
    [[nodiscard]] inline bool
    hasConstantVolatility(const QuantLib::GeneralizedBlackScholesProcess& process) {
        const auto& blackVol = *process.blackVolatility();
        const auto& localVol = *process.localVolatility();
        return QuantLib::ext::dynamic_pointer_cast<QuantLib::BlackConstantVol>(blackVol) !=
                   nullptr &&
               QuantLib::ext::dynamic_pointer_cast<QuantLib::LocalConstantVol>(localVol) != nullptr;
    }
}

#endif // STRIKEINDEPENDENTVOLATILITY_HPP
