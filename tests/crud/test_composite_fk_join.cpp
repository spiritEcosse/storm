#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — Person, StormTestFixture, ensure_tables

// Must follow test_models.h: OrderLineWithShipments/Shipment name storm:: annotations.
#include "test_composite_pk_models.h" // NOSONAR cpp:S954

// ── #504: composite FK bind + extract (INSERT/SELECT plain FK column) ───────
// Shipment::line is a plain (non-JOIN) FK member whose target
// (OrderLineWithShipments) carries a 2-part composite PK (order_id, product_id).
// This proves the INSERT bind path writes both target-key parts into
// line_order_id/line_product_id, and the SELECT extraction path reads them back
// into a fully-populated `line` object — without ever touching the JOIN machinery
// (out of scope here, #504's later tasks).

namespace {

    // SchemaStatement<Shipment>::create_table_if_not_exists creates the FK parent
    // (OrderLineWithShipments) automatically via create_fk_parents (#412), so no
    // ExtraModels are needed here — unlike StockEntryTest in test_composite_pk_crud.cpp,
    // which creates Person itself because that fixture seeds through raw SQL before
    // Shipment's own table exists.
    template <typename ConnType> class CompositeFkJoinTest : public StormTestFixture<Shipment, ConnType> {};

} // namespace

TYPED_TEST_SUITE(CompositeFkJoinTest, DatabaseTypes);

TYPED_TEST(CompositeFkJoinTest, InsertAndSelectRoundTripsCompositeFkColumns) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments line{.order_id = 5, .product_id = 12, .quantity = 3, .note = "first"};
    ASSERT_TRUE(line_qs.insert(line).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    const Shipment                       ship{.line = line, .carrier = "UPS"};
    auto                                 insert_result = ship_qs.insert(ship).execute();
    ASSERT_TRUE(insert_result.has_value());

    auto rows = ship_qs.select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    const Shipment& loaded = *rows.value().begin();
    EXPECT_EQ(loaded.line.order_id, 5);
    EXPECT_EQ(loaded.line.product_id, 12);
    EXPECT_EQ(loaded.carrier, "UPS");
}

// Distinguishes the two composite-FK target parts from each other: swapping them
// would have this test pass if the bind/extract order matched a coincidental
// symmetry, but the two rows below share no value across order_id/product_id.
TYPED_TEST(CompositeFkJoinTest, MultipleShipmentsResolveDistinctCompositeFkTargets) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    const OrderLineWithShipments line_a{.order_id = 1, .product_id = 100, .quantity = 1, .note = "a"};
    const OrderLineWithShipments line_b{.order_id = 2, .product_id = 200, .quantity = 2, .note = "b"};
    ASSERT_TRUE(line_qs.insert(line_a).execute().has_value());
    ASSERT_TRUE(line_qs.insert(line_b).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line_a, .carrier = "DHL"}).execute().has_value());
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line_b, .carrier = "FedEx"}).execute().has_value());

    auto rows = ship_qs.select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 2U);

    bool found_a = false;
    bool found_b = false;
    for (const Shipment& row : rows.value()) {
        if (row.carrier == "DHL") {
            EXPECT_EQ(row.line.order_id, 1);
            EXPECT_EQ(row.line.product_id, 100);
            found_a = true;
        } else if (row.carrier == "FedEx") {
            EXPECT_EQ(row.line.order_id, 2);
            EXPECT_EQ(row.line.product_id, 200);
            found_b = true;
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TYPED_TEST(CompositeFkJoinTest, SchemaEmitsTwoColumnsForCompositeFk) {
    // line_order_id, line_product_id must both exist as columns.
    const std::string& sql = storm::create_table_sql<Shipment>();
    EXPECT_NE(sql.find("line_order_id"), std::string::npos) << sql;
    EXPECT_NE(sql.find("line_product_id"), std::string::npos) << sql;
}

// ── Single-column FK regression ──────────────────────────────────────────────
// Message::sender (test_models.h) is the long-standing single-column FK fixture.
// Its bind/extract behaviour must be byte-for-byte unchanged by the composite
// widening (the if constexpr branches key on fk_primary_key_count<FKType>()).

namespace {
    template <typename ConnType>
    class SingleColumnFkRegressionTest : public StormTestFixture<Message, ConnType, Person> {};
} // namespace

TYPED_TEST_SUITE(SingleColumnFkRegressionTest, DatabaseTypes);

TYPED_TEST(SingleColumnFkRegressionTest, InsertAndSelectRoundTripsSingleColumnFk) {
    storm::QuerySet<Person, TypeParam> person_qs;
    const Person                       sender{.name = "Alice", .age = 30};
    ASSERT_TRUE(person_qs.insert(sender).execute().has_value());

    storm::QuerySet<Message, TypeParam> msg_qs;
    const Message                       msg{.content = "hi", .sender = {.id = 1}};
    ASSERT_TRUE(msg_qs.insert(msg).execute().has_value());

    auto rows = msg_qs.select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value().begin()->sender.id, 1);
    EXPECT_EQ(rows.value().begin()->content, "hi");
}
