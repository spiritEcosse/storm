#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

// Tests for the PrimaryKeyType<T> concept (#505, widened for composite parts in #517):
// a compile-time gate on the type of a model's primary-key member(s). DECIDED
// 2026-07-22 for single PKs: every signed integral type except bool — short/int/long/
// long long and their fixed-width spellings. Rejected: bool, std::optional<T>, unsigned
// types (unless storm::signed_storage), and text. storm::UUID was admitted by #507.
// DECIDED 2026-07-29 for composite parts (#517): the same allowlist WIDENED WITH TEXT
// (std::string / std::string_view), because a composite key is never DB-generated (#502)
// — so #505's int64-identity rationale (RETURNING id / last_insert_rowid) does not reach
// parts. A TEXT SINGLE PK stays rejected. An FK part routes through valid_fk_key_target:
// it binds the TARGET's key, so the target's PK must itself be well-typed — the widening
// does not reach through an FK.

// ============================================================================
// Test models (local to this TU)
// ============================================================================

namespace {

    // ---- Accepted: signed integral widths ----
    struct PkShort {
        [[= storm::primary]] short id{};
    };
    struct PkInt {
        [[= storm::primary]] int id{};
    };
    struct PkLong {
        [[= storm::primary]] long id{};
    };
    struct PkLongLong {
        [[= storm::primary]] long long id{};
    };
    struct PkInt16 {
        [[= storm::primary]] std::int16_t id{};
    };
    struct PkInt32 {
        [[= storm::primary]] std::int32_t id{};
    };
    struct PkInt64 {
        [[= storm::primary]] std::int64_t id{};
    };
    struct PkPrimaryAutoincrement {
        [[= storm::primary_autoincrement]] int id{};
    };
    // A uint64_t PK explicitly annotated signed_storage is admitted — it binds and
    // extracts through the same int64 path as any other signed field (#436).
    struct PkUint64SignedStorage {
        [[ = storm::primary, = storm::signed_storage ]] std::uint64_t id{};
    };

    // ---- Rejected: bool ----
    struct PkBool {
        [[= storm::primary]] bool id{};
    };

    // ---- Rejected: char (a 1-byte identity is pathological; not in the width list) ----
    struct PkChar {
        [[= storm::primary]] char id{};
    };

    // ---- Rejected: nullable primary key ----
    struct PkOptionalInt {
        [[= storm::primary]] std::optional<int> id{};
    };

    // ---- Rejected: unsigned without signed_storage ----
    // (unsigned short/int are not gated by ModelStorageAnnotated at all, so they
    // reach PrimaryKeyType — reject them here since they aren't in the decided set.)
    struct PkUnsignedInt {
        [[= storm::primary]] unsigned int id{};
    };
    // full_unsigned stores as zero-padded 20-char TEXT, which breaks every
    // extract_int64 site a PK is read through — reject even though it is annotated.
    struct PkUint64FullUnsigned {
        [[ = storm::primary, = storm::full_unsigned ]] std::uint64_t id{};
    };

    // ---- Rejected: text ----
    struct PkText {
        [[= storm::primary]] std::string id{};
    };

    // ---- Rejected: UUID (not yet — a "not never", see #507) ----
    struct PkUuid {
        [[= storm::primary]] storm::UUID id{};
    };

    // ---- Composite PK parts (#517) ----
    // Accepted: the #505 scalar allowlist, widened with TEXT for parts only.
    struct PartsAllInt {
        [[= storm::primary_part]] int order_id{};
        [[= storm::primary_part]] int product_id{};
    };
    struct PartsIntAndText {
        [[= storm::primary_part]] int         warehouse{};
        [[= storm::primary_part]] std::string sku;
    };
    struct PartsThreeMixed {
        [[= storm::primary_part]] int          region{};
        [[= storm::primary_part]] std::string  account;
        [[= storm::primary_part]] std::int64_t period{};
    };
    struct PartsStringView {
        [[= storm::primary_part]] int              id{};
        [[= storm::primary_part]] std::string_view code;
    };
    struct PartsUuid {
        [[= storm::primary_part]] storm::UUID order_id{};
        [[= storm::primary_part]] int         product_id{};
    };
    struct PartsUint64SignedStorage {
        [[= storm::primary_part]] int                                      id{};
        [[ = storm::primary_part, = storm::signed_storage ]] std::uint64_t big{};
    };
    // An FK part is validated through its TARGET's primary key, not its own declared
    // type — it binds the referenced row's key (bind_one_pk_part's is_fk_field branch).
    struct PartFkTarget {
        [[= storm::primary]] int id{};
    };
    struct PartsFkAndInt {
        [[= storm::primary_part]][[= storm::fk<>]] PartFkTarget warehouse;
        [[= storm::primary_part]] int                           sku{};
    };

    // Rejected part types.
    struct PartsBool {
        [[= storm::primary_part]] int  id{};
        [[= storm::primary_part]] bool flag{};
    };
    struct PartsChar {
        [[= storm::primary_part]] int  id{};
        [[= storm::primary_part]] char code{};
    };
    struct PartsOptionalInt {
        [[= storm::primary_part]] int                id{};
        [[= storm::primary_part]] std::optional<int> maybe{};
    };
    // Guards the is_text_member optional-unwrap trap: is_text_member looks THROUGH
    // std::optional<>, so a naive TEXT check would accept this nullable part.
    struct PartsOptionalText {
        [[= storm::primary_part]] int                        id{};
        [[= storm::primary_part]] std::optional<std::string> maybe;
    };
    struct PartsUnsignedInt {
        [[= storm::primary_part]] int          id{};
        [[= storm::primary_part]] unsigned int u{};
    };
    struct PartsUint64FullUnsigned {
        [[= storm::primary_part]] int                                     id{};
        [[ = storm::primary_part, = storm::full_unsigned ]] std::uint64_t big{};
    };
    struct PartsDouble {
        [[= storm::primary_part]] int    id{};
        [[= storm::primary_part]] double amount{};
    };
    struct PartsBlob {
        [[= storm::primary_part]] int                       id{};
        [[= storm::primary_part]] std::vector<std::uint8_t> data;
    };
    // An FK part whose target has NO primary key — valid_fk_key_target must reject it.
    struct NoPkTarget {
        int value{};
    };
    struct PartsFkNoPkTarget {
        [[= storm::primary_part]][[= storm::fk<>]] NoPkTarget bad;
        [[= storm::primary_part]] int                         sku{};
    };
    // An FK part whose target HAS a PK, but of a type the policy rejects. The part binds
    // the TARGET's key (bind_one_pk_part splices find_fk_primary_key), so a double would
    // land in the composite key column — the policy must not be circumventable one hop
    // away. Checking PK presence alone (valid_fk_target) would wrongly accept this.
    struct BadPkTarget {
        [[= storm::primary]] double id{};
    };
    struct PartsFkBadPkTarget {
        [[= storm::primary_part]][[= storm::fk<>]] BadPkTarget bad;
        [[= storm::primary_part]] int                          sku{};
    };
    // An FK part whose target is itself COMPOSITE-keyed. Not merely unsupported — #504
    // shipped composite FKs for ordinary FK fields — but bind_one_pk_part still splices
    // the single-column find_fk_primary_key, so such a part would bind ONE column for an
    // N-column key. Refused outright rather than validated on its first part alone. Also
    // guards the is_primary_member subsumption trap: is_primary_member matches
    // primary_part too, so testing it first would accept this model after checking only
    // `warehouse`, never seeing `sku`.
    struct CompositeKeyTarget {
        [[= storm::primary_part]] int         warehouse{};
        [[= storm::primary_part]] std::string sku;
    };
    struct PartsFkCompositeTarget {
        [[= storm::primary_part]][[= storm::fk<>]] CompositeKeyTarget ref;
        [[= storm::primary_part]] int                                 n{};
    };
    // The parts-only TEXT widening does NOT reach through an FK: what binds here is the
    // target's own single PK, and #505 rejects a std::string single PK. So an FK part
    // pointing at a TEXT-keyed model inherits that rejection.
    struct TextPkTarget {
        [[= storm::primary]] std::string id;
    };
    struct PartsFkTextPkTarget {
        [[= storm::primary_part]][[= storm::fk<>]] TextPkTarget ref;
        [[= storm::primary_part]] int                           sku{};
    };

} // namespace

// ============================================================================
// Compile-time concept gate — PrimaryKeyType<T>
// ============================================================================

// PrimaryKeyType<T> is the model-boundary concept: true iff T's primary-key member
// has a type in the decided allowed set. It is one of the concepts BaseStatement<T>
// (and thus QuerySet<T>) requires, so a bad model fails to instantiate with a clear
// constraint violation. These static_asserts pin the contract on the GATE predicate
// itself (mirrors ModelMaxLengthValid in test_max_length.cpp), not on a downstream
// public call.

static_assert(storm::orm::statements::PrimaryKeyType<PkShort>, "short PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkInt>, "int PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkLong>, "long PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkLongLong>, "long long PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkInt16>, "std::int16_t PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkInt32>, "std::int32_t PK must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PkInt64>, "std::int64_t PK must be accepted");
static_assert(
        storm::orm::statements::PrimaryKeyType<PkPrimaryAutoincrement>, "primary_autoincrement int PK must be accepted"
);
static_assert(
        storm::orm::statements::PrimaryKeyType<PkUint64SignedStorage>,
        "uint64_t PK annotated signed_storage must be accepted"
);

static_assert(!storm::orm::statements::PrimaryKeyType<PkBool>, "bool PK must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PkChar>, "char PK must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PkOptionalInt>, "std::optional<int> PK must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PkUnsignedInt>, "unsigned int PK must be rejected");
static_assert(
        !storm::orm::statements::PrimaryKeyType<PkUint64FullUnsigned>,
        "uint64_t PK annotated full_unsigned must be rejected"
);
static_assert(!storm::orm::statements::PrimaryKeyType<PkText>, "std::string PK must be rejected");
// UUID PKs are now supported (#507) — caller must provide the UUID explicitly
static_assert(storm::orm::statements::PrimaryKeyType<PkUuid>, "storm::UUID primary keys should be accepted");

// ---- Composite PK parts (#517) ----
// Accepted
static_assert(storm::orm::statements::PrimaryKeyType<PartsAllInt>, "all-int composite parts must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PartsIntAndText>, "a TEXT composite part must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PartsThreeMixed>, "3-part mixed int/TEXT/int64 must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PartsStringView>, "a string_view part must be accepted");
static_assert(storm::orm::statements::PrimaryKeyType<PartsUuid>, "a storm::UUID part must be accepted");
static_assert(
        storm::orm::statements::PrimaryKeyType<PartsUint64SignedStorage>,
        "a signed_storage uint64 part must be accepted"
);
static_assert(storm::orm::statements::PrimaryKeyType<PartsFkAndInt>, "an FK part with a PK'd target must be accepted");

// Rejected
static_assert(!storm::orm::statements::PrimaryKeyType<PartsBool>, "a bool part must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PartsChar>, "a char part must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PartsOptionalInt>, "a nullable part must be rejected");
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsOptionalText>,
        "optional<string> must be rejected — is_text_member looks through optional"
);
static_assert(!storm::orm::statements::PrimaryKeyType<PartsUnsignedInt>, "an unsigned int part must be rejected");
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsUint64FullUnsigned>,
        "a full_unsigned uint64 part must be rejected — it stores as zero-padded TEXT"
);
static_assert(!storm::orm::statements::PrimaryKeyType<PartsDouble>, "a double part must be rejected");
static_assert(!storm::orm::statements::PrimaryKeyType<PartsBlob>, "a blob part must be rejected");
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsFkNoPkTarget>, "an FK part whose target has no PK must be rejected"
);
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsFkBadPkTarget>,
        "an FK part whose target PK is a rejected type must be rejected — the part binds that key"
);
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsFkTextPkTarget>,
        "the parts-only TEXT widening must not reach through an FK to a std::string single PK"
);
static_assert(
        !storm::orm::statements::PrimaryKeyType<PartsFkCompositeTarget>,
        "an FK part pointing at a composite-keyed target needs a multi-column FK (#504) — refuse it"
);

// ============================================================================
// Positive path — accepted PK types remain queryable end to end
// ============================================================================

namespace {
    // A non-PK column keeps this out of the single-field (PK-only, non-autoincrement)
    // INSERT edge case — a separate, pre-existing issue unrelated to #505.
    struct PkIntWithColumn {
        [[= storm::primary]] int id{};
        int                      value{};
    };
} // namespace

template <typename ConnType> class PrimaryKeyTypeAcceptedTest : public StormTestFixture<PkIntWithColumn, ConnType> {};
TYPED_TEST_SUITE(PrimaryKeyTypeAcceptedTest, DatabaseTypes);

TYPED_TEST(PrimaryKeyTypeAcceptedTest, InsertAndSelectRoundTrip) {
    storm::QuerySet<PkIntWithColumn, TypeParam> qs;
    auto                                        result = qs.insert(PkIntWithColumn{.id = 1, .value = 42}).execute();
    ASSERT_TRUE(result.has_value()) << result.error().message();

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value()) << selected.error().message();
    ASSERT_EQ(selected.value().size(), 1U);
    EXPECT_EQ(selected.value().begin()->id, 1);
    EXPECT_EQ(selected.value().begin()->value, 42);
}

TEST(PrimaryKeyType, CompileTimeOnly) {
    // The static_asserts above are the real test; this keeps the TU a runnable
    // GTest target so the suite reports it.
    SUCCEED();
}
