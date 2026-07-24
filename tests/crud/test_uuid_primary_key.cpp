#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

import storm;
import std;

using storm::QuerySet;
using storm::orm::where::f;
using testing::HasSubstr;

#include "test_models.h" // NOSONAR cpp:S954

// ============================================================================
// UUID primary key — CRUD round-trips (#507)
//
// A UUID primary key is caller-supplied (never DB-generated). INSERT with an
// empty PK is rejected; SELECT/UPDATE/DELETE filter on the UUID column. Runs on
// both SQLite (TEXT) and PostgreSQL backends via TYPED_TEST.
// ============================================================================

template <typename ConnType> class UuidPkTest : public StormTestFixture<UuidPkModel, ConnType> {};
TYPED_TEST_SUITE(UuidPkTest, DatabaseTypes);

// A caller-supplied UUID PK inserts and returns void (no RETURNING — the key is
// never DB-generated).
TYPED_TEST(UuidPkTest, InsertUuidPkCallerSupplied) {
    QuerySet<UuidPkModel, TypeParam> qs;
    UuidPkModel const                obj{.id = storm::UUID::generate(), .name = "test"};
    auto                             result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();
}

// An empty (default-constructed) UUID PK is rejected — PKs are never
// auto-generated on the way to the database.
TYPED_TEST(UuidPkTest, InsertUuidPkEmptyFails) {
    QuerySet<UuidPkModel, TypeParam> qs;
    UuidPkModel const                obj{.id = storm::UUID{}, .name = "test"};
    auto                             result = qs.insert(obj).execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_THAT(result.error().message(), HasSubstr("must be explicitly set"));
}

// SELECT filtered on the UUID PK returns the matching row.
TYPED_TEST(UuidPkTest, SelectByUuidPk) {
    QuerySet<UuidPkModel, TypeParam> qs;
    storm::UUID const                id = storm::UUID::generate();
    UuidPkModel const                obj{.id = id, .name = "alice"};
    ASSERT_TRUE(qs.insert(obj).execute().has_value());

    auto results = qs.where(f<^^UuidPkModel::id>() == id).select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->begin()->name, "alice");
    EXPECT_EQ(results->begin()->id, id);
}

// UPDATE filtered on the UUID PK rewrites a non-key column; re-select confirms.
TYPED_TEST(UuidPkTest, UpdateByUuidPk) {
    QuerySet<UuidPkModel, TypeParam> qs;
    storm::UUID const                id = storm::UUID::generate();
    UuidPkModel const                obj{.id = id, .name = "alice"};
    ASSERT_TRUE(qs.insert(obj).execute().has_value());

    auto result = qs.where(f<^^UuidPkModel::id>() == id)
                          .template update<^^UuidPkModel::name>(UuidPkModel{.id = id, .name = "bob"})
                          .execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();

    auto results = qs.where(f<^^UuidPkModel::id>() == id).select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->begin()->name, "bob");
}

// DELETE filtered on the UUID PK removes the row.
TYPED_TEST(UuidPkTest, DeleteByUuidPk) {
    QuerySet<UuidPkModel, TypeParam> qs;
    storm::UUID const                id = storm::UUID::generate();
    UuidPkModel const                obj{.id = id, .name = "alice"};
    ASSERT_TRUE(qs.insert(obj).execute().has_value());

    auto del = qs.where(f<^^UuidPkModel::id>() == id).erase().execute();
    ASSERT_TRUE(del.has_value()) << del.error().message();

    auto results = qs.where(f<^^UuidPkModel::id>() == id).select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();
    EXPECT_EQ(results->size(), 0U);
}

// ============================================================================
// UUID primary key — foreign keys (#507)
//
// UuidPkRef has an FK to the UUID-PK UuidPkModel. INSERT binds the owner's UUID
// into the <name>_id column; a JOIN eager-loads the owner and matches on the
// UUID key.
// ============================================================================

template <typename ConnType> class UuidPkFkTest : public StormTestFixture<UuidPkRef, ConnType, UuidPkModel> {
  public:
    // Inserts one UUID-PK owner and one referrer pointing at it. Returns the
    // owner so the caller can assert on the stitched UUID.
    static auto seed_owner_and_ref() -> UuidPkModel {
        QuerySet<UuidPkModel, ConnType> qs_owner;
        QuerySet<UuidPkRef, ConnType>   qs_ref;

        UuidPkModel const owner{.id = storm::UUID::generate(), .name = "owner"};
        EXPECT_TRUE(qs_owner.insert(owner).execute().has_value());

        UuidPkRef const ref{.id = 0, .owner = owner, .value = "ref_val"};
        EXPECT_TRUE(qs_ref.insert(ref).execute().has_value());
        return owner;
    }
};
TYPED_TEST_SUITE(UuidPkFkTest, DatabaseTypes);

// Inserting a referrer whose FK points at a UUID-PK owner binds the owner's UUID.
TYPED_TEST(UuidPkFkTest, InsertFkToUuidPk) {
    TestFixture::seed_owner_and_ref();
    EXPECT_FALSE(this->HasNonfatalFailure());
}

// A JOIN over the FK eager-loads the UUID-PK owner and stitches on the UUID key.
TYPED_TEST(UuidPkFkTest, JoinFkToUuidPk) {
    UuidPkModel const owner = TestFixture::seed_owner_and_ref();

    QuerySet<UuidPkRef, TypeParam> qs_ref;
    auto                           results = qs_ref.template join<^^UuidPkRef::owner>().select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->begin()->owner.name, "owner");
    EXPECT_EQ(results->begin()->owner.id, owner.id);
}

// ============================================================================
// UUID models alongside many-to-many (#507)
//
// UuidOwnerM2M owns an auto-junction m2m to the integer-PK UuidPkCourse. This
// verifies the m2m eager-load path continues to work for models that live in
// the UUID-PK suite (the owner key here is an integer PK — a UUID-keyed m2m
// owner is a separate concern tracked with the stitch implementation).
// ============================================================================

template <typename ConnType> class UuidOwnerM2MTest : public StormTestFixture<UuidOwnerM2M, ConnType, UuidPkCourse> {};
TYPED_TEST_SUITE(UuidOwnerM2MTest, DatabaseTypes);

// Insert a course + an owner, link them through the auto junction, and verify
// the eager-load aggregates the related course.
TYPED_TEST(UuidOwnerM2MTest, M2MJoinAggregatesCourses) {
    QuerySet<UuidOwnerM2M, TypeParam> qs_owner;
    QuerySet<UuidPkCourse, TypeParam> qs_course;

    auto course_id = qs_course.insert(UuidPkCourse{.id = 0, .title = "Math"}).execute();
    ASSERT_TRUE(course_id.has_value()) << course_id.error().message();

    auto owner_id = qs_owner.insert(UuidOwnerM2M{.id = 0, .name = "student"}).execute();
    ASSERT_TRUE(owner_id.has_value()) << owner_id.error().message();

    auto conn = QuerySet<UuidOwnerM2M, TypeParam>::get_default_connection();
    auto link = conn->execute(
            std::format(
                    "INSERT INTO UuidOwnerM2M_UuidPkCourse (UuidOwnerM2M_id, UuidPkCourse_id) VALUES ({}, {})",
                    owner_id.value(),
                    course_id.value()
            )
    );
    ASSERT_TRUE(link.has_value()) << link.error().message();

    auto results = qs_owner.template join<^^UuidOwnerM2M::courses>().select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();
    ASSERT_EQ(results->size(), 1U);
    ASSERT_EQ(results->begin()->courses.size(), 1U);
    EXPECT_EQ(results->begin()->courses[0].title, "Math");
}
