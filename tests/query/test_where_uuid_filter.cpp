#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — StormTestFixture, ensure_tables

// ── #609: WHERE-clause filter comparisons against a UUID column still auto-generate on empty ──
//
// #573 fixed the by-KEY WHERE clause (update(obj)/erase(obj), which binds through
// BaseStatement::bind_one_pk_part) so an unset storm::UUID primary key is rejected instead of
// matching zero rows silently. The by-FILTER WHERE path — a .where(...) comparison — had a
// deeper bug: where.cppm's normalize_operand() silently folded a UUID operand into a plain
// std::string (UUID has an implicit operator string_view(), added for an unrelated reason,
// which also matched normalize_operand's text-operand branch), so ComparisonExpr<UUID> /
// InExpression<UUID> were dead code — the value was bound as raw, unvalidated text. Fixed by
// excluding UUID from that fold, and routing WHERE/HAVING comparisons through a new
// bind_filter_value that rejects an empty UUID instead of auto-generating one (auto-generation
// is only meaningful in an INSERT/SET position). Unlike the INSERT-side FK-part gap (#608),
// there is no FK constraint to catch this, and it is not limited to PK columns: any UUID column
// compared against an unset storm::UUID{} had the same symptom.

// NOLINTBEGIN(readability-implicit-bool-conversion)

namespace {

    // A UUID primary key plus a plain (non-PK) UUID column — proves the fix isn't limited to PK
    // columns, unlike #573's by-key path which only ever sees the PK.
    struct UuidFilterDoc {
        [[= storm::primary]] storm::UUID id{};
        storm::UUID                      related_uuid{};
        std::string                      title;
    };

    constexpr std::string_view kDocUuid      = "55555555-5555-4555-8555-555555555555";
    constexpr std::string_view kDocUuid2     = "66666666-6666-4666-8666-666666666666";
    constexpr std::string_view kRelatedUuid  = "77777777-7777-4777-8777-777777777777";
    constexpr std::string_view kRelatedUuid2 = "88888888-8888-4888-8888-888888888888";

    constexpr std::string_view kExpectedFilterError =
            "UUID comparison value must be explicitly set; auto-generation not allowed in a WHERE/HAVING clause";

    template <typename Model, typename ConnType> auto count_rows() -> int {
        storm::QuerySet<Model, ConnType> qs;
        auto                             result = qs.count().execute();
        return result.has_value() ? static_cast<int>(result.value()) : -1;
    }

} // namespace

namespace fields {

    struct UuidFilterDocT;
    consteval {
        std::meta::define_aggregate(^^UuidFilterDocT, storm::field_specs_for(^^UuidFilterDoc));
    }
    inline constexpr UuidFilterDocT UuidFilterDoc{};

} // namespace fields

namespace {

    template <typename ConnType> class UuidFilterDocTest : public StormTestFixture<UuidFilterDoc, ConnType> {
      public:
        // Two rows, not one: proves a rejection isn't just "matched zero rows anyway" and a
        // round-trip test actually discriminates the matching row.
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            storm::QuerySet<UuidFilterDoc, ConnType> qs;
            ASSERT_TRUE(qs.insert(UuidFilterDoc{
                                          .id           = storm::UUID{kDocUuid},
                                          .related_uuid = storm::UUID{kRelatedUuid},
                                          .title        = "Original"
                                  })
                                .execute()
                                .has_value());
            ASSERT_TRUE(qs.insert(UuidFilterDoc{
                                          .id           = storm::UUID{kDocUuid2},
                                          .related_uuid = storm::UUID{kRelatedUuid2},
                                          .title        = "Other"
                                  })
                                .execute()
                                .has_value());
        }
    };

} // namespace
TYPED_TEST_SUITE(UuidFilterDocTest, DatabaseTypes);

// Ordering (>, >=, <, <=) and between() on a UUID column must be compile-time rejections (#609
// review findings): WHERE_CLAUSES.md documents storm::UUID as equality/IN only, and between()
// specifically has no ExpressionVariant<UUID> arm to bind through anyway. Named concepts, not an
// inline requires(), mirror the CanWhereBetween idiom in test_fk_column_name_orderby_aggregate_body.h
// — an inline `requires(V v) { ... }` directly inside static_assert hard-errors on this compiler
// instead of evaluating false (see docs/internals/compiler/COMPILER_ISSUES.md).
template <typename V>
concept CanGreaterUuidId = requires(V v) { fields::UuidFilterDoc.id > v; };
template <typename V>
concept CanLessUuidId = requires(V v) { fields::UuidFilterDoc.id < v; };
template <typename V>
concept CanBetweenUuidId = requires(V v) { fields::UuidFilterDoc.id.between(v, v); };
template <typename V>
concept CanBetweenCollatedUuidId =
        requires(V v) { fields::UuidFilterDoc.id.collate(storm::orm::utilities::Collate::None).between(v, v); };
static_assert(!CanGreaterUuidId<storm::UUID>, "> on a UUID column must be rejected at compile time");
static_assert(!CanLessUuidId<storm::UUID>, "< on a UUID column must be rejected at compile time");
static_assert(!CanBetweenUuidId<storm::UUID>, "between() on a UUID column must be rejected at compile time");
static_assert(!CanBetweenCollatedUuidId<storm::UUID>, "collate().between() on a UUID column must be rejected too");

// The exact reproduction from the issue: a WHERE filter on an unset UUID PK column must ERROR,
// not silently bind a random UUID, match zero rows, and report success.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByEmptyPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == storm::UUID{}).select().execute();
    ASSERT_FALSE(result.has_value()) << "an empty UUID comparison must not silently auto-generate and match nothing";
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// The non-PK column shows the same symptom — the guard cannot be gated on is_primary_member.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByEmptyNonPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.related_uuid == storm::UUID{}).select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// A different comparison operator routes through the identical bind_impl — the rejection must not
// be Equal-specific.
TYPED_TEST(UuidFilterDocTest, NotEqualFilterWithEmptyUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id != storm::UUID{}).select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// erase() via a filter WHERE — the issue's own repro. Must not delete zero rows silently.
TYPED_TEST(UuidFilterDocTest, EraseFilteredByEmptyPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == storm::UUID{}).erase().execute();
    ASSERT_FALSE(result.has_value()) << "must not silently bind a random UUID and delete zero rows";
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
    EXPECT_EQ((count_rows<UuidFilterDoc, TypeParam>()), 2) << "both existing rows must survive";
}

// IN — one empty element among otherwise-valid ones must reject the whole filter.
TYPED_TEST(UuidFilterDocTest, InFilterWithOneEmptyUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id.in(storm::UUID{kDocUuid}, storm::UUID{})).select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// A SET UUID value must still round-trip correctly and discriminate the matching row — the guard
// must not reject legitimate, non-empty comparisons.
TYPED_TEST(UuidFilterDocTest, SelectFilteredBySetPkUuidReturnsMatchingRow) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == storm::UUID{kDocUuid}).select().execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->title, "Original");
}

TYPED_TEST(UuidFilterDocTest, SelectFilteredBySetNonPkUuidReturnsMatchingRow) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.related_uuid == storm::UUID{kRelatedUuid2}).select().execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->title, "Other");
}

// A non-empty but malformed UUID comparison now goes through the same RFC-4122 validation as
// bind_uuid_pk/INSERT (validate_and_bind_uuid_text), rather than binding raw unvalidated text —
// a behavior change on top of the empty-value rejection, pinned here on both backends.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByMalformedUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == storm::UUID{"not-a-uuid"}).select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), "Invalid UUID format: 'not-a-uuid'");
}

// NOLINTEND(readability-implicit-bool-conversion)
