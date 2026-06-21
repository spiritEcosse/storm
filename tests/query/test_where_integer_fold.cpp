#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness,performance-unnecessary-value-param)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

using storm::QuerySet;
using storm::orm::where::f;

// Verifies that WHERE filters on narrow / unsigned integer fields fold to the
// existing int / int64_t variant arms (the same fold enums already use). Before
// #407 these source types had no ComparisonExpr arm and failed to compile.
//
// Seeds three rows so range filters split the set:
//   r1: u_int=10,  ll_signed=100,  tiny_unsigned=1
//   r2: u_int=20,  ll_signed=200,  tiny_unsigned=2
//   r3: u_int=30,  ll_signed=300,  tiny_unsigned=3
template <typename ConnType> class WhereIntegerFoldTest : public StormTestFixture<ExtendedTypes, ConnType> {
  protected:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        QuerySet<ExtendedTypes, ConnType> qs;
        std::vector<ExtendedTypes>        batch = {
                {.u_int = 10U, .ll_signed = 100LL, .label = "r1", .tiny_unsigned = 1},
                {.u_int = 20U, .ll_signed = 200LL, .label = "r2", .tiny_unsigned = 2},
                {.u_int = 30U, .ll_signed = 300LL, .label = "r3", .tiny_unsigned = 3},
        };
        ASSERT_TRUE(qs.insert(batch).execute().has_value());
    }

    static auto count_where(auto expr) -> std::size_t {
        QuerySet<ExtendedTypes, ConnType> qs;
        auto                              result = qs.where(expr).select().execute();
        EXPECT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();
        return result.has_value() ? result.value().size() : 0;
    }
};

TYPED_TEST_SUITE(WhereIntegerFoldTest, DatabaseTypes);

// ===== unsigned int → folds to int arm =====

TYPED_TEST(WhereIntegerFoldTest, UnsignedIntEqual) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::u_int>() == 20U), 1U);
}
TYPED_TEST(WhereIntegerFoldTest, UnsignedIntGreater) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::u_int>() > 10U), 2U);
}
TYPED_TEST(WhereIntegerFoldTest, UnsignedIntBetween) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::u_int>().between(15U, 25U)), 1U);
}
TYPED_TEST(WhereIntegerFoldTest, UnsignedIntIn) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::u_int>().in(10U, 30U)), 2U);
}

// ===== long long → folds to int64_t arm =====

TYPED_TEST(WhereIntegerFoldTest, LongLongLessEqual) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::ll_signed>() <= 200LL), 2U);
}
TYPED_TEST(WhereIntegerFoldTest, LongLongBetween) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::ll_signed>().between(150LL, 350LL)), 2U);
}
TYPED_TEST(WhereIntegerFoldTest, LongLongIn) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::ll_signed>().in(100LL, 300LL)), 2U);
}

// ===== unsigned char (narrow) → folds to int arm =====

TYPED_TEST(WhereIntegerFoldTest, TinyUnsignedGreaterEqual) {
    EXPECT_EQ(this->count_where(f<^^ExtendedTypes::tiny_unsigned>() >= static_cast<unsigned char>(2)), 2U);
}

// NOLINTEND(misc-const-correctness,performance-unnecessary-value-param)
