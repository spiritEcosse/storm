#include <gtest/gtest.h>
#include <meta>
#include <plf_hive/plf_hive.h> // NOSONAR cpp:S954 — must precede `import std;` (see test_m2m_models.h)

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — Person, Message, StormTestFixture, DatabaseTypes

// Must follow test_models.h: OrderLineWithShipments/Shipment name storm:: annotations.
#include "crud/test_composite_pk_models.h" // NOSONAR cpp:S954
#include "test_m2m_models.h"               // NOSONAR cpp:S954 — Student/Course (single-PK m2m regression baseline)

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
// ── #504 Task 8: StitchKey-based m2m/reverse-FK stitch for a COMPOSITE owner ──
//
// The two-query eager-load path (m2m #391, reverse-FK #398) stitches Q2 rows to
// their Q1 owner through a hash map keyed on the owner's primary key. Before
// this task the key was a bare std::int64_t built from ONLY the first PK part
// (Base::primary_key_), which for LedgerWithTags/OrderLineWithShipments (both
// composite: 3 and 2 parts respectively) collides whenever two owners share
// their first part but differ in a later one — exactly what the tests below
// construct on purpose.

// ---- Reverse-FK composite-owner case (no junction table needed) -----------
// OrderLineWithShipments' PK is (order_id, product_id); Shipment::line is the
// FK back-reference (reverse_fk<^^Shipment>::shipments). Two lines share
// order_id=1 but differ in product_id — proves the stitch keys on BOTH parts.
template <typename ConnType>
class CompositeReverseFkOwnerTest : public StormTestFixture<OrderLineWithShipments, ConnType, Shipment> {
  protected:
    // Insert a lone OrderLineWithShipments with no Shipment pointing at it —
    // shared setup for the empty-relation INNER/LEFT pair below.
    static auto insert_solo_line(storm::QuerySet<OrderLineWithShipments, ConnType>& line_qs) -> void {
        ASSERT_TRUE(
                line_qs.insert(OrderLineWithShipments{.order_id = 9, .product_id = 1, .quantity = 1, .note = "solo"})
                        .execute()
                        .has_value()
        );
    }
};
TYPED_TEST_SUITE(CompositeReverseFkOwnerTest, DatabaseTypes);

TYPED_TEST(CompositeReverseFkOwnerTest, ReverseFkStitchesCorrectlyWithCompositeOwnerKey) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments line_a{.order_id = 1, .product_id = 10, .quantity = 5, .note = "a"};
    const OrderLineWithShipments line_b{.order_id = 1, .product_id = 20, .quantity = 7, .note = "b"};
    ASSERT_TRUE(line_qs.insert(line_a).execute().has_value());
    ASSERT_TRUE(line_qs.insert(line_b).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line_a, .carrier = "DHL"}).execute().has_value());

    auto results = line_qs.template join<^^OrderLineWithShipments::shipments>().select().execute();
    ASSERT_TRUE(results.has_value()) << results.error().message();

    bool found_a = false;
    for (const auto& line : *results) {
        if (line.product_id == 10) {
            ASSERT_EQ(line.shipments.size(), 1U);
            EXPECT_EQ(line.shipments[0].carrier, "DHL");
            found_a = true;
        }
        if (line.product_id == 20) {
            FAIL() << "product_id=20 shares order_id=1 with product_id=10 but has no shipment of its own — "
                      "if the stitch keyed only on order_id (the pre-#504-Task-8 bug), this row would "
                      "wrongly inherit product_id=10's shipment and survive the INNER-join drop.";
        }
    }
    EXPECT_TRUE(found_a);
}

TYPED_TEST(CompositeReverseFkOwnerTest, EmptyRelationOnCompositeOwnerReturnsNoInnerJoinRows) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    this->insert_solo_line(line_qs);
    auto results = line_qs.template join<^^OrderLineWithShipments::shipments>().select().execute(); // INNER
    ASSERT_TRUE(results.has_value());
    EXPECT_TRUE(results->empty());
}

TYPED_TEST(CompositeReverseFkOwnerTest, LeftJoinKeepsEmptyRelationOnCompositeOwner) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    this->insert_solo_line(line_qs);
    auto results = line_qs.template left_join<^^OrderLineWithShipments::shipments>().select().execute();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);
    EXPECT_TRUE(results->begin()->shipments.empty());
}

// Fan-out >= 10: one composite owner with 10 shipments — proves the stitch loop
// appends every related row into the SAME owner's container (no drops, no
// cross-contamination into a different owner sharing a PK part).
TYPED_TEST(CompositeReverseFkOwnerTest, FanOutTenShipmentsAllStitchToSameCompositeOwner) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments owner{.order_id = 3, .product_id = 7, .quantity = 1, .note = "fanout"};
    ASSERT_TRUE(line_qs.insert(owner).execute().has_value());
    // A decoy sharing order_id=3 but a different product_id — must stay unaffected.
    const OrderLineWithShipments decoy{.order_id = 3, .product_id = 8, .quantity = 1, .note = "decoy"};
    ASSERT_TRUE(line_qs.insert(decoy).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(
                ship_qs.insert(Shipment{.line = owner, .carrier = std::format("carrier-{}", i)}).execute().has_value()
        );
    }

    auto results = line_qs.template join<^^OrderLineWithShipments::shipments>().select().execute();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U); // decoy has zero shipments — dropped by INNER
    EXPECT_EQ(results->begin()->product_id, 7);
    EXPECT_EQ(results->begin()->shipments.size(), 10U);
}

// ---- m2m composite-owner case ----------------------------------------------
// LedgerWithTags' PK is (region, account, period); `tags` is a many_to_many<>
// container of LedgerTag via an auto-junction table. The auto-junction DDL
// (build_junction_sql in schema.cppm) is NOT yet widened for a composite owner
// side — that is Task 9's scope, tracked separately: build_junction_sql emits
// exactly ONE "<Base>_id" junction column regardless of Base's own PK arity,
// AND emits "REFERENCES LedgerWithTags(id)" — a column that does not exist on
// a composite-PK owner (there is no "id" column at all). SQLite does not
// validate FK target columns at CREATE TABLE time, so this happens to succeed
// there, but PostgreSQL does validate and rejects it outright ("column id does
// not exist") — CONFIRMED during this task's own testing (not a hypothetical):
// create_table_if_not_exists<LedgerWithTags> genuinely cannot run on
// PostgreSQL today. This is a pre-existing Task-9-scoped bug, not something
// Task 8 introduced or can fix without touching schema.cppm (explicitly out
// of scope here) — hence no StormTestFixture (which would call
// create_table_if_not_exists and fail on PG); .sql() below is pure text
// generation and needs only a bare connection, not real tables. Task 8
// deliberately does NOT touch the m2m junction-side column list — doing so
// would reference columns the junction schema doesn't have. What IS verified
// below (without needing the junction DDL, and without hitting the PG bug) is
// that the m2m path's Q2 SQL shape is UNCHANGED (single "t2.<Base>_id" column,
// on both a single-PK and a composite-PK owner) — i.e. this task did not
// silently half-widen the m2m path into broken SQL. Full end-to-end m2m
// stitch coverage over a composite owner is deferred to whenever Task 9 lands
// the junction DDL (and, on PostgreSQL, the schema-creation fix too).
template <typename ConnType> class CompositeM2MOwnerSqlShapeTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        auto result = storm::QuerySet<LedgerWithTags, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value());
    }
    auto TearDown() -> void override {
        storm::QuerySet<LedgerWithTags, ConnType>::clear_default_connection();
    }
};
TYPED_TEST_SUITE(CompositeM2MOwnerSqlShapeTest, DatabaseTypes);

TYPED_TEST(CompositeM2MOwnerSqlShapeTest, Q2SqlStaysSingleColumnPendingTask9JunctionWidening) {
    storm::QuerySet<LedgerWithTags, TypeParam> ledger_qs;
    auto                                       sql = ledger_qs.template join<^^LedgerWithTags::tags>().select().sql();
    // Q2 SELECT head: still exactly one owner-key column (the junction's only
    // "<Base>_id" column) — NOT all 3 PK parts. Widening this requires Task 9's
    // junction DDL (a composite owner side needs 3 junction columns, not 1).
    EXPECT_TRUE(sql.contains("SELECT t2.LedgerWithTags_id, t3.id, t3.label")) << sql;
    EXPECT_TRUE(sql.contains("WHERE t2.LedgerWithTags_id IN (SELECT region, account, period FROM LedgerWithTags"))
            << sql;
}

// Review-fix regression guard: TwoQueryJoinBase::extract_q2_owner_pk originally
// read Base::primary_key_members_.size() columns unconditionally — correct for
// reverse-FK (the owning table carries a REAL N-column FK), but WRONG for m2m,
// whose junction is always exactly 1 physical owner-key column regardless of
// Base's own PK arity. For LedgerWithTags (3-part PK) that bug would read 3
// columns from a Q2 row that only has 1 owner column + 2 related columns
// (t3.id, t3.label) — reading straight into the RELATED entity's own data, a
// genuine cross-entity misread caught during this task's own self-review
// (not a hypothetical). M2MJoinStatement::owner_key_column_count_ must stay 1
// for every model, composite-PK owner or not.
static_assert(
        stmt::M2MJoinStatement<
                LedgerWithTags,
                storm::db::sqlite::Connection,
                stmt::JoinType::Inner,
                ^^LedgerWithTags::tags>::owner_key_column_count_ == 1,
        "m2m owner-key column count must stay 1 (the junction's single physical column) even for a "
        "composite-PK owner — Task 9 owns widening the junction itself"
);
static_assert(
        stmt::ReverseFKJoinStatement<
                OrderLineWithShipments,
                storm::db::sqlite::Connection,
                stmt::JoinType::Inner,
                ^^Shipment::line>::owner_key_column_count_ == 2,
        "reverse-FK owner-key column count must match the composite base's own PK arity (2 for "
        "OrderLineWithShipments) — the owning table carries a REAL N-column FK"
);

template <typename ConnType>
class CompositeM2MSinglePkRegressionTest : public StormTestFixture<Student, ConnType, Course> {};
TYPED_TEST_SUITE(CompositeM2MSinglePkRegressionTest, DatabaseTypes);

TYPED_TEST(CompositeM2MSinglePkRegressionTest, SinglePkM2MSqlShapeStaysByteIdentical) {
    storm::QuerySet<Student, TypeParam> student_qs;
    auto                                sql = student_qs.template join<^^Student::courses>().select().sql();
    EXPECT_TRUE(sql.contains("SELECT t2.Student_id, t3.id, t3.title")) << sql;
    EXPECT_TRUE(sql.contains("WHERE t2.Student_id IN (SELECT id FROM Student)")) << sql;
}

// ── Reverse-FK Q2 SQL shape over a composite owner (#504 Task 8) ────────────
// Documents (and regression-guards) the exact widened SQL: the owner-key
// column list widens to N parts, each individually re-aliased ("t2.<part>",
// not a bare comma list — that would alias only the first part); the WHERE
// clause becomes the row-value-IN-subquery form on BOTH sides (outer
// "(t2.a, t2.b)" wrapped in parens, inner subquery SELECT list a PLAIN
// comma-joined column list, NOT itself parenthesized — parenthesizing the
// inner list was a real bug this task's own testing caught: SQLite parses
// "(a, b)" in a SELECT list as one parenthesized scalar expression, not a row
// value, so the sub-select silently returns 1 column instead of 2 (an error,
// not a silent miscompare, so it fails loudly here — but a subtler variant of
// the same mistake could easily have gone unnoticed without an exact-string
// assertion). Owner columns include Shipment::line, itself a composite-FK
// member — its SELECT columns must ALSO widen (t2.line_order_id,
// t2.line_product_id), not collapse to a single bogus t2.line_id.
TEST(CompositeFkJoinSqlTest, ReverseFkQ2SqlShapeIsExactOverCompositeOwner) {
    using JS = stmt::ReverseFKJoinStatement<
            OrderLineWithShipments,
            storm::db::sqlite::Connection,
            stmt::JoinType::Inner,
            ^^Shipment::line>;
    const std::string sql = JS::build_q2_sql(nullptr, std::nullopt, std::nullopt, std::nullopt);
    EXPECT_EQ(
            sql,
            "SELECT t2.line_order_id, t2.line_product_id, t2.id, t2.line_order_id, t2.line_product_id, t2.carrier "
            "FROM Shipment t2 WHERE (t2.line_order_id, t2.line_product_id) IN (SELECT order_id, product_id FROM "
            "OrderLineWithShipments)"
    );
}

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
