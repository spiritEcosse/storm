#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

using storm::QuerySet;

#include "test_models.h"     // NOSONAR cpp:S954
#include "test_m2m_models.h" // NOSONAR cpp:S954

// ============================================================================
// Compile-time: m2m fields are excluded from persisted members (#203)
// ============================================================================

static_assert(
        storm::orm::statements::BaseStatement<Student>::field_count_ == 3,
        "courses (m2m) must not count as a persisted column"
);
static_assert(storm::orm::statements::BaseStatement<Course>::field_count_ == 2);

// ============================================================================
// Compile-time: M2MFieldOf concept
// ============================================================================

static_assert(storm::orm::statements::M2MFieldOf<Student, ^^Student::courses>);
static_assert(!storm::orm::statements::M2MFieldOf<Student, ^^Student::name>);
static_assert(!storm::orm::statements::FKFieldOf<Student, ^^Student::courses>);
static_assert(storm::orm::statements::M2MFieldOf<Pupil, ^^Pupil::courses>);

// ============================================================================
// Compile-time: related-type extraction from containers via std::meta
// ============================================================================

static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<Course>>, Course>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<plf::hive<Track>>, Track>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<std::shared_ptr<Track>>>, Track>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::deque<Course>>, Course>);
static_assert(std::same_as<storm::orm::statements::meta::m2m_related_t<std::vector<std::unique_ptr<Course>>>, Course>);

// ============================================================================
// Runtime: CRUD on a model WITH an m2m field ignores the container member
// ============================================================================

template <typename ConnType> class M2MBaseTest : public StormTestFixture<Student, ConnType, Course> {};

TYPED_TEST_SUITE(M2MBaseTest, DatabaseTypes);

TYPED_TEST(M2MBaseTest, PlainCrudIgnoresM2MField) {
    QuerySet<Student, TypeParam> qs;
    Student const                s{.name = "Alice", .age = 20, .courses = {{.id = 99, .title = "ghost"}}};
    auto                         ins = qs.insert(s).execute();
    ASSERT_TRUE(ins.has_value()) << ins.error().message();

    auto rows = qs.select().execute();
    ASSERT_TRUE(rows.has_value()) << rows.error().message();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->begin()->name, "Alice");
    EXPECT_EQ(rows->begin()->age, 20);
    EXPECT_TRUE(rows->begin()->courses.empty()); // plain select never populates m2m
}

// NOLINTEND(misc-const-correctness)
