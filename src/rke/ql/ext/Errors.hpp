// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef ERRORS_HPP
#define ERRORS_HPP

/*! \def NOT_IMPLEMENTED_FAILURE
    \brief throw a QuantLib::Error reading "not implemented"

    Meant as the whole body of a virtual override that this library does not
    implement, so that calling it fails loudly instead of returning a default.

    \warning this header does not include <ql/errors.hpp>; the expanding file
             has to make QL_FAIL visible itself.
*/
// A function would report its own location: QL_FAIL takes __FILE__ and __LINE__ where it expands.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define NOT_IMPLEMENTED_FAILURE() QL_FAIL("not implemented")

#endif // ERRORS_HPP
