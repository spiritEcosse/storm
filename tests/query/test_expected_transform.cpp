#include <gtest/gtest.h>
#include "plf_hive/plf_hive.h"
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-rvalue-reference-param-not-moved)

import storm;
import <string>;
import <vector>;
import <expected>;
import <ranges>;
import <algorithm>;
import <utility>;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

using namespace storm;
using namespace storm::orm::where;

template <typename ConnType> class ExpectedTransformTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));
    }
};

TYPED_TEST_SUITE(ExpectedTransformTest, DatabaseTypes);

// The recommended idiom pairs `std::expected::transform` with `std::views::as_rvalue`
// so `std::ranges::to` move-constructs elements from the source hive instead of copying.
// Without `as_rvalue`, the hive's iterators yield `Person&` and elements get deep-copied.

// Compile-time proof that `as_rvalue` turns hive iteration into rvalue references.
static_assert(
        std::is_rvalue_reference_v<
                std::ranges::range_reference_t<decltype(std::declval<plf::hive<Person>&&>() | std::views::as_rvalue)>>,
        "as_rvalue over plf::hive must yield rvalue references for move semantics"
);

TYPED_TEST(ExpectedTransformTest, HiveToVectorViaTransformMoves) {
    QuerySet<Person, TypeParam> qs;

    auto vec_result = qs.select().execute().transform([](auto&& hive) {
        return std::forward<decltype(hive)>(hive) | std::views::as_rvalue | std::ranges::to<std::vector<Person>>();
    });

    ASSERT_TRUE(vec_result.has_value()) << "Query failed: " << vec_result.error().message();
    EXPECT_EQ(vec_result.value().size(), 25U);
}

TYPED_TEST(ExpectedTransformTest, EmptyResultTransformYieldsEmptyVector) {
    QuerySet<Person, TypeParam> qs;

    auto vec_result = qs.where(field<^^Person::age>() > 10000).select().execute().transform([](auto&& hive) {
        return std::forward<decltype(hive)>(hive) | std::views::as_rvalue | std::ranges::to<std::vector<Person>>();
    });

    ASSERT_TRUE(vec_result.has_value()) << "Query failed: " << vec_result.error().message();
    EXPECT_TRUE(vec_result.value().empty());
}

TYPED_TEST(ExpectedTransformTest, ChainViewsInsideTransformMoves) {
    QuerySet<Person, TypeParam> qs;

    auto names_result = qs.select().execute().transform([](auto&& hive) {
        return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
               std::views::transform([](Person&& p) -> std::string&& { return std::move(p.name); }) |
               std::ranges::to<std::vector<std::string>>();
    });

    ASSERT_TRUE(names_result.has_value()) << "Query failed: " << names_result.error().message();
    EXPECT_EQ(names_result.value().size(), 25U);
    for (const auto& name : names_result.value()) {
        EXPECT_FALSE(name.empty());
    }
}

TYPED_TEST(ExpectedTransformTest, TransformPreservesWhereFilter) {
    QuerySet<Person, TypeParam> qs;

    auto vec_result = qs.where(field<^^Person::age>() > 35).select().execute().transform([](auto&& hive) {
        return std::forward<decltype(hive)>(hive) | std::views::as_rvalue | std::ranges::to<std::vector<Person>>();
    });

    ASSERT_TRUE(vec_result.has_value()) << "Query failed: " << vec_result.error().message();
    EXPECT_GT(vec_result.value().size(), 0U);
    for (const auto& p : vec_result.value()) {
        EXPECT_GT(p.age, 35);
    }
}

// Error propagation: build a pre-failed expected and verify transform() skips the lambda.
TYPED_TEST(ExpectedTransformTest, TransformPropagatesErrorWithoutInvokingLambda) {
    using Error = typename TypeParam::Error;

    std::expected<plf::hive<Person>, Error> failed{std::unexpect, Error{42, "synthetic failure"}};

    bool lambda_invoked = false;
    auto result         = std::move(failed).transform([&lambda_invoked](auto&& hive) {
        lambda_invoked = true;
        return std::forward<decltype(hive)>(hive) | std::views::as_rvalue | std::ranges::to<std::vector<Person>>();
    });

    EXPECT_FALSE(lambda_invoked);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), 42);
    EXPECT_EQ(result.error().message(), "synthetic failure");
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-rvalue-reference-param-not-moved)
