#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;
import <ranges>;
import <algorithm>;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

using namespace storm;
using namespace storm::orm::where;

template <typename ConnType> class RowsTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));
    }
};

TYPED_TEST_SUITE(RowsTest, DatabaseTypes);

TYPED_TEST(RowsTest, BasicIteration) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value()) << "Row error: " << result.error().message_;
        ++count;
    }
    EXPECT_EQ(count, 25);
}

TYPED_TEST(RowsTest, EmptyTable) {
    QuerySet<Person, TypeParam> qs;
    qs.erase_all().execute();

    int count = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_EQ(count, 0);
}

TYPED_TEST(RowsTest, SingleRow) {
    QuerySet<Person, TypeParam> qs;
    qs.erase_all().execute();
    qs.insert(storm::test::PEOPLE_25[0]).execute();

    int count = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().name, storm::test::PEOPLE_25[0].name);
        ++count;
    }
    EXPECT_EQ(count, 1);
}

TYPED_TEST(RowsTest, WhereFilter) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.where(field<^^Person::age>() > 35).rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result.value().age, 35);
        ++count;
    }
    EXPECT_GT(count, 0);
    EXPECT_LT(count, 25);
}

TYPED_TEST(RowsTest, WhereComplex) {
    QuerySet<Person, TypeParam> qs;
    auto                        expr  = field<^^Person::age>() > 30 || field<^^Person::name>() == "Alice";
    int                         count = 0;
    for (auto&& result : qs.where(expr).rows()) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_GT(count, 0);
}

TYPED_TEST(RowsTest, WithLimit) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.limit(5).rows()) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TYPED_TEST(RowsTest, WithLimitOffset) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.limit(5).offset(3).rows()) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TYPED_TEST(RowsTest, WithOrderBy) {
    QuerySet<Person, TypeParam> qs;
    int                         prev_age = -1;
    for (auto&& result : qs.template order_by<^^Person::age>().rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_GE(result.value().age, prev_age);
        prev_age = result.value().age;
    }
}

TYPED_TEST(RowsTest, WithAllModifiers) {
    QuerySet<Person, TypeParam> qs;
    int                         count    = 0;
    int                         prev_age = -1;
    for (auto&& result :
         qs.where(field<^^Person::age>() > 25).template order_by<^^Person::age>().limit(5).offset(2).rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result.value().age, 25);
        EXPECT_GE(result.value().age, prev_age);
        prev_age = result.value().age;
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TYPED_TEST(RowsTest, EarlyBreak) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
        if (++count == 3)
            break;
    }
    EXPECT_EQ(count, 3);

    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 25);
}

TYPED_TEST(RowsTest, DoesNotCorruptCache) {
    QuerySet<Person, TypeParam> qs;

    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
    }

    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 25);
}

TYPED_TEST(RowsTest, MultipleGenerators) {
    QuerySet<Person, TypeParam> qs;

    int count1 = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
        ++count1;
    }

    int count2 = 0;
    for (auto&& result : qs.rows()) {
        ASSERT_TRUE(result.has_value());
        ++count2;
    }

    EXPECT_EQ(count1, 25);
    EXPECT_EQ(count2, 25);
}

TYPED_TEST(RowsTest, RepeatedCallsSameWhere) {
    QuerySet<Person, TypeParam> qs;
    auto                        expr = field<^^Person::age>() > 30;

    int count1 = 0;
    for (auto&& result : qs.where(expr).rows()) {
        ASSERT_TRUE(result.has_value());
        ++count1;
    }

    int count2 = 0;
    for (auto&& result : qs.where(expr).rows()) {
        ASSERT_TRUE(result.has_value());
        ++count2;
    }

    EXPECT_EQ(count1, count2);
    EXPECT_GT(count1, 0);
}

TYPED_TEST(RowsTest, DifferentQueries) {
    QuerySet<Person, TypeParam> qs1;
    QuerySet<Person, TypeParam> qs2;

    int count_young = 0;
    for (auto&& result : qs1.where(field<^^Person::age>() < 30).rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_LT(result.value().age, 30);
        ++count_young;
    }

    int count_old = 0;
    for (auto&& result : qs2.where(field<^^Person::age>() >= 30).rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_GE(result.value().age, 30);
        ++count_old;
    }

    EXPECT_EQ(count_young + count_old, 25);
}

TYPED_TEST(RowsTest, CollectToVector) {
    QuerySet<Person, TypeParam> qs;
    std::vector<Person>         people;
    for (auto&& result : qs.template order_by<^^Person::name>().rows()) {
        ASSERT_TRUE(result.has_value());
        people.push_back(std::move(result.value()));
    }
    EXPECT_EQ(people.size(), 25);
    for (size_t i = 1; i < people.size(); ++i) {
        EXPECT_LE(people[i - 1].name, people[i].name);
    }
}

// JOIN test
template <typename ConnType> class RowsJoinTest : public StormTestFixture<Message, ConnType> {
  protected:
    auto on_after_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        storm::test::ensure_table<Person, ConnType>(conn);
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));

        QuerySet<Person, ConnType> pqs;
        auto                       people_result = pqs.template order_by<^^Person::name>().select().execute();
        ASSERT_TRUE(people_result.has_value());
        std::array<int, 4> sender_ids{};
        for (const auto& p : people_result.value()) {
            if (p.name == "Alice")
                sender_ids[0] = p.id;
            else if (p.name == "Bob")
                sender_ids[1] = p.id;
            else if (p.name == "Charlie")
                sender_ids[2] = p.id;
            else if (p.name == "Diana")
                sender_ids[3] = p.id;
        }
        std::vector<Message> const msgs = {
                {.content = "Hello", .value = 10, .sender = {.id = sender_ids[0]}},
                {.content = "World", .value = 20, .sender = {.id = sender_ids[0]}},
                {.content = "Hi there", .value = 30, .sender = {.id = sender_ids[0]}},
                {.content = "Goodbye", .value = 40, .sender = {.id = sender_ids[1]}},
                {.content = "See ya", .value = 50, .sender = {.id = sender_ids[1]}},
                {.content = "Later", .value = 60, .sender = {.id = sender_ids[2]}},
                {.content = "Bye", .value = 70, .sender = {.id = sender_ids[2]}},
                {.content = "Peace", .value = 80, .sender = {.id = sender_ids[3]}},
        };
        ASSERT_TRUE((storm::test::batch_insert<Message, ConnType>(msgs)));
    }
};

TYPED_TEST_SUITE(RowsJoinTest, DatabaseTypes);

TYPED_TEST(RowsJoinTest, WithJoin) {
    QuerySet<Message, TypeParam> qs;
    int                          count = 0;
    for (auto&& result : qs.template join<&Message::sender>().rows()) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_EQ(count, 8);
}

TYPED_TEST(RowsJoinTest, WithJoinAndWhere) {
    QuerySet<Message, TypeParam> qs;
    int                          count = 0;
    for (auto&& result : qs.template join<&Message::sender>().where(field<^^Message::value>() > 30).rows()) {
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result.value().value, 30);
        ++count;
    }
    EXPECT_GT(count, 0);
}

// Static checks: generator satisfies range and view concepts
static_assert(std::movable<storm::generator<int>>);
static_assert(std::ranges::range<storm::generator<int>>);
static_assert(std::ranges::view<storm::generator<int>>);

// Ranges/views compatibility tests
TYPED_TEST(RowsTest, ViewsFilterTransform) {
    QuerySet<Person, TypeParam> qs;
    std::vector<std::string>    names;
    for (auto&& name : qs.rows() | std::views::filter([](auto&& r) { return r.has_value(); }) |
                               std::views::transform([](auto&& r) { return r.value().name; })) {
        names.push_back(std::move(name));
    }
    EXPECT_EQ(names.size(), 25);
}

TYPED_TEST(RowsTest, ViewsTake) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.rows() | std::views::take(7)) {
        ASSERT_TRUE(result.has_value());
        ++count;
    }
    EXPECT_EQ(count, 7);
}

TYPED_TEST(RowsTest, ViewsFilterCount) {
    QuerySet<Person, TypeParam> qs;
    int                         count = 0;
    for (auto&& result : qs.rows() | std::views::filter([](auto&& r) { return r.has_value() && r.value().age > 35; })) {
        EXPECT_GT(result.value().age, 35);
        ++count;
    }
    EXPECT_GT(count, 0);
    EXPECT_LT(count, 25);
}

TYPED_TEST(RowsTest, ViewsFilterWhereTransformCollect) {
    QuerySet<Person, TypeParam> qs;
    std::vector<int>            ages;
    for (auto&& age : qs.where(field<^^Person::is_active>() == true).rows() | std::views::filter([](auto&& r) {
                          return r.has_value();
                      }) | std::views::transform([](auto&& r) { return r.value().age; })) {
        ages.push_back(age);
    }
    EXPECT_GT(ages.size(), 0);
    EXPECT_LE(ages.size(), 25);
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
