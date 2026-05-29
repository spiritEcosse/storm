#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

using namespace storm;
using namespace storm::orm::where;

template <typename ConnType> class ComplexWhereTest : public PersonSeedFixture<ConnType> {};

TYPED_TEST_SUITE(ComplexWhereTest, DatabaseTypes);

TYPED_TEST(ComplexWhereTest, OrWithAnd) {
    auto young    = field<^^Person::age>() < 26;
    auto old      = field<^^Person::age>() > 35;
    auto mkt      = field<^^Person::department>() == "Marketing";
    auto combined = young || (old && mkt);

    auto result = this->qs->where(combined).select().execute();
    ASSERT_TRUE(result.has_value()) << "Complex OR/AND should work";
    EXPECT_GE(result.value().size(), 1);
}

TYPED_TEST(ComplexWhereTest, NestedAndOr) {
    auto eng_young = field<^^Person::department>() == "Engineering" && field<^^Person::age>() < 30;
    auto sales_old = field<^^Person::department>() == "Sales" && field<^^Person::age>() > 29;

    auto result = this->qs->where(eng_young || sales_old).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}
