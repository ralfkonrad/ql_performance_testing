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
            BOOST_CHECK(QuantLib::IndexManager::instance().histories().empty());
            QuantLib::IndexManager::instance().clearHistories();
        }

      private:
        QuantLib::SavedSettings restore;
    };
}

#endif // TESTSUITEFIXTURE_HPP
