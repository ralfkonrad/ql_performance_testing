// SPDX-FileCopyrightText: 2025 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef TESTSUITEFIXTURE_HPP
#define TESTSUITEFIXTURE_HPP

#include <ql/indexes/indexmanager.hpp>
#include <ql/settings.hpp>
#include <boost/test/unit_test.hpp>


namespace RKE::QL::External {

    class TestSuiteFixture { // NOLINT(cppcoreguidelines-special-member-functions)
      public:
        TestSuiteFixture() = default;

        ~TestSuiteFixture() {
            // The clear runs even when the check fails. The check is on the names IndexManager
            // holds, because getHistory() inserts an empty entry although it is const.
            BOOST_CHECK(QuantLib::IndexManager::instance().histories().empty());
            QuantLib::IndexManager::instance().clearHistories();
        }

      private:
        // Held for its destructor, which is why a test case assigns
        // Settings::instance().evaluationDate() directly and adds no second SavedSettings.
        QuantLib::SavedSettings restore;
    };
}

#endif // TESTSUITEFIXTURE_HPP
