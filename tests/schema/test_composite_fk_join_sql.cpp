#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — Message, Person

// Must follow test_models.h: OrderLine/Ledger are composite-PK fixtures reused
// across the #501/#502 tests; nothing here needs Person as an FK part.
#include "crud/test_composite_pk_models.h" // NOSONAR cpp:S954

// ── #504 (Task 2): widen find_fk_primary_key to find_fk_primary_key_members ──
// Step 1 of the composite-FK-join plan. Adds an ADDITIVE, composite-aware
// accessor for an FK target's full primary-key member list, alongside the
// existing (untouched) single-column find_fk_primary_key. Every existing
// single-FK call site keeps using find_fk_primary_key, byte-identical;
// find_fk_primary_key_members is consumed only by later composite-FK bind /
// extract / JOIN paths (Tasks 3-8 of the plan).

namespace {

    using storm::orm::statements::BaseStatement;

    // ---- Single-column FK target: Message::sender is fk<> Person, Person has
    // ---- exactly one primary-key member. find_fk_primary_key_members must
    // ---- return a 1-element array — the same shape find_fk_primary_key does.
    using MessageBase                = BaseStatement<Message>;
    constexpr auto single_pk_members = MessageBase::find_fk_primary_key_members<Person>();
    static_assert(single_pk_members.size() == 1);
    static_assert(MessageBase::fk_primary_key_count<Person>() == 1);
    static_assert(single_pk_members[0] == MessageBase::find_fk_primary_key<Person>());

    // ---- Composite (2-part) FK target: OrderLine has a 2-part composite PK
    // ---- (order_id, product_id). find_fk_primary_key_members must return
    // ---- both parts, in declaration order, matching OrderLine's own
    // ---- primary_key_members_ exactly.
    using OrderLineBase = BaseStatement<OrderLine>;
    static_assert(OrderLineBase::primary_key_members_.size() == 2);
    static_assert(OrderLineBase::primary_key_column_count_ == 2);

    constexpr auto order_line_fk_members = MessageBase::find_fk_primary_key_members<OrderLine>();
    static_assert(order_line_fk_members.size() == 2);
    static_assert(MessageBase::fk_primary_key_count<OrderLine>() == 2);
    static_assert(order_line_fk_members[0] == OrderLineBase::primary_key_members_[0]);
    static_assert(order_line_fk_members[1] == OrderLineBase::primary_key_members_[1]);

    // ---- Composite (3-part), mixed-type FK target: Ledger has region (int),
    // ---- account (std::string), period (int64_t). Proves the widened
    // ---- accessor is not arity- or type-limited.
    using LedgerBase = BaseStatement<Ledger>;
    static_assert(LedgerBase::primary_key_members_.size() == 3);
    static_assert(LedgerBase::primary_key_column_count_ == 3);

    constexpr auto ledger_fk_members = MessageBase::find_fk_primary_key_members<Ledger>();
    static_assert(ledger_fk_members.size() == 3);
    static_assert(MessageBase::fk_primary_key_count<Ledger>() == 3);
    static_assert(ledger_fk_members[0] == LedgerBase::primary_key_members_[0]);
    static_assert(ledger_fk_members[1] == LedgerBase::primary_key_members_[1]);
    static_assert(ledger_fk_members[2] == LedgerBase::primary_key_members_[2]);

    // ---- Nullable FK target: std::optional<Person> unwraps the same as the
    // ---- existing find_fk_primary_key does (ValidForeignKey / optional_inner_type_t).
    static_assert(MessageBase::fk_primary_key_count<std::optional<Person>>() == 1);
    constexpr auto optional_target_members = MessageBase::find_fk_primary_key_members<std::optional<Person>>();
    static_assert(optional_target_members.size() == 1);
    static_assert(optional_target_members[0] == single_pk_members[0]);

} // namespace

TEST(CompositeFkResolutionTest, FindFkPrimaryKeyMembersReturnsOnePartForSinglePkTarget) {
    // Verification is entirely in the static_asserts above; this body just
    // gives GoogleTest something to run.
    SUCCEED();
}

TEST(CompositeFkResolutionTest, FindFkPrimaryKeyMembersReturnsTwoPartsForOrderLineTarget) {
    SUCCEED();
}

TEST(CompositeFkResolutionTest, FkPrimaryKeyCountMatchesTargetPrimaryKeyColumnCount) {
    EXPECT_EQ(OrderLineBase::primary_key_column_count_, 2);
}
