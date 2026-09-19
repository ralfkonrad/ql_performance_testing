//
// Created by ralf.eckel on 22.06.2025.
//

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
            // Tests must clean up the fixings they set; this also guarantees that every
            // test case contains at least one assertion.
            BOOST_CHECK(QuantLib::IndexManager::instance().histories().empty());
            QuantLib::IndexManager::instance().clearHistories();
        }

      private:
        // Restore settings after each test.
        QuantLib::SavedSettings restore;
    };
}

#endif // TESTSUITEFIXTURE_HPP
