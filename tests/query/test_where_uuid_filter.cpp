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

    // FK member whose TARGET has a UUID PK (#622 review finding, both reviewers): Field's
    // FieldType for an FK member is the RELATED MODEL STRUCT, not the FK's stored column type
    // (a UUID/TEXT column), so the comparison-side guard must resolve through the same
    // FK-target-PK lookup .in() already used pre-#622 (ComparisonColumnType), not raw FieldType,
    // or this shape stays silently unguarded — `fields::UuidFkChild.owner == "not-a-uuid"` would
    // otherwise bind raw unvalidated text against a UUID column.
    struct UuidFkOwner {
        [[= storm::primary]] storm::UUID id{};
        std::string                      name;
    };

    struct UuidFkChild {
        [[= storm::primary]] int      id{};
        [[= storm::fk<>]] UuidFkOwner owner;
        std::string                   label;
    };

    constexpr std::string_view kOwnerUuid = "99999999-9999-4999-8999-999999999999";

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

    struct UuidFkOwnerT;
    consteval {
        std::meta::define_aggregate(^^UuidFkOwnerT, storm::field_specs_for(^^UuidFkOwner));
    }
    inline constexpr UuidFkOwnerT UuidFkOwner{};

    struct UuidFkChildT;
    consteval {
        std::meta::define_aggregate(^^UuidFkChildT, storm::field_specs_for(^^UuidFkChild));
    }
    inline constexpr UuidFkChildT UuidFkChild{};

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

namespace {

    template <typename ConnType> class UuidFkChildTest : public StormTestFixture<UuidFkChild, ConnType, UuidFkOwner> {
      public:
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            UuidFkOwner                            owner{.id = storm::UUID{kOwnerUuid}, .name = "Acme"};
            storm::QuerySet<UuidFkOwner, ConnType> owner_qs;
            auto                                   owner_result = owner_qs.insert(owner).execute();
            ASSERT_TRUE(owner_result.has_value()) << owner_result.error().message();

            UuidFkChild                            child{.id = 1, .owner = owner, .label = "Widget"};
            storm::QuerySet<UuidFkChild, ConnType> qs;
            auto                                   child_result = qs.insert(child).execute();
            ASSERT_TRUE(child_result.has_value()) << child_result.error().message();
        }
    };

} // namespace
TYPED_TEST_SUITE(UuidFkChildTest, DatabaseTypes);

// #622 review finding: an FK member whose target has a UUID PK used to bypass the guard entirely
// (raw text bound, no validation) because Field's FieldType for an FK member is the related
// model struct, not the FK's stored column type.
TYPED_TEST(UuidFkChildTest, SelectFilteredByEmptyStringFkUuidTargetIsRejected) {
    storm::QuerySet<UuidFkChild, TypeParam> qs;
    auto                                    result = qs.where(fields::UuidFkChild.owner == "").select().execute();
    ASSERT_FALSE(
            result.has_value()
    ) << "a string-spelled empty UUID comparison against an FK-to-UUID-PK column must not silently match nothing";
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

TYPED_TEST(UuidFkChildTest, SelectFilteredBySetStringFkUuidTargetReturnsMatchingRow) {
    storm::QuerySet<UuidFkChild, TypeParam> qs;
    auto result = qs.where(fields::UuidFkChild.owner == kOwnerUuid).select().execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->label, "Widget");
}

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
// CollatedField's half of the fix (make_comparison threads ComparisonColumnType too) — a
// string-spelled collate().between() on a UUID column must be rejected the same as Field's.
static_assert(
        !CanBetweenCollatedUuidId<std::string_view>, "collate().between() with a string operand must be rejected too"
);

// ── #622: the guard above keyed on the OPERAND's type, not the COLUMN's — a string-spelled
// ordering/between() against a UUID column used to silently WORK as a plain lexicographic TEXT
// compare (BetweenExpr<std::string> exists), inconsistent with UUID being documented equality/
// IN-only. Deliberate, breaking tightening for consistency, not a crash fix. Same concepts,
// string_view operand.
static_assert(!CanGreaterUuidId<std::string_view>, "> with a string operand on a UUID column must be rejected too");
static_assert(!CanLessUuidId<std::string_view>, "< with a string operand on a UUID column must be rejected too");
static_assert(
        !CanBetweenUuidId<std::string_view>, "between() with string operands on a UUID column must be rejected too"
);
// Equality/IN must still accept a string operand — only ordering is rejected.
template <typename V>
concept CanEqualUuidId = requires(V v) { fields::UuidFilterDoc.id == v; };
static_assert(CanEqualUuidId<std::string_view>, "== with a string operand on a UUID column must still compile");

// ── #622 (related finding): a std::optional<T> operand used to pass ComparableOperand's
// BindableType check (which recurses through is_optional_v) and then hard-error deep inside
// make_comparison_expr's variant construction — no ComparisonExpr<std::optional<T>> arm exists
// for any T. Now rejected at the constraint, for a UUID column and a plain int column alike.
template <typename V>
concept CanEqualPersonAge = requires(V v) { fields::Person.age == v; };
static_assert(CanEqualPersonAge<int>, "== with a plain int operand must still compile");
static_assert(!CanEqualPersonAge<std::optional<int>>, "== with a std::optional operand must be rejected");
static_assert(!CanEqualUuidId<std::optional<storm::UUID>>, "== with a std::optional<UUID> operand must be rejected");

// A UUID column also rejects any operand that can't become a storm::UUID (#622 review finding):
// before this, `uuid_col == 5` silently bound an int against a UUID/TEXT column instead of
// failing at the constraint.
static_assert(!CanEqualUuidId<int>, "== with a non-UUID-constructible operand on a UUID column must be rejected");

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

// ── #622: same reproductions as above, but with the operand spelled as a plain string — the
// shape a hand-written `== ""` and the YAML/JSON query_builder.hpp path both produce.
// normalize_operand only ever saw the OPERAND's type before this fix, so these bypassed #609's
// guard entirely and bound raw, unvalidated text.

// The issue's own repro, string-spelled.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByEmptyStringPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto                                      result = qs.where(fields::UuidFilterDoc.id == "").select().execute();
    ASSERT_FALSE(result.has_value()) << "a string-spelled empty UUID comparison must not silently match nothing";
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

TYPED_TEST(UuidFilterDocTest, SelectFilteredByEmptyStringNonPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.related_uuid == "").select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

TYPED_TEST(UuidFilterDocTest, NotEqualFilterWithEmptyStringUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto                                      result = qs.where(fields::UuidFilterDoc.id != "").select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// erase() via a string-spelled empty filter — the issue's exact repro
// (`qs.where(fields::UuidKeyedDoc.id == "").erase().execute();`). Must not delete zero rows
// silently, and both rows must survive.
TYPED_TEST(UuidFilterDocTest, EraseFilteredByEmptyStringPkUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto                                      result = qs.where(fields::UuidFilterDoc.id == "").erase().execute();
    ASSERT_FALSE(result.has_value()) << "must not silently bind raw text and delete zero rows";
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
    EXPECT_EQ((count_rows<UuidFilterDoc, TypeParam>()), 2) << "both existing rows must survive";
}

// A malformed (non-empty) string operand must go through the same RFC-4122 validation as a
// storm::UUID{}-spelled operand, not bind as raw unvalidated text.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByMalformedStringUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == "not-a-uuid").select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), "Invalid UUID format: 'not-a-uuid'");
}

// A valid, non-empty string operand must still round-trip and discriminate the matching row —
// the guard must not reject legitimate string-spelled comparisons.
TYPED_TEST(UuidFilterDocTest, SelectFilteredBySetStringPkUuidReturnsMatchingRow) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id == kDocUuid).select().execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value().begin()->title, "Original");
}

// .in() was already routing through the FK/PK TARGET type (InTargetType) before #622, so a
// string-spelled element was already converted to storm::UUID — pinned here as a regression
// guard rather than a new fix.
TYPED_TEST(UuidFilterDocTest, InFilterWithOneEmptyStringUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto result = qs.where(fields::UuidFilterDoc.id.in(kDocUuid, "")).select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// CollatedField's half of the fix (#622 review finding) — make_comparison threads
// ComparisonColumnType too, so a string-spelled comparison via .collate() must be guarded the
// same as the plain Field path above.
TYPED_TEST(UuidFilterDocTest, SelectFilteredByEmptyStringCollatedUuidIsRejected) {
    storm::QuerySet<UuidFilterDoc, TypeParam> qs;
    auto                                      result =
            qs.where(fields::UuidFilterDoc.id.collate(storm::orm::utilities::Collate::None) == "").select().execute();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message(), kExpectedFilterError);
}

// NOLINTEND(readability-implicit-bool-conversion)
