// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT


#define BOOST_TEST_MODULE RkeQlTests

// Pulls in the <boost/config.hpp> that defines the BOOST_MSVC tested below.
#include <ql/qldefines.hpp>
// The included/ variant compiles Boost.Test into this translation unit, which is why the
// target links no Boost library.
#include <boost/test/included/unit_test.hpp>

#if !defined(BOOST_ALL_NO_LIB) && defined(BOOST_MSVC)
#    include <ql/auto_link.hpp>
#endif
