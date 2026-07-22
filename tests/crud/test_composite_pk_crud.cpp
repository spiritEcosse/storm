#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954 — Person, the FK target of StockEntry

// Must follow test_models.h: StockEntry's FK part names Person.
#include "test_composite_pk_models.h" // NOSONAR cpp:S954

// ── #501: composite PK — UPDATE and DELETE executed on live backends ─────────
// The companion of test_composite_pk_sql.cpp, which pins the compile-time gates
// and the generated SQL text. This TU proves the widened WHERE clause and the
// bind arithmetic behave against real SQLite and PostgreSQL: a full-key match
// hits exactly one row, and a partial-key match hits none.
//
// INSERT by composite key is #502, so every fixture seeds through raw SQL — the
// same approach #500's execution test used.

// NOLINTBEGIN(readability-implicit-bool-conversion)

namespace {

    // Composite-PK fixture. The table is created from the generated DDL, since
    // ensure_tables() drives the same schema path. Anchors the connection on
    // Person but creates no Person table.
    template <typename Model, typename ConnType> class CompositePkFixture : public StormTestFixture<Person, ConnType> {
      public:
        auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
            const std::string ddl = storm::test::is_postgresql<ConnType>()
                                            ? storm::create_table_sql<Model, storm::orm::schema::Dialect::PostgreSQL>()
                                            : storm::create_table_sql<Model>();
            ASSERT_TRUE(conn->execute(ddl).has_value()) << ddl;
        }

        static auto seed(std::string_view sql) -> void {
            const auto& conn = storm::QuerySet<Person, ConnType>::get_default_connection();
            ASSERT_TRUE(conn->execute(std::string(sql)).has_value()) << sql;
        }

        static auto row_count() -> int {
            storm::QuerySet<Model, ConnType> qs;
            auto                             result = qs.count().execute();
            return result.has_value() ? static_cast<int>(result.value()) : -1;
        }

        // The single row matching `filter`, or nullopt when the filter matches
        // nothing. Every "did the right row change?" assertion below reads
        // through this, so the key-matching check is written once.
        static auto find_one(const auto& filter) -> std::optional<Model> {
            storm::QuerySet<Model, ConnType> qs;
            auto                             rows = qs.where(filter).select().execute();
            if (!rows.has_value() || rows.value().empty()) {
                return std::nullopt;
            }
            return *rows.value().begin();
        }
    };

    // --- OrderLine: 2-part int key -------------------------------------------

    template <typename ConnType> class OrderLineTest : public CompositePkFixture<OrderLine, ConnType> {
      public:
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            this->seed(
                    "INSERT INTO OrderLine (order_id, product_id, quantity, note) VALUES "
                    "(1, 10, 5, 'a'), (1, 20, 7, 'b'), (2, 10, 9, 'c'), (2, 20, 11, 'd')"
            );
        }

        // quantity of one key, or -1 when the row is gone.
        static auto quantity_of(int order_id, int product_id) -> int {
            using storm::orm::where::f;
            auto row = OrderLineTest::find_one(
                    f<^^OrderLine::order_id>() == order_id && f<^^OrderLine::product_id>() == product_id
            );
            return row ? row->quantity : -1;
        }
    };

} // namespace

TYPED_TEST_SUITE(OrderLineTest, DatabaseTypes);

// --- DELETE: single row -----------------------------------------------------

TYPED_TEST(OrderLineTest, DeleteSingleMatchesFullKeyOnly) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       target{.order_id = 1, .product_id = 20};
    ASSERT_TRUE(qs.erase(target).execute().has_value());

    EXPECT_EQ(this->row_count(), 3);
    EXPECT_EQ(this->quantity_of(1, 20), -1) << "the full-key row is deleted";
    EXPECT_EQ(this->quantity_of(1, 10), 5) << "a row sharing only order_id survives";
    EXPECT_EQ(this->quantity_of(2, 20), 11) << "a row sharing only product_id survives";
}

// The core failure this issue prevents: with a single-column WHERE, deleting
// (1,20) would also take (1,10) — every row sharing the first key part.
TYPED_TEST(OrderLineTest, DeletePartialKeyMatchDoesNotDelete) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    // order_id matches rows of order 1, but no row of order 1 has product_id 99.
    const OrderLine target{.order_id = 1, .product_id = 99};
    ASSERT_TRUE(qs.erase(target).execute().has_value());
    EXPECT_EQ(this->row_count(), 4) << "no row matches the FULL key, so nothing is deleted";
}

TYPED_TEST(OrderLineTest, DeleteNoMatchIsNotAnError) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       target{.order_id = 77, .product_id = 88};
    EXPECT_TRUE(qs.erase(target).execute().has_value());
    EXPECT_EQ(this->row_count(), 4);
}

// --- DELETE: batch ----------------------------------------------------------

TYPED_TEST(OrderLineTest, DeleteBatchMatchesEachFullKey) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          targets{
                     {.order_id = 1, .product_id = 10},
                     {.order_id = 2, .product_id = 20},
    };
    ASSERT_TRUE(qs.erase(std::span<const OrderLine>(targets)).execute().has_value());

    // The listed keys CROSS: (1,10) and (2,20) are given, (1,20) and (2,10) are
    // not. A per-column IN list — "order_id IN (1,2) AND product_id IN (10,20)"
    // — would wrongly delete all four. Only a row-value list gets this right.
    EXPECT_EQ(this->row_count(), 2) << "exactly the two listed keys, not their cross product";
    EXPECT_EQ(this->quantity_of(1, 10), -1);
    EXPECT_EQ(this->quantity_of(2, 20), -1);
    EXPECT_EQ(this->quantity_of(1, 20), 7) << "an unlisted key survives";
    EXPECT_EQ(this->quantity_of(2, 10), 9) << "an unlisted key survives";
}

TYPED_TEST(OrderLineTest, DeleteEmptyBatchIsANoOp) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          empty;
    EXPECT_TRUE(qs.erase(std::span<const OrderLine>(empty)).execute().has_value());
    EXPECT_EQ(this->row_count(), 4);
}

// --- UPDATE: single row -----------------------------------------------------

TYPED_TEST(OrderLineTest, UpdateSingleMatchesFullKeyOnly) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       updated{.order_id = 1, .product_id = 20, .quantity = 700, .note = "B"};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_EQ(this->quantity_of(1, 20), 700) << "the full-key row is updated";
    EXPECT_EQ(this->quantity_of(1, 10), 5) << "a row sharing only order_id is untouched";
    EXPECT_EQ(this->quantity_of(2, 20), 11) << "a row sharing only product_id is untouched";
}

TYPED_TEST(OrderLineTest, UpdatePartialKeyMatchUpdatesNothing) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       updated{.order_id = 1, .product_id = 99, .quantity = 700};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_EQ(this->quantity_of(1, 10), 5);
    EXPECT_EQ(this->quantity_of(1, 20), 7);
    EXPECT_EQ(this->row_count(), 4) << "UPDATE never inserts";
}

// The bind offsets are what this asserts: the two SET values occupy
// placeholders 1-2, so the key values must land at 3-4. Binding the key at a
// stale offset would either write the wrong row or fail outright.
TYPED_TEST(OrderLineTest, UpdateBindsEveryKeyPartAtTheRightOffset) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       updated{.order_id = 2, .product_id = 10, .quantity = 42, .note = "z"};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_EQ(this->quantity_of(2, 10), 42) << "the SET values and the key both landed correctly";
    EXPECT_EQ(this->quantity_of(2, 20), 11) << "the second key part was bound, not defaulted";
    EXPECT_EQ(this->quantity_of(1, 10), 5) << "the first key part was bound, not defaulted";
}

// --- UPDATE: batch ----------------------------------------------------------

TYPED_TEST(OrderLineTest, UpdateBatchAppliesPerKey) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          updates{
                     {.order_id = 1, .product_id = 10, .quantity = 100, .note = "x"},
                     {.order_id = 2, .product_id = 20, .quantity = 200, .note = "y"},
    };
    ASSERT_TRUE(qs.update(std::span<const OrderLine>(updates)).execute().has_value());

    EXPECT_EQ(this->quantity_of(1, 10), 100);
    EXPECT_EQ(this->quantity_of(2, 20), 200);
    EXPECT_EQ(this->quantity_of(1, 20), 7) << "an unlisted key is untouched";
    EXPECT_EQ(this->quantity_of(2, 10), 9) << "an unlisted key is untouched";
}

TYPED_TEST(OrderLineTest, UpdateEmptyBatchIsANoOp) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          empty;
    EXPECT_TRUE(qs.update(std::span<const OrderLine>(empty)).execute().has_value());
    EXPECT_EQ(this->quantity_of(1, 10), 5);
}

// ── Mixed-type key parts (int + std::string) ─────────────────────────────────

namespace {
    template <typename ConnType> class InventoryTest : public CompositePkFixture<Inventory, ConnType> {
      public:
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            this->seed(
                    "INSERT INTO Inventory (warehouse, sku, on_hand) VALUES "
                    "(1, 'apple', 5), (1, 'pear', 7), (2, 'apple', 9)"
            );
        }

        static auto on_hand_of(int warehouse, std::string_view sku) -> int {
            using storm::orm::where::f;
            auto row = InventoryTest::find_one(
                    f<^^Inventory::warehouse>() == warehouse && f<^^Inventory::sku>() == std::string(sku)
            );
            return row ? row->on_hand : -1;
        }
    };
} // namespace

TYPED_TEST_SUITE(InventoryTest, DatabaseTypes);

TYPED_TEST(InventoryTest, DeleteWithTextKeyPart) {
    storm::QuerySet<Inventory, TypeParam> qs;
    const Inventory                       target{.warehouse = 1, .sku = "apple"};
    ASSERT_TRUE(qs.erase(target).execute().has_value());

    EXPECT_EQ(this->on_hand_of(1, "apple"), -1);
    EXPECT_EQ(this->on_hand_of(1, "pear"), 7) << "same warehouse, different sku survives";
    EXPECT_EQ(this->on_hand_of(2, "apple"), 9) << "same sku, different warehouse survives";
}

TYPED_TEST(InventoryTest, UpdateWithTextKeyPart) {
    storm::QuerySet<Inventory, TypeParam> qs;
    const Inventory                       updated{.warehouse = 2, .sku = "apple", .on_hand = 900};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_EQ(this->on_hand_of(2, "apple"), 900);
    EXPECT_EQ(this->on_hand_of(1, "apple"), 5) << "the text part alone must not match";
}

TYPED_TEST(InventoryTest, DeleteBatchWithTextKeyPart) {
    storm::QuerySet<Inventory, TypeParam> qs;
    const std::vector<Inventory>          targets{
                     {.warehouse = 1, .sku = "pear"},
                     {.warehouse = 2, .sku = "apple"},
    };
    ASSERT_TRUE(qs.erase(std::span<const Inventory>(targets)).execute().has_value());

    EXPECT_EQ(this->on_hand_of(1, "apple"), 5) << "unlisted key survives";
    EXPECT_EQ(this->on_hand_of(1, "pear"), -1);
    EXPECT_EQ(this->on_hand_of(2, "apple"), -1);
}

// ── Three-part key ───────────────────────────────────────────────────────────

namespace {
    template <typename ConnType> class LedgerTest : public CompositePkFixture<Ledger, ConnType> {
      public:
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            this->seed(
                    "INSERT INTO Ledger (region, account, period, balance) VALUES "
                    "(1, 'cash', 202601, 10.5), (1, 'cash', 202602, 20.5), (1, 'debt', 202601, 30.5)"
            );
        }

        static auto balance_of(int region, std::string_view account, std::int64_t period) -> double {
            using storm::orm::where::f;
            auto row = LedgerTest::find_one(
                    f<^^Ledger::region>() == region && f<^^Ledger::account>() == std::string(account) &&
                    f<^^Ledger::period>() == period
            );
            return row ? row->balance : -1.0;
        }
    };
} // namespace

TYPED_TEST_SUITE(LedgerTest, DatabaseTypes);

// Two of the three parts match — the row must NOT be touched. This is the case
// a 2-of-3 AND-join would get wrong.
TYPED_TEST(LedgerTest, DeleteRequiresAllThreeParts) {
    storm::QuerySet<Ledger, TypeParam> qs;
    const Ledger                       target{.region = 1, .account = "cash", .period = 209912};
    ASSERT_TRUE(qs.erase(target).execute().has_value());

    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202601), 10.5) << "no row matches all three parts";
    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202602), 20.5);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "debt", 202601), 30.5);
}

TYPED_TEST(LedgerTest, DeleteThreePartKey) {
    storm::QuerySet<Ledger, TypeParam> qs;
    const Ledger                       target{.region = 1, .account = "cash", .period = 202602};
    ASSERT_TRUE(qs.erase(target).execute().has_value());

    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202602), -1.0);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202601), 10.5);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "debt", 202601), 30.5);
}

TYPED_TEST(LedgerTest, UpdateThreePartKey) {
    storm::QuerySet<Ledger, TypeParam> qs;
    const Ledger                       updated{.region = 1, .account = "debt", .period = 202601, .balance = 99.5};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_DOUBLE_EQ(this->balance_of(1, "debt", 202601), 99.5);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202601), 10.5) << "differs in one part — untouched";
}

TYPED_TEST(LedgerTest, DeleteBatchThreePartKey) {
    storm::QuerySet<Ledger, TypeParam> qs;
    const std::vector<Ledger>          targets{
                     {.region = 1, .account = "cash", .period = 202601},
                     {.region = 1, .account = "debt", .period = 202601},
    };
    ASSERT_TRUE(qs.erase(std::span<const Ledger>(targets)).execute().has_value());

    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202601), -1.0);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "debt", 202601), -1.0);
    EXPECT_DOUBLE_EQ(this->balance_of(1, "cash", 202602), 20.5) << "unlisted key survives";
}

// ── FK key parts: the association-table shape ────────────────────────────────
// An FK part binds the REFERENCED row's key, not the whole object, and names the
// "<name>_id" column. Executing it is the only way to catch a mismatch between
// the two — a bare-member column name fails at prepare time, and binding the
// wrong value silently hits the wrong row.

namespace {
    template <typename ConnType> class StockEntryTest : public CompositePkFixture<StockEntry, ConnType> {
      public:
        // The FK target table must exist before the referencing table.
        auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
            ASSERT_TRUE((storm::test::ensure_tables<ConnType, Person>(conn))) << "Failed to create Person";
            CompositePkFixture<StockEntry, ConnType>::on_setup(conn);
        }

        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            storm::QuerySet<Person, ConnType> people;
            for (int i = 1; i <= 2; ++i) {
                const Person person{.id = i, .name = std::format("W{}", i), .age = 30};
                ASSERT_TRUE(people.insert(person).execute().has_value());
            }
            this->seed("INSERT INTO StockEntry (warehouse_id, sku, qty) VALUES (1, 10, 5), (1, 20, 7), (2, 10, 9)");
        }

        // Selects every row and matches the key in C++ rather than in a WHERE clause.
        // Filtering with f<^^StockEntry::warehouse>() would compare the FK MEMBER rather
        // than its "warehouse_id" column — an unrelated gap in the WHERE layer (nothing
        // in the tree filters on an FK member today), and leaning on it here would make
        // this test fail for a reason unrelated to the key binding it exists to check.
        // The tables hold three rows, so the scan costs nothing.
        static auto qty_of(int warehouse_id, int sku) -> int {
            storm::QuerySet<StockEntry, ConnType> qs;
            auto                                  rows = qs.select().execute();
            if (!rows.has_value()) {
                return -1;
            }
            for (const StockEntry& row : rows.value()) {
                if (row.warehouse.id == warehouse_id && row.sku == sku) {
                    return row.qty;
                }
            }
            return -1;
        }
    };
} // namespace

TYPED_TEST_SUITE(StockEntryTest, DatabaseTypes);

TYPED_TEST(StockEntryTest, DeleteByFkCompositeKey) {
    storm::QuerySet<StockEntry, TypeParam> qs;
    const StockEntry                       target{.warehouse = {.id = 1}, .sku = 20};
    ASSERT_TRUE(qs.erase(target).execute().has_value());

    EXPECT_EQ(this->qty_of(1, 20), -1) << "the full-key row is deleted";
    EXPECT_EQ(this->qty_of(1, 10), 5) << "same warehouse, different sku survives";
    EXPECT_EQ(this->qty_of(2, 10), 9) << "same sku, different warehouse survives";
}

TYPED_TEST(StockEntryTest, UpdateByFkCompositeKey) {
    storm::QuerySet<StockEntry, TypeParam> qs;
    const StockEntry                       updated{.warehouse = {.id = 2}, .sku = 10, .qty = 900};
    ASSERT_TRUE(qs.update(updated).execute().has_value());

    EXPECT_EQ(this->qty_of(2, 10), 900);
    EXPECT_EQ(this->qty_of(1, 10), 5) << "the sku part alone must not match";
}

TYPED_TEST(StockEntryTest, DeleteBatchByFkCompositeKey) {
    storm::QuerySet<StockEntry, TypeParam> qs;
    const std::vector<StockEntry>          targets{
                     {.warehouse = {.id = 1}, .sku = 10},
                     {.warehouse = {.id = 2}, .sku = 10},
    };
    ASSERT_TRUE(qs.erase(std::span<const StockEntry>(targets)).execute().has_value());

    EXPECT_EQ(this->qty_of(1, 10), -1);
    EXPECT_EQ(this->qty_of(2, 10), -1);
    EXPECT_EQ(this->qty_of(1, 20), 7) << "unlisted key survives";
}

// ── Chunking: a batch that crosses the shrunken chunk boundary ───────────────
// The 999-variable ceiling is spent N params per row, so a 2-part key chunks at
// 399 rows rather than 799. 900 rows forces the max-chunk statement to run
// twice plus a remainder.

namespace {
    template <typename ConnType> class LargeBatchTest : public CompositePkFixture<OrderLine, ConnType> {
      public:
        static constexpr int ROWS = 900;

        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            std::string sql = "INSERT INTO OrderLine (order_id, product_id, quantity, note) VALUES ";
            for (int i = 0; i < ROWS; ++i) {
                if (i > 0) {
                    sql += ", ";
                }
                sql += std::format("({}, {}, {}, 'n')", i, i * 2, i);
            }
            this->seed(sql);
        }

        // Keys (i, 2i) for i in [from, ROWS).
        static auto keys_from(int from) -> std::vector<OrderLine> {
            std::vector<OrderLine> targets;
            targets.reserve(static_cast<std::size_t>(ROWS - from));
            for (int i = from; i < ROWS; ++i) {
                targets.push_back({.order_id = i, .product_id = i * 2});
            }
            return targets;
        }
    };
} // namespace

TYPED_TEST_SUITE(LargeBatchTest, DatabaseTypes);

TYPED_TEST(LargeBatchTest, ChunkedDeleteSpansMultipleChunks) {
    const std::vector<OrderLine>          targets = LargeBatchTest<TypeParam>::keys_from(0);
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.erase(std::span<const OrderLine>(targets)).execute().has_value());
    EXPECT_EQ(this->row_count(), 0) << "every key across every chunk is deleted";
}

// The same batch minus one key: the survivor proves the chunked path deletes the
// keys it was given rather than everything it touched.
TYPED_TEST(LargeBatchTest, ChunkedDeleteLeavesUnlistedKeys) {
    const std::vector<OrderLine>          targets = LargeBatchTest<TypeParam>::keys_from(1);
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.erase(std::span<const OrderLine>(targets)).execute().has_value());
    EXPECT_EQ(this->row_count(), 1) << "the one unlisted key survives";
}

TYPED_TEST(LargeBatchTest, ChunkedUpdateSpansMultipleChunks) {
    std::vector<OrderLine> updates = LargeBatchTest<TypeParam>::keys_from(0);
    for (OrderLine& row : updates) {
        row.quantity = 1000 + row.order_id;
        row.note     = "u";
    }
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.update(std::span<const OrderLine>(updates)).execute().has_value());

    using storm::orm::where::f;
    storm::QuerySet<OrderLine, TypeParam> check;
    auto                                  updated = check.where(f<^^OrderLine::quantity>() >= 1000).count().execute();
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated.value(), LargeBatchTest<TypeParam>::ROWS);
}

// ── Single-PK regression ─────────────────────────────────────────────────────
// Person is the long-standing CRUD fixture. Its UPDATE/DELETE behaviour must be
// unchanged by the composite widening.

namespace {
    template <typename ConnType> class SinglePkRegressionTest : public StormTestFixture<Person, ConnType> {
      public:
        auto on_after_setup(const std::shared_ptr<ConnType>& /*conn*/) -> void override {
            storm::QuerySet<Person, ConnType> qs;
            for (int i = 1; i <= 3; ++i) {
                const Person person{.id = i, .name = std::format("P{}", i), .age = 20 + i};
                ASSERT_TRUE(qs.insert(person).execute().has_value());
            }
        }

        static auto age_of(int person_id) -> int {
            using storm::orm::where::f;
            storm::QuerySet<Person, ConnType> qs;
            auto                              rows = qs.where(f<^^Person::id>() == person_id).select().execute();
            if (!rows.has_value() || rows.value().empty()) {
                return -1;
            }
            return rows.value().begin()->age;
        }
    };
} // namespace

TYPED_TEST_SUITE(SinglePkRegressionTest, DatabaseTypes);

TYPED_TEST(SinglePkRegressionTest, SingleDeleteStillWorks) {
    storm::QuerySet<Person, TypeParam> qs;
    const Person                       target{.id = 2};
    ASSERT_TRUE(qs.erase(target).execute().has_value());
    EXPECT_EQ(this->age_of(2), -1);
    EXPECT_EQ(this->age_of(1), 21);
}

TYPED_TEST(SinglePkRegressionTest, SingleUpdateStillWorks) {
    storm::QuerySet<Person, TypeParam> qs;
    const Person                       updated{.id = 3, .name = "P3", .age = 99};
    ASSERT_TRUE(qs.update(updated).execute().has_value());
    EXPECT_EQ(this->age_of(3), 99);
    EXPECT_EQ(this->age_of(1), 21);
}

TYPED_TEST(SinglePkRegressionTest, BatchDeleteStillWorks) {
    storm::QuerySet<Person, TypeParam> qs;
    const std::vector<Person>          targets{{.id = 1}, {.id = 3}};
    ASSERT_TRUE(qs.erase(std::span<const Person>(targets)).execute().has_value());
    EXPECT_EQ(this->age_of(1), -1);
    EXPECT_EQ(this->age_of(3), -1);
    EXPECT_EQ(this->age_of(2), 22);
}

// NOLINTEND(readability-implicit-bool-conversion)
