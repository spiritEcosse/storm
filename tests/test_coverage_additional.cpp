/**
 * @file test_coverage_additional.cpp
 * @brief Additional tests specifically targeting remaining coverage gaps
 *
 * This file tests code paths not covered by existing tests:
 * - Multiple aggregate operations returning tuples
 * - COUNT_DISTINCT aggregate
 * - JOIN type extractions (float, bool, optional)
 * - execute_single_optimized in UpdateStatement
 * - Edge cases for aggregate empty table handling
 */

#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTBEGIN(readability-identifier-length)

import storm;

import <expected>;
import <string>;
import <vector>;
import <optional>;
import <tuple>;
import <cstdint>;

#include "test_models.h"

using namespace storm;

// =============================================================================
// Multiple Aggregates Test Fixture
// =============================================================================

template <typename ConnType> class MultipleAggregatesTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value()));

        qs = std::make_unique<QuerySet<Person, ConnType>>();

        // Insert test data with varied values
        std::vector<Person> const people = {
                Person{.name = "Alice", .age = 25, .salary = 50000.0, .score = std::optional<int>(85)},
                Person{.name = "Bob", .age = 30, .salary = 60000.0, .score = std::optional<int>(90)},
                Person{.name = "Charlie", .age = 35, .salary = 70000.0, .score = std::optional<int>(75)},
                Person{.name = "Diana", .age = 28, .salary = 55000.0, .score = std::optional<int>(95)},
                Person{.name = "Eve", .age = 40, .salary = 80000.0, .score = std::optional<int>(80)},
        };

        for (const auto& person : people) {
            auto r = qs->insert(person).execute();
            ASSERT_TRUE(r.has_value());
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<Person, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<Person, ConnType>> qs;
};

TYPED_TEST_SUITE(MultipleAggregatesTest, DatabaseTypes);

// =============================================================================
// Multiple Aggregates Tests (returns tuples)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, TwoAggregatesCountAndSum) {
    // Tests tuple return type with 2 operations
    auto result = this->qs->count().template sum<^^Person::age>().get();

    ASSERT_TRUE(result.has_value()) << "Two aggregates should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 25 + 30 + 35 + 28 + 40); // 158
}

TYPED_TEST(MultipleAggregatesTest, TwoAggregatesSumAndAvg) {
    // Tests tuple with SUM (int64_t) and AVG (double)
    auto result = this->qs->template sum<^^Person::score>().template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value()) << "SUM + AVG should succeed";

    auto [sum, avg] = result.value();
    EXPECT_EQ(sum, 85 + 90 + 75 + 95 + 80); // 425
    EXPECT_NEAR(avg, (50000.0 + 60000.0 + 70000.0 + 55000.0 + 80000.0) / 5.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, ThreeAggregates) {
    // Tests tuple with 3 operations
    auto result = this->qs->count().template sum<^^Person::age>().template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value()) << "Three aggregates should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, FourAggregates) {
    // Tests tuple with 4 operations
    auto result = this->qs->count()
                          .template sum<^^Person::age>()
                          .template min<^^Person::score>()
                          .template max<^^Person::score>()
                          .get();

    ASSERT_TRUE(result.has_value()) << "Four aggregates should succeed";

    auto [count, sum, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_EQ(min, 75.0); // MIN returns double
    EXPECT_EQ(max, 95.0); // MAX returns double
}

TYPED_TEST(MultipleAggregatesTest, FiveAggregatesAllTypes) {
    // Tests all 5 aggregate types together
    auto result = this->qs->count()
                          .template sum<^^Person::age>()
                          .template avg<^^Person::salary>()
                          .template min<^^Person::score>()
                          .template max<^^Person::score>()
                          .get();

    ASSERT_TRUE(result.has_value()) << "Five aggregates should succeed";

    auto [count, sum, avg, min, max] = result.value();
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 158);
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(min, 75.0);
    EXPECT_EQ(max, 95.0);
}

TYPED_TEST(MultipleAggregatesTest, MultipleAggregatesEmptyTable) {
    // Test multiple aggregates on empty table (default/zero values)
    // First delete all rows
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->count().template sum<^^Person::age>().template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value()) << "Aggregates on empty table should succeed";

    auto [count, sum, avg] = result.value();
    EXPECT_EQ(count, 0);
    EXPECT_EQ(sum, 0);
    // AVG of empty set should be 0.0 or NULL depending on implementation
}

// =============================================================================
// Single Aggregate Edge Cases
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, SingleCountEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->count().get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(MultipleAggregatesTest, SingleSumEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value());
    // SUM of empty set is 0 or NULL
}

TYPED_TEST(MultipleAggregatesTest, SingleAvgEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->template avg<^^Person::salary>().get();
    ASSERT_TRUE(result.has_value());
    // AVG of empty set
}

TYPED_TEST(MultipleAggregatesTest, SingleMinEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->template min<^^Person::score>().get();
    ASSERT_TRUE(result.has_value());
}

TYPED_TEST(MultipleAggregatesTest, SingleMaxEmptyTable) {
    // Delete all rows first
    auto all = this->qs->select().execute();
    ASSERT_TRUE(all.has_value());
    for (const auto& p : all.value()) {
        (void)this->qs->remove(p).execute();
    }

    auto result = this->qs->template max<^^Person::score>().get();
    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// Single Aggregate as First in Chain (Coverage for avg/min/max entry points)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, SingleAvgWithData) {
    // Test avg() as the FIRST aggregate call (not chained after count/sum)
    auto result = this->qs->template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value()) << "Single AVG should succeed";
    EXPECT_NEAR(result.value(), 63000.0, 0.01); // (50000+60000+70000+55000+80000)/5
}

TYPED_TEST(MultipleAggregatesTest, SingleMinWithData) {
    // Test min() as the FIRST aggregate call
    auto result = this->qs->template min<^^Person::score>().get();

    ASSERT_TRUE(result.has_value()) << "Single MIN should succeed";
    EXPECT_EQ(result.value(), 75.0); // Charlie has lowest score
}

TYPED_TEST(MultipleAggregatesTest, SingleMaxWithData) {
    // Test max() as the FIRST aggregate call
    auto result = this->qs->template max<^^Person::score>().get();

    ASSERT_TRUE(result.has_value()) << "Single MAX should succeed";
    EXPECT_EQ(result.value(), 95.0); // Diana has highest score
}

// =============================================================================
// Chaining FROM avg/min/max (Coverage for AggregateStatement::avg/min/max)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, AvgThenCount) {
    // Chain count() AFTER avg() - tests AggregateStatement<..., AVG>::count()
    auto result = this->qs->template avg<^^Person::salary>().count().get();

    ASSERT_TRUE(result.has_value()) << "AVG then COUNT should succeed";

    auto [avg, count] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(count, 5);
}

TYPED_TEST(MultipleAggregatesTest, MinThenSum) {
    // Chain sum() AFTER min() - tests AggregateStatement<..., MIN>::sum()
    auto result = this->qs->template min<^^Person::score>().template sum<^^Person::age>().get();

    ASSERT_TRUE(result.has_value()) << "MIN then SUM should succeed";

    auto [min, sum] = result.value();
    EXPECT_EQ(min, 75.0);
    EXPECT_EQ(sum, 158); // 25+30+35+28+40
}

TYPED_TEST(MultipleAggregatesTest, MaxThenAvg) {
    // Chain avg() AFTER max() - tests AggregateStatement<..., MAX>::avg()
    auto result = this->qs->template max<^^Person::score>().template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value()) << "MAX then AVG should succeed";

    auto [max, avg] = result.value();
    EXPECT_EQ(max, 95.0);
    EXPECT_NEAR(avg, 63000.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, AvgThenMin) {
    // Chain min() AFTER avg() - tests AggregateStatement<..., AVG>::min()
    auto result = this->qs->template avg<^^Person::salary>().template min<^^Person::age>().get();

    ASSERT_TRUE(result.has_value()) << "AVG then MIN should succeed";

    auto [avg, min] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(min, 25.0); // Alice is youngest
}

TYPED_TEST(MultipleAggregatesTest, AvgThenMax) {
    // Chain max() AFTER avg() - tests AggregateStatement<..., AVG>::max()
    auto result = this->qs->template avg<^^Person::salary>().template max<^^Person::age>().get();

    ASSERT_TRUE(result.has_value()) << "AVG then MAX should succeed";

    auto [avg, max] = result.value();
    EXPECT_NEAR(avg, 63000.0, 0.01);
    EXPECT_EQ(max, 40.0); // Eve is oldest
}

TYPED_TEST(MultipleAggregatesTest, MinThenMax) {
    // Chain max() AFTER min() - tests range query pattern
    auto result = this->qs->template min<^^Person::age>().template max<^^Person::age>().get();

    ASSERT_TRUE(result.has_value()) << "MIN then MAX should succeed";

    auto [min, max] = result.value();
    EXPECT_EQ(min, 25.0); // Alice
    EXPECT_EQ(max, 40.0); // Eve
}

TYPED_TEST(MultipleAggregatesTest, MaxThenMin) {
    // Chain min() AFTER max() - reverse order
    auto result = this->qs->template max<^^Person::score>().template min<^^Person::score>().get();

    ASSERT_TRUE(result.has_value()) << "MAX then MIN should succeed";

    auto [max, min] = result.value();
    EXPECT_EQ(max, 95.0); // Diana
    EXPECT_EQ(min, 75.0); // Charlie
}

// =============================================================================
// Aggregate with JOIN Tests
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, MultipleAggregatesWithJoin) {
    // Set up FK tables using SchemaStatement (pg_schema_init already run in SetUp)
    const auto& conn = QuerySet<Person, TypeParam>::get_default_connection();
    (void)storm::orm::schema::SchemaStatement<Message>::create_table_if_not_exists(conn);

    // Insert additional users for JOIN (Person table already created in SetUp)
    QuerySet<Person, TypeParam> user_qs;
    std::ignore = user_qs.insert(Person{.name      = "User1",
                                        .salary    = 4.5,
                                        .is_active = true,
                                        .score     = std::optional<int>(100),
                                        .nickname  = std::optional<std::string>("Bio1")})
                          .execute();
    std::ignore = user_qs.insert(Person{.name = "User2", .salary = 3.8}).execute();

    // Insert messages
    QuerySet<Message, TypeParam> msg_qs;
    std::ignore = msg_qs.insert(Message{.content = "Post1", .value = 100, .sender = {.id = 1}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "Post2", .value = 200, .sender = {.id = 1}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "Post3", .value = 50, .sender = {.id = 2}}).execute();

    // Test multiple aggregates with JOIN
    auto result = msg_qs.template join<&Message::sender>().count().template sum<^^Message::value>().get();

    ASSERT_TRUE(result.has_value()) << "Multiple aggregates with JOIN should succeed";

    auto [count, sum] = result.value();
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 350); // 100 + 200 + 50
}

// =============================================================================
// JOIN Type Extraction Tests (float, bool, optional)
// =============================================================================

template <typename ConnType> class JoinTypeExtractionTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value()));
        ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()));

        person_qs = std::make_unique<QuerySet<Person, ConnType>>();
        msg_qs    = std::make_unique<QuerySet<Message, ConnType>>();

        // Insert test users with various type values
        // JoinUser field mapping: rating->salary, active->is_active, opt_score->score, opt_bio->nickname
        std::ignore = person_qs
                              ->insert(
                                      Person{.name      = "Alice",
                                             .salary    = 4.5,
                                             .is_active = true,
                                             .score     = std::optional<int>(100),
                                             .nickname  = std::optional<std::string>("Alice bio")}
                              )
                              .execute();
        std::ignore = person_qs->insert(Person{.name = "Bob", .salary = 3.2}).execute();
        std::ignore = person_qs
                              ->insert(
                                      Person{.name      = "Charlie",
                                             .salary    = 4.9,
                                             .is_active = true,
                                             .score     = std::optional<int>(95)}
                              )
                              .execute();

        // Insert messages (JoinPost field mapping: title->content, author->sender, views->value)
        std::ignore = msg_qs->insert(Message{.content = "Post by Alice", .value = 100, .sender = {.id = 1}}).execute();
        std::ignore =
                msg_qs->insert(Message{.content = "Another Alice post", .value = 200, .sender = {.id = 1}}).execute();
        std::ignore = msg_qs->insert(Message{.content = "Post by Bob", .value = 50, .sender = {.id = 2}}).execute();
        std::ignore = msg_qs->insert(Message{.content = "Charlie post", .value = 150, .sender = {.id = 3}}).execute();
    }

    auto TearDown() -> void override {
        person_qs = nullptr;
        msg_qs    = nullptr;
        StormTestFixture<Person, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<Person, ConnType>>  person_qs;
    std::unique_ptr<QuerySet<Message, ConnType>> msg_qs;
};

TYPED_TEST_SUITE(JoinTypeExtractionTest, DatabaseTypes);

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsFloatField) {
    // Tests extract_typed_field for float
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with float field should succeed";
    ASSERT_FALSE(result.value().empty());

    // Check that float values are extracted correctly
    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Alice") {
            EXPECT_NEAR(msg.sender.salary, 4.5, 0.01);
        } else if (msg.sender.name == "Bob") {
            EXPECT_NEAR(msg.sender.salary, 3.2, 0.01);
        } else if (msg.sender.name == "Charlie") {
            EXPECT_NEAR(msg.sender.salary, 4.9, 0.01);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsBoolField) {
    // Tests extract_typed_field for bool
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with bool field should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Alice") {
            EXPECT_TRUE(msg.sender.is_active);
        } else if (msg.sender.name == "Bob") {
            EXPECT_FALSE(msg.sender.is_active);
        } else if (msg.sender.name == "Charlie") {
            EXPECT_TRUE(msg.sender.is_active);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalIntWithValue) {
    // Tests extract_typed_field for optional<int> with value
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with optional int should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Alice") {
            ASSERT_TRUE(msg.sender.score.has_value());
            EXPECT_EQ(msg.sender.score.value(), 100);
        } else if (msg.sender.name == "Charlie") {
            ASSERT_TRUE(msg.sender.score.has_value());
            EXPECT_EQ(msg.sender.score.value(), 95);
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalIntNull) {
    // Tests extract_typed_field for optional<int> with NULL
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Bob") {
            EXPECT_FALSE(msg.sender.score.has_value());
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalStringWithValue) {
    // Tests extract_typed_field for optional<string> with value
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Alice") {
            ASSERT_TRUE(msg.sender.nickname.has_value());
            EXPECT_EQ(msg.sender.nickname.value(), "Alice bio");
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinExtractsOptionalStringNull) {
    // Tests extract_typed_field for optional<string> with NULL
    auto result = this->msg_qs->template join<&Message::sender>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with NULL optional string should succeed";
    ASSERT_FALSE(result.value().empty());

    for (const auto& msg : result.value()) {
        if (msg.sender.name == "Bob" || msg.sender.name == "Charlie") {
            EXPECT_FALSE(msg.sender.nickname.has_value());
        }
    }
}

TYPED_TEST(JoinTypeExtractionTest, JoinWithWhere) {
    // Test JOIN with WHERE to ensure proper handling
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 75)
                          .template join<&Message::sender>()
                          .select()
                          .execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with WHERE should succeed";
    // Should return posts with views > 75 (Alice's posts with 100/200 and Charlie's with 150)
    EXPECT_EQ(result.value().size(), 3);
}

TYPED_TEST(JoinTypeExtractionTest, JoinWithOrderBy) {
    // Test JOIN with ORDER BY
    auto result =
            this->msg_qs->template join<&Message::sender>().template order_by<^^Message::value>().select().execute();

    ASSERT_TRUE(result.has_value()) << "JOIN with ORDER BY should succeed";
    EXPECT_EQ(result.value().size(), 4);

    // Verify ordering (ascending by default)
    auto it         = result.value().begin();
    int  prev_value = 0;
    while (it != result.value().end()) {
        EXPECT_GE(it->value, prev_value);
        prev_value = it->value;
        ++it;
    }
}

// =============================================================================
// Update execute_single_optimized Coverage
// =============================================================================

template <typename ConnType> class UpdateOptimizedTest : public StormTestFixture<SimpleRecord, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<SimpleRecord, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<SimpleRecord, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<SimpleRecord, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<SimpleRecord, ConnType>> qs;
};

TYPED_TEST_SUITE(UpdateOptimizedTest, DatabaseTypes);

TYPED_TEST(UpdateOptimizedTest, RepeatedSingleUpdates) {
    // Test repeated single updates to exercise cached statement path
    SimpleRecord p{0, "Test", 100};
    auto         insert_result = this->qs->insert(p).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Perform multiple single updates (exercises cached_update_stmt_ path)
    for (int i = 0; i < 10; ++i) {
        SimpleRecord updated{static_cast<int>(id), "Updated" + std::to_string(i), 100 + i};
        auto         result = this->qs->update(updated).execute();
        ASSERT_TRUE(result.has_value()) << "Update iteration " << i << " should succeed";
    }

    // Verify final state
    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->value, 109);
}

TYPED_TEST(UpdateOptimizedTest, BatchUpdateFollowedByInsert) {
    // Test batch update followed by insert (different code paths)
    std::vector<SimpleRecord> batch = {
            {0, "P1", 1},
            {0, "P2", 2},
            {0, "P3", 3},
    };

    auto insert_result = this->qs->insert(std::span<const SimpleRecord>(batch)).execute();
    ASSERT_TRUE(insert_result.has_value());

    // Batch update
    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_FALSE(selected.value().empty());

    std::vector<SimpleRecord> updates;
    for (const auto& p : selected.value()) {
        updates.push_back({p.id, p.name, p.value + 100});
    }

    auto batch_update = this->qs->update(std::span<const SimpleRecord>(updates)).execute();
    ASSERT_TRUE(batch_update.has_value());

    // Verify batch update worked
    auto after_batch = this->qs->select().execute();
    ASSERT_TRUE(after_batch.has_value());
    for (const auto& p : after_batch.value()) {
        EXPECT_GE(p.value, 101) << "Values should be updated";
    }

    // Insert a new record after batch update
    SimpleRecord new_person{0, "NewPerson", 500};
    auto         new_insert = this->qs->insert(new_person).execute();
    ASSERT_TRUE(new_insert.has_value());

    // Verify total count
    auto final_result = this->qs->select().execute();
    ASSERT_TRUE(final_result.has_value());
    EXPECT_EQ(final_result.value().size(), 4);
}

// =============================================================================
// Additional WHERE Clause Coverage
// =============================================================================

template <typename ConnType> class WhereAdditionalTest : public StormTestFixture<CovPerson, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<CovPerson, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;

        qs = std::make_unique<QuerySet<CovPerson, ConnType>>();

        // Insert test data (CovAddWherePerson::score maps to CovPerson::salary)
        std::vector<CovPerson> const people = {
                CovPerson{.name = "Alice", .age = 25, .salary = 85.5},
                CovPerson{.name = "Bob", .age = 30, .salary = 90.0},
                CovPerson{.name = "Charlie", .age = 35, .salary = 75.5},
                CovPerson{.name = "Diana", .age = 40, .salary = 95.0},
                CovPerson{.name = "Eve", .age = 28, .salary = 80.0},
        };

        for (const auto& person : people) {
            (void)qs->insert(person).execute();
        }
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<CovPerson, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<CovPerson, ConnType>> qs;
};

TYPED_TEST_SUITE(WhereAdditionalTest, DatabaseTypes);

TYPED_TEST(WhereAdditionalTest, DoubleFieldComparison) {
    // Test WHERE with double field (score)
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::salary>() > 85.0).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice (85.5), Bob (90), Diana (95)
}

TYPED_TEST(WhereAdditionalTest, DoubleFieldEquality) {
    // Test WHERE with double equality
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::salary>() == 90.0).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Bob");
}

TYPED_TEST(WhereAdditionalTest, LikePatternStartsWith) {
    // Test LIKE with starts-with pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::name>().like("A%")).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->name, "Alice");
}

TYPED_TEST(WhereAdditionalTest, LikePatternEndsWith) {
    // Test LIKE with ends-with pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::name>().like("%e")).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3); // Alice, Charlie, Eve
}

TYPED_TEST(WhereAdditionalTest, LikePatternContains) {
    // Test LIKE with contains pattern
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::name>().like("%li%")).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice, Charlie
}

TYPED_TEST(WhereAdditionalTest, ComplexNestedOrAnd) {
    // Test deeply nested OR/AND
    // (age < 28 OR age > 38) AND (score > 80)
    auto age_cond =
            (storm::orm::where::field<^^CovPerson::age>() < 28 or storm::orm::where::field<^^CovPerson::age>() > 38);
    auto score_cond = storm::orm::where::field<^^CovPerson::salary>() > 80.0;
    auto combined   = age_cond and score_cond;

    auto result = this->qs->where(combined).select().execute();

    ASSERT_TRUE(result.has_value());
    // Alice (25, 85.5) and Diana (40, 95) should match
    EXPECT_EQ(result.value().size(), 2);
}

TYPED_TEST(WhereAdditionalTest, WhereWithLessEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() <= 28).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Alice (25), Eve (28)
}

TYPED_TEST(WhereAdditionalTest, WhereWithGreaterEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() >= 35).select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2); // Charlie (35), Diana (40)
}

TYPED_TEST(WhereAdditionalTest, WhereWithNotEqual) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::name>() != "Alice").select().execute();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Everyone except Alice
}

// =============================================================================
// DISTINCT Additional Tests
// =============================================================================

TYPED_TEST(WhereAdditionalTest, DistinctWithWhereAndOrderBy) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::age>() > 25)
                          .template order_by<^^CovPerson::name>()
                          .template distinct<^^CovPerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4); // Bob, Charlie, Diana, Eve
}

TYPED_TEST(WhereAdditionalTest, DistinctMultiFieldWhereOrderBy) {
    auto result = this->qs->where(storm::orm::where::field<^^CovPerson::salary>() >= 80.0)
                          .template order_by<^^CovPerson::age>()
                          .template distinct<^^CovPerson::age, ^^CovPerson::name>()
                          .select();

    ASSERT_TRUE(result.has_value());
}

// =============================================================================
// GROUP BY Additional Aggregate Types
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, GroupByWithAvg) {
    // Test GROUP BY with AVG aggregate (returns double)
    // Group by age ranges (we'll use age directly)
    auto result = this->qs->template group_by<^^Person::age>().template avg<^^Person::salary>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5); // 5 distinct ages
}

TYPED_TEST(MultipleAggregatesTest, GroupByWithMin) {
    auto result = this->qs->template group_by<^^Person::age>().template min<^^Person::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

TYPED_TEST(MultipleAggregatesTest, GroupByWithMax) {
    auto result = this->qs->template group_by<^^Person::age>().template max<^^Person::score>().select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

// =============================================================================
// Aggregate with WHERE Tests
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, AggregateCountWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // Charlie (35), Eve (40)
}

TYPED_TEST(MultipleAggregatesTest, AggregateSumWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template sum<^^Person::age>().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 35 + 40); // 75
}

TYPED_TEST(MultipleAggregatesTest, AggregateAvgWithWhere) {
    auto result =
            this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template avg<^^Person::salary>().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result.value(), (70000.0 + 80000.0) / 2.0, 0.01);
}

TYPED_TEST(MultipleAggregatesTest, AggregateMinWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template min<^^Person::score>().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 75.0); // Charlie has 75
}

TYPED_TEST(MultipleAggregatesTest, AggregateMaxWithWhere) {
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template max<^^Person::score>().get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 80.0); // Eve has 80
}

// =============================================================================
// COUNT_DISTINCT Additional Tests (if supported)
// =============================================================================

TYPED_TEST(MultipleAggregatesTest, CountDistinctViaDistinct) {
    // Test DISTINCT + COUNT combination
    auto distinct_result = this->qs->template distinct<^^Person::age>().select();
    ASSERT_TRUE(distinct_result.has_value());

    // All ages are unique in our test data
    EXPECT_EQ(distinct_result.value().size(), 5);
}

// NOLINTEND(readability-identifier-length)
// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
