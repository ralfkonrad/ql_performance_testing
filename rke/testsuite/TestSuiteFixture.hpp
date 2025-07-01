//
// Created by ralf.eckel on 22.06.2025.
//

#ifndef TESTSUITEFIXTURE_HPP
#define TESTSUITEFIXTURE_HPP

#include <ql/indexes/indexmanager.hpp>
#include <ql/settings.hpp>
#include <boost/test/unit_test.hpp>


namespace RKE::QL::External {

    class TestSuiteFixture {
        // NOLINT(cppcoreguidelines-special-member-functions)
      public:
        TestSuiteFixture() = default;

        ~TestSuiteFixture() {
            QuantLib::IndexManager::instance().clearHistories();
            BOOST_CHECK(QuantLib::IndexManager::instance().histories().empty());
        }

      private:
        // Restore settings after each test.
        QuantLib::SavedSettings restore;
    };
}

#endif // TESTSUITEFIXTURE_HPP
