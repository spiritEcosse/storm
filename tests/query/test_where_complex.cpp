#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

using storm::orm::where::f;

template <typename ConnType> class ComplexWhereTest : public PersonSeedFixture<ConnType> {};

TYPED_TEST_SUITE(ComplexWhereTest, DatabaseTypes);

TYPED_TEST(ComplexWhereTest, OrWithAnd) {
    auto young    = f<^^Person::age>() < 26;
    auto old      = f<^^Person::age>() > 35;
    auto mkt      = f<^^Person::department>() == "Marketing";
    auto combined = young || (old && mkt);

    auto result = this->qs->where(combined).select().execute();
    ASSERT_TRUE(result.has_value()) << "Complex OR/AND should work";
    EXPECT_GE(result.value().size(), 1);
}

TYPED_TEST(ComplexWhereTest, NestedAndOr) {
    auto eng_young = f<^^Person::department>() == "Engineering" && f<^^Person::age>() < 30;
    auto sales_old = f<^^Person::department>() == "Sales" && f<^^Person::age>() > 29;

    auto result = this->qs->where(eng_young || sales_old).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 1);
}
