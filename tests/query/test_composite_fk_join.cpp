#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — Person, Message, StormTestFixture, DatabaseTypes

// Must follow test_models.h: OrderLineWithShipments/Shipment name storm:: annotations.
#include "crud/test_composite_pk_models.h" // NOSONAR cpp:S954

// ── #504 Task 7: regular FK JoinStatement — multi-column AND-joined ON clause ──
// Shipment::line is a plain (non-m2m, non-reverse-FK) FK member whose target
// (OrderLineWithShipments) carries a 2-part composite PK (order_id, product_id).
// join<^^Shipment::line>() must emit an ON clause that AND-joins BOTH target PK
// parts against their respective local <fk>_<part>_id columns — using only ONE
// part (as the pre-fix code did, via FKBase_at<Is>::pk_name_) would silently
// match on order_id alone, potentially joining to the WRONG OrderLineWithShipments
// row when several rows share an order_id but differ in product_id.

namespace stmt = storm::orm::statements;

namespace {

    template <typename ConnType> class CompositeFkRegularJoinTest : public StormTestFixture<Shipment, ConnType> {};

} // namespace

TYPED_TEST_SUITE(CompositeFkRegularJoinTest, DatabaseTypes);

TYPED_TEST(CompositeFkRegularJoinTest, InnerJoinAcrossCompositeFkReturnsMatchingRow) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments line{.order_id = 5, .product_id = 12, .quantity = 3, .note = "first"};
    ASSERT_TRUE(line_qs.insert(line).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line, .carrier = "UPS"}).execute().has_value());

    auto results = ship_qs.template join<^^Shipment::line>().select().execute();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);
    auto& shipment = *results->begin();
    EXPECT_EQ(shipment.line.order_id, 5);
    EXPECT_EQ(shipment.line.product_id, 12);
    EXPECT_EQ(shipment.line.quantity, 3);
    EXPECT_EQ(shipment.carrier, "UPS");
}

// Disambiguation case (mandated by the task brief): two OrderLineWithShipments
// rows share ONE PK part (order_id == 5) but differ in the OTHER (product_id).
// Each is referenced by a different Shipment. If the ON clause only matched on
// order_id (the pre-fix bug), BOTH Shipments would join to whichever row the
// query planner picked first — this test proves each Shipment resolves to its
// OWN OrderLineWithShipments, not the other one sharing the partial key.
TYPED_TEST(CompositeFkRegularJoinTest, InnerJoinDoesNotCrossMatchOnSharedPkPart) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments line_a{.order_id = 5, .product_id = 12, .quantity = 3, .note = "a"};
    const OrderLineWithShipments line_b{.order_id = 5, .product_id = 99, .quantity = 7, .note = "b"};
    ASSERT_TRUE(line_qs.insert(line_a).execute().has_value());
    ASSERT_TRUE(line_qs.insert(line_b).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line_a, .carrier = "UPS"}).execute().has_value());
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line_b, .carrier = "FedEx"}).execute().has_value());

    auto results = ship_qs.template join<^^Shipment::line>().select().execute();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 2U);

    bool found_ups   = false;
    bool found_fedex = false;
    for (auto& shipment : *results) {
        if (shipment.carrier == "UPS") {
            EXPECT_EQ(shipment.line.order_id, 5);
            EXPECT_EQ(shipment.line.product_id, 12);
            EXPECT_EQ(shipment.line.quantity, 3);
            found_ups = true;
        } else if (shipment.carrier == "FedEx") {
            EXPECT_EQ(shipment.line.order_id, 5);
            EXPECT_EQ(shipment.line.product_id, 99);
            EXPECT_EQ(shipment.line.quantity, 7);
            found_fedex = true;
        }
    }
    EXPECT_TRUE(found_ups);
    EXPECT_TRUE(found_fedex);
}

// Shipment::line is a non-optional FK, so SQLite's FK enforcement (RESTRICT,
// the default policy) rejects both a dangling INSERT and a DELETE of a
// referenced parent — there is no way to produce a "no match" row through it.
// OptionalShipment::line (std::optional<OrderLineWithShipments>) is the
// composite-FK fixture built for exactly this: a NULL FK, mirroring the
// established convention for single-column optional-FK LEFT JOIN tests
// (test_fk_fields.cpp's NullableFKTest).
template <typename ConnType> class OptionalCompositeFkJoinTest : public StormTestFixture<OptionalShipment, ConnType> {};
TYPED_TEST_SUITE(OptionalCompositeFkJoinTest, DatabaseTypes);

TYPED_TEST(OptionalCompositeFkJoinTest, LeftJoinKeepsRowWithNullCompositeFk) {
    storm::QuerySet<OptionalShipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(OptionalShipment{.line = std::nullopt, .carrier = "FedEx"}).execute().has_value());

    auto results = ship_qs.template left_join<^^OptionalShipment::line>().select().execute();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U); // LEFT keeps the shipment even with a NULL composite FK
    EXPECT_FALSE(results->begin()->line.has_value());
}

TEST(CompositeFkJoinSqlTest, JoinOnClauseAndJoinsBothCompositeKeyParts) {
    using JS = stmt::JoinStatement<Shipment, storm::db::sqlite::Connection, stmt::JoinType::Inner, ^^Shipment::line>;
    const std::string& sql = JS::get_complete_sql();
    EXPECT_NE(sql.find("t2.order_id = t1.line_order_id"), std::string::npos) << sql;
    EXPECT_NE(sql.find("t2.product_id = t1.line_product_id"), std::string::npos) << sql;
    EXPECT_NE(sql.find(" AND "), std::string::npos) << sql;
}

// Regression guard: Message::sender (single-column PK target) JOIN SQL must be
// EXACTLY what it was before #504 — no AND, plain "t2.id = t1.sender_id".
TEST(CompositeFkJoinSqlTest, SinglePkJoinSqlStaysByteIdentical) {
    using JS = stmt::JoinStatement<Message, storm::db::sqlite::Connection, stmt::JoinType::Inner, ^^Message::sender>;
    const std::string& sql = JS::get_complete_sql();
    EXPECT_NE(sql.find("ON t2.id = t1.sender_id"), std::string::npos) << sql;
    EXPECT_EQ(sql.find(" AND "), std::string::npos) << sql; // no AND for a single-column key
}

// ── Review fix: fk_on_clause_body_size undercounted parts after the first ──
// The sizer reserved only " AND " (5 bytes) per composite-FK part after the
// first, but append_fk_on_clause_body actually writes " AND t<alias>." (7 +
// digits_of(alias) bytes) — a 2-3 byte-per-part deficit that ConstexprString
// silently swallows by truncating the tail of the buffer (ConstexprString::
// append drops bytes past capacity with no diagnostic). A `.contains(...)`
// substring check on a handful of expected fragments would still pass even
// with the tail chopped off, so both tests below assert EXACT equality
// against the full, hand-computed SQL string instead — the only check exact
// enough to catch a deficit regardless of whether SMALL_BUFFER's slack
// happens to absorb it for a given arity.
//
// LedgerEntryRef -> Ledger is a 3-part composite FK target (region, account,
// period): the ON clause has TWO "after-first" parts, a 6-byte deficit under
// the pre-fix sizer. Verified NOT sufficient on its own to overflow the
// stacked SMALL_BUFFER slack (calculate_complete_sql_size /
// calculate_join_sql_size / calculate_select_fields_size each pad by 10 bytes,
// ~30 bytes total) — this test still passed under the pre-fix sizer. Kept as
// the exact-length guard for the common 3-part case; LedgerEntryRefWide below
// is the fixture that actually forces a real truncation.
TEST(CompositeFkJoinSqlTest, ThreePartCompositeFkJoinSqlIsExact) {
    using JS = stmt::JoinStatement<
            LedgerEntryRef,
            storm::db::sqlite::Connection,
            stmt::JoinType::Inner,
            ^^LedgerEntryRef::ledger>;
    const std::string& sql = JS::get_complete_sql();
    EXPECT_EQ(
            sql,
            "SELECT t1.id, t1.note, t2.region, t2.account, t2.period, t2.balance "
            "FROM LedgerEntryRef t1"
            " INNER JOIN Ledger t2 ON t2.region = t1.ledger_region"
            " AND t2.account = t1.ledger_account"
            " AND t2.period = t1.ledger_period"
    );
}

// LedgerEntryRefWide joins the SAME 3-part Ledger target through SIX separate
// FK fields (ledger1..ledger6) — each FK's ON-clause body independently
// contributes the 6-byte deficit, for a combined 36 bytes, which DOES exceed
// the ~30-byte stacked slack. Confirmed to fail (truncated/wrong-length SQL)
// against the pre-fix sizer during development of this fix, and to pass here
// only because the sizer now reserves the writer's exact byte count.
TEST(CompositeFkJoinSqlTest, SixCompositeFkJoinsToSameTargetSqlIsExact) {
    using JS = stmt::JoinStatement<
            LedgerEntryRefWide,
            storm::db::sqlite::Connection,
            stmt::JoinType::Inner,
            ^^LedgerEntryRefWide::ledger1,
            ^^LedgerEntryRefWide::ledger2,
            ^^LedgerEntryRefWide::ledger3,
            ^^LedgerEntryRefWide::ledger4,
            ^^LedgerEntryRefWide::ledger5,
            ^^LedgerEntryRefWide::ledger6>;
    const std::string& sql = JS::get_complete_sql();

    std::string expected = "SELECT t1.id";
    for (int i = 2; i <= 7; ++i) {
        expected += std::format(", t{0}.region, t{0}.account, t{0}.period, t{0}.balance", i);
    }
    expected += " FROM LedgerEntryRefWide t1";
    for (int i = 2, ledger_n = 1; i <= 7; ++i, ++ledger_n) {
        expected += std::format(
                " INNER JOIN Ledger t{0} ON t{0}.region = t1.ledger{1}_region"
                " AND t{0}.account = t1.ledger{1}_account"
                " AND t{0}.period = t1.ledger{1}_period",
                i,
                ledger_n
        );
    }
    EXPECT_EQ(sql, expected);
}
