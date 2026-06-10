#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

using storm::QuerySet;
using storm::orm::where::field;

#include "test_models.h"     // NOSONAR cpp:S954
#include "test_m2m_models.h" // NOSONAR cpp:S954

// ============================================================================
// M2M joins with WHERE / ORDER BY / LIMIT / OFFSET / first / get / rows (#203)
// Seed: Alice(20)→[Math, Physics], Bob(22)→[Math], Carol(25)→[].
// ============================================================================

template <typename ConnType> class M2MModifierTest : public StormTestFixture<Student, ConnType, Course> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
        storm::test::seed_students<ConnType>();
    }

    // Joined SELECT with a WHERE expression — shared by the operator tests.
    auto names_matching(storm::orm::where::ExpressionVariantPtr expr) -> std::vector<std::string> {
        QuerySet<Student, ConnType> qs;
        auto rows = qs.template join<^^Student::courses>().where(std::move(expr)).select().execute();
        EXPECT_TRUE(rows.has_value());
        std::vector<std::string> names;
        for (const auto& s : *rows) {
            names.push_back(s.name);
        }
        std::ranges::sort(names);
        return names;
    }
};

TYPED_TEST_SUITE(M2MModifierTest, DatabaseTypes);

using Names = std::vector<std::string>;

TYPED_TEST(M2MModifierTest, WhereFiltersBaseEntities) {
    QuerySet<Student, TypeParam> qs;
    auto rows = qs.template join<^^Student::courses>().where(field<^^Student::age>() >= 22).select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    // Carol matches the WHERE but has no courses → dropped by INNER join
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Bob");
    ASSERT_EQ(rows->begin()->courses.size(), 1U);
    EXPECT_EQ(rows->begin()->courses[0].title, "Math");
}

TYPED_TEST(M2MModifierTest, WhereAllSixComparisonOperators) {
    EXPECT_EQ(this->names_matching(field<^^Student::age>() == 20), (Names{"Alice"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>() != 20), (Names{"Bob"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>() > 20), (Names{"Bob"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>() >= 20), (Names{"Alice", "Bob"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>() < 22), (Names{"Alice"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>() <= 22), (Names{"Alice", "Bob"}));
}

TYPED_TEST(M2MModifierTest, WhereSpecialExpressionsAndLogic) {
    EXPECT_EQ(this->names_matching(field<^^Student::age>().in(20, 25)), (Names{"Alice"}));
    EXPECT_EQ(this->names_matching(field<^^Student::age>().between(19, 21)), (Names{"Alice"}));
    EXPECT_EQ(this->names_matching(field<^^Student::name>().like("B%")), (Names{"Bob"}));
    EXPECT_EQ(
            this->names_matching(field<^^Student::name>().like("A%") || field<^^Student::age>() == 22),
            (Names{"Alice", "Bob"})
    );
    EXPECT_EQ(
            this->names_matching(
                    (field<^^Student::age>() >= 20 && field<^^Student::age>() < 22) || field<^^Student::name>() == "Bob"
            ),
            (Names{"Alice", "Bob"})
    );
}

TYPED_TEST(M2MModifierTest, OrderByOrdersStudents) {
    QuerySet<Student, TypeParam> qs;
    auto rows = qs.template join<^^Student::courses>().template order_by<^^Student::name, false>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 2U);
    auto it = rows->begin();
    EXPECT_EQ(it->name, "Bob"); // DESC: Bob before Alice
    ++it;
    EXPECT_EQ(it->name, "Alice");
    EXPECT_EQ(it->courses.size(), 2U); // full collection survives ordering
}

TYPED_TEST(M2MModifierTest, LimitLimitsStudentsNotJoinedRows) {
    QuerySet<Student, TypeParam> qs;
    auto rows = qs.template join<^^Student::courses>().template order_by<^^Student::name>().limit(1).select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    // LIMIT 1 = one STUDENT (a flat outer LIMIT would have cut Alice's courses)
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Alice");
    EXPECT_EQ(rows->begin()->courses.size(), 2U);
}

TYPED_TEST(M2MModifierTest, OffsetSkipsStudents) {
    QuerySet<Student, TypeParam> qs;
    auto                         rows = qs.template join<^^Student::courses>()
                        .template order_by<^^Student::name>()
                        .limit(1)
                        .offset(1)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Bob");
    EXPECT_EQ(rows->begin()->courses.size(), 1U);
}

TYPED_TEST(M2MModifierTest, FirstReturnsCompleteEntity) {
    QuerySet<Student, TypeParam> qs;
    auto first = qs.template join<^^Student::courses>().template order_by<^^Student::name>().first().execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((*first)->name, "Alice");
    EXPECT_EQ((*first)->courses.size(), 2U); // LIMIT 1 applies to students, not rows
}

TYPED_TEST(M2MModifierTest, FirstOnEmptyReturnsNullopt) {
    QuerySet<Student, TypeParam> qs;
    auto first = qs.template join<^^Student::courses>().where(field<^^Student::age>() > 99).first().execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_FALSE(first->has_value());
}

TYPED_TEST(M2MModifierTest, GetReturnsExactlyOne) {
    QuerySet<Student, TypeParam> qs;
    auto got = qs.template join<^^Student::courses>().where(field<^^Student::name>() == "Bob").get().execute();
    ASSERT_TRUE(got.has_value()) << got.error().message();
    EXPECT_EQ(got->name, "Bob");
    ASSERT_EQ(got->courses.size(), 1U);
    EXPECT_EQ(got->courses[0].title, "Math");
}

TYPED_TEST(M2MModifierTest, GetAggregatesMultipleRelationsIntoOneEntity) {
    QuerySet<Student, TypeParam> qs;
    // Alice has TWO courses — two joined rows must aggregate into ONE entity,
    // not trip the "multiple rows" uniqueness check.
    auto got = qs.template join<^^Student::courses>().where(field<^^Student::name>() == "Alice").get().execute();
    ASSERT_TRUE(got.has_value()) << got.error().message();
    EXPECT_EQ(got->name, "Alice");
    EXPECT_EQ(got->courses.size(), 2U);
}

TYPED_TEST(M2MModifierTest, GetZeroRowsIsError) {
    QuerySet<Student, TypeParam> qs;
    auto got = qs.template join<^^Student::courses>().where(field<^^Student::name>() == "Zed").get().execute();
    ASSERT_FALSE(got.has_value());
    EXPECT_TRUE(got.error().message().contains("No row found"));
}

TYPED_TEST(M2MModifierTest, GetMultipleRowsIsError) {
    QuerySet<Student, TypeParam> qs;
    auto                         got = qs.template join<^^Student::courses>().get().execute();
    ASSERT_FALSE(got.has_value());
    EXPECT_TRUE(got.error().message().contains("Multiple rows found"));
}

TYPED_TEST(M2MModifierTest, RowsGeneratorYieldsAggregatedEntities) {
    QuerySet<Student, TypeParam> qs;
    auto                         joined = qs.template join<^^Student::courses>().template order_by<^^Student::name>();

    std::vector<Student> yielded;
    for (auto&& row : joined.rows()) {
        ASSERT_TRUE(row.has_value()) << row.error().message();
        yielded.push_back(std::move(*row));
    }
    ASSERT_EQ(yielded.size(), 2U);
    EXPECT_EQ(yielded[0].name, "Alice");
    EXPECT_EQ(yielded[0].courses.size(), 2U);
    EXPECT_EQ(yielded[1].name, "Bob");
    EXPECT_EQ(yielded[1].courses.size(), 1U);
}

TYPED_TEST(M2MModifierTest, RowsGeneratorOnEmptyYieldsNothing) {
    QuerySet<Student, TypeParam> qs;
    auto                         joined = qs.template join<^^Student::courses>().where(field<^^Student::age>() > 99);

    std::size_t count = 0;
    for (auto&& row : joined.rows()) {
        ASSERT_TRUE(row.has_value());
        ++count;
    }
    EXPECT_EQ(count, 0U);
}

TYPED_TEST(M2MModifierTest, RepeatedQueryUsesStatementCache) {
    QuerySet<Student, TypeParam> qs;
    auto                         joined    = qs.template join<^^Student::courses>();
    auto                         first_run = joined.select().execute();
    ASSERT_TRUE(first_run.has_value()) << first_run.error().message();
    auto second_run = joined.select().execute(); // same SQL → cached statement
    ASSERT_TRUE(second_run.has_value()) << second_run.error().message();
    EXPECT_EQ(first_run->size(), second_run->size());
    auto* alice = [&]() -> Student* {
        for (auto& s : *second_run) {
            if (s.name == "Alice") {
                return &s;
            }
        }
        return nullptr;
    }();
    ASSERT_NE(alice, nullptr);
    EXPECT_EQ(alice->courses.size(), 2U); // re-extraction appends into a FRESH object
}

TYPED_TEST(M2MModifierTest, JoinIsImmutableOnBaseQuerySet) {
    QuerySet<Student, TypeParam> qs;
    auto                         joined = qs.template join<^^Student::courses>();
    (void)joined;
    // base QuerySet is unaffected by the join (Django-style immutability)
    auto plain = qs.select().execute();
    ASSERT_TRUE(plain.has_value()) << plain.error().message();
    EXPECT_EQ(plain->size(), 3U); // Carol included; no aggregation
    for (const auto& s : *plain) {
        EXPECT_TRUE(s.courses.empty());
    }
}

// NOLINTEND(misc-const-correctness)
