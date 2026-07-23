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

// ── #504 (Task 3): composite-aware column-name writer for FK members ──
// fk_column_names_size / append_fk_column_names are the composite-aware
// sibling of append_column_name / column_name_size (#422). A single-column
// FK target must degenerate to EXACTLY what append_column_name emits today
// ("<member>_id") — the byte-identical guarantee. A composite target spells
// one column per target PK part: "<member>_<part>", AND-joined by the
// caller-supplied separator.

TEST(FkColumnNamesTest, SingleColumnTargetMatchesLegacyAppendColumnName) {
    // Message::sender (fk<> Person, single-column PK) must emit exactly what
    // append_column_name emits today: "sender_id".
    //
    // fk_column_names_size/append_fk_column_names are consteval (like every other
    // writer in field_attr.cppm) and write into any buffer exposing
    // .append(std::string_view) — the same contract append_column_name uses. Every
    // real call site (field_names.cppm, join.cppm, ...) drives that buffer as a
    // storm::orm::utilities::ConstexprString<N> built inside a consteval function, so
    // the test mirrors that: build the ConstexprString at compile time, then compare
    // its runtime .view() to the expected string.
    using MessageBase            = storm::orm::statements::BaseStatement<Message>;
    constexpr auto sender_member = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^Message, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == "sender") {
                return m;
            }
        }
        std::unreachable();
    }();
    constexpr auto target_pk = MessageBase::find_fk_primary_key_members<Person>();
    constexpr auto size      = storm::meta::fk_column_names_size(sender_member, target_pk, ", ");
    constexpr auto result    = []() consteval {
        storm::orm::utilities::ConstexprString<size + 1> buf;
        storm::meta::append_fk_column_names(buf, sender_member, target_pk, ", ");
        return buf;
    }();
    EXPECT_EQ(result.view(), "sender_id");
}

TEST(FkColumnNamesTest, CompositeTargetEmitsOneColumnPerPart) {
    // A hypothetical FK member `line` of type OrderLine (2-part composite PK:
    // order_id, product_id) must emit "line_order_id, line_product_id".
    // (Uses a hand-rolled member array since no in-tree model has this FK yet —
    // this test validates the writer in isolation before Task 4 wires up a real model.)
    // fk_member is a placeholder info — the writer only reads the FK member's own
    // identifier, so any member reflection with identifier "line" would do; here we
    // reuse OrderLine::order_id's info as a stand-in reflection value and rely on
    // the test asserting the TARGET side (order_id/product_id) is correctly spelled
    // to prove the per-part loop is right; Task 4 covers the member-identifier prefix
    // end to end against a real composite-FK-holding model.
    constexpr auto target_pk = storm::orm::statements::BaseStatement<OrderLine>::primary_key_members_;
    constexpr auto size      = storm::meta::fk_column_names_size(target_pk[0], target_pk, ", ");
    constexpr auto result    = []() consteval {
        storm::orm::utilities::ConstexprString<size + 1> buf;
        storm::meta::append_fk_column_names(buf, target_pk[0], target_pk, ", ");
        return buf;
    }();
    EXPECT_EQ(result.view(), "order_id_order_id, order_id_product_id");
}

// ── #504 (Task 5): ValidForeignKey composite-arity finding (no code change) ──
// fk<> annotates ONE member whose TYPE is the whole related object — there are
// no separate "local FK columns" to mismatch in count against the target's PK
// arity (unlike SQLAlchemy's ForeignKeyConstraint, which lists local columns
// explicitly). So ValidForeignKey<OrderLine> just needs OrderLine to have A
// primary key (single or composite) — already true via ModelWithPrimaryKey
// (#500 widened is_primary_member for primary_part). No new concept code
// needed; this test is a permanent regression guard on that reasoning.

TEST(CompositeFkArityTest, ValidForeignKeyAcceptsCompositePkTargetWithNoArityMismatchPossible) {
    static_assert(storm::orm::statements::ValidForeignKey<OrderLine>);
    static_assert(storm::orm::statements::ValidForeignKey<std::optional<OrderLine>>);
    SUCCEED();
}
