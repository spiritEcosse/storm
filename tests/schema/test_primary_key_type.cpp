#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

// Tests for the PrimaryKeyType<T> concept (#505): a compile-time gate on the type
// of a model's primary-key member. Independent of #90 (composite PK); found while
// scoping it. DECIDED 2026-07-22: the allowed set is every signed integral type
// except bool — short/int/long/long long and their fixed-width spellings. Rejected:
// bool, std::optional<T>, unsigned types (unless storm::signed_storage), and
// text/UUID (storm::UUID PKs are a "not yet" — filed as #507).

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
static_assert(!storm::orm::statements::PrimaryKeyType<PkUuid>, "storm::UUID PK must be rejected");

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
