#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

using storm::QuerySet;
using storm::orm::where::f;

#include "test_models.h"     // NOSONAR cpp:S954
#include "test_m2m_models.h" // NOSONAR cpp:S954

// ============================================================================
// Phase 2 (#203): explicit junction model — many_to_many_through<Enrollment>
// Pupil ⟷ Course through Enrollment (with `grade` metadata).
// ============================================================================

template <typename ConnType> class M2MThroughTest : public StormTestFixture<Pupil, ConnType, Course, Enrollment> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
        QuerySet<Pupil, ConnType> pqs;
        std::vector<Pupil> const  pupils = {{.name = "Dora", .age = 11}, {.name = "Eli", .age = 12}};
        ASSERT_TRUE(pqs.insert(std::span<const Pupil>(pupils)).execute().has_value());

        QuerySet<Course, ConnType> cqs;
        std::vector<Course> const  courses = {{.title = "Math"}, {.title = "Art"}};
        ASSERT_TRUE(cqs.insert(std::span<const Course>(courses)).execute().has_value());

        // Junction rows are plain Enrollment inserts — the through model is a model.
        QuerySet<Enrollment, ConnType> eqs;
        std::vector<Enrollment> const  enrollments = {
                {.pupil = {.id = 1}, .course = {.id = 1}, .grade = "A"},
                {.pupil = {.id = 1}, .course = {.id = 2}, .grade = "B"},
                {.pupil = {.id = 2}, .course = {.id = 1}, .grade = "C"},
        };
        ASSERT_TRUE(eqs.insert(std::span<const Enrollment>(enrollments)).execute().has_value());
    }
};

TYPED_TEST_SUITE(M2MThroughTest, DatabaseTypes);

TYPED_TEST(M2MThroughTest, ThroughSqlUsesJunctionModel) {
    QuerySet<Pupil, TypeParam> qs;
    auto                       sql = qs.template join<^^Pupil::courses>().select().sql();
    // Two-query shape (#391): junction = the through model's table; FK columns
    // come from its field names. Q1 selects the base pupils; Q2 the junction⋈course.
    EXPECT_TRUE(sql.contains("SELECT id, name, age FROM Pupil; ")) << sql;
    EXPECT_TRUE(sql.contains("FROM Enrollment t2 INNER JOIN Course t3 ON t2.course_id = t3.id")) << sql;
    EXPECT_TRUE(sql.contains("WHERE t2.pupil_id IN (SELECT id FROM Pupil)")) << sql;
}

TYPED_TEST(M2MThroughTest, ThroughEagerLoadIgnoresMetadata) {
    QuerySet<Pupil, TypeParam> qs;
    auto                       rows = qs.template join<^^Pupil::courses>().select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 2U);

    for (const auto& pupil : *rows) {
        if (pupil.name == "Dora") {
            ASSERT_EQ(pupil.courses.size(), 2U);
            EXPECT_EQ(pupil.courses[0].title, "Math");
            EXPECT_EQ(pupil.courses[1].title, "Art");
        } else {
            EXPECT_EQ(pupil.name, "Eli");
            ASSERT_EQ(pupil.courses.size(), 1U);
            EXPECT_EQ(pupil.courses[0].title, "Math");
        }
    }
}

TYPED_TEST(M2MThroughTest, ThroughWithWhereOrderAndLimit) {
    QuerySet<Pupil, TypeParam> qs;
    auto                       rows = qs.template join<^^Pupil::courses>()
                        .where(f<^^Pupil::age>() >= 11)
                        .template order_by<^^Pupil::name, false>()
                        .limit(1)
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Eli"); // DESC → Eli first
    EXPECT_EQ(rows->begin()->courses.size(), 1U);
}

// Metadata access: query the junction model directly with the existing FK join API.
TYPED_TEST(M2MThroughTest, JunctionModelQueriedDirectlyForMetadata) {
    QuerySet<Enrollment, TypeParam> qs;
    auto                            rows = qs.template join<^^Enrollment::pupil, ^^Enrollment::course>()
                        .where(f<^^Enrollment::grade>() == "B")
                        .select()
                        .execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->grade, "B");
    EXPECT_EQ(rows->begin()->pupil.name, "Dora");
    EXPECT_EQ(rows->begin()->course.title, "Art");
}

// NOLINTEND(misc-const-correctness)
