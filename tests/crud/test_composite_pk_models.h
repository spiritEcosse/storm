#ifndef STORM_TESTS_TEST_COMPOSITE_PK_MODELS_H
#define STORM_TESTS_TEST_COMPOSITE_PK_MODELS_H

/**
 * @file test_composite_pk_models.h
 * @brief Composite-PK model fixtures shared by the #501 UPDATE/DELETE tests.
 *
 * IMPORTANT: like test_models.h, this header names storm:: types and must only
 * be included AFTER `import storm;` (NOSONAR cpp:S954 at the include site).
 *
 * Split across two test TUs: test_composite_pk_sql.cpp (compile-time gates and
 * SQL text) and test_composite_pk_crud.cpp (live execution on both backends).
 */

// Canonical 2-part composite key, both parts int.
struct OrderLine {
    [[= storm::primary_part]] int order_id{};
    [[= storm::primary_part]] int product_id{};
    int quantity{};
    std::string note;
};

// Mixed-type parts (int + std::string): proves the bind path dispatches per
// part type rather than assuming an integer key, and that a TEXT part is
// compared correctly in the WHERE clause.
struct Inventory {
    [[= storm::primary_part]] int warehouse{};
    [[= storm::primary_part]] std::string sku;
    int on_hand{};
};

// Three parts, mixed widths — the AND-join and the row-value list must not be
// arity-limited, and the consteval sizers must stay exact at N=3.
struct Ledger {
    [[= storm::primary_part]] int region{};
    [[= storm::primary_part]] std::string account;
    [[= storm::primary_part]] std::int64_t period{};
    double balance{};
};

// Single-PK control. Every SQL assertion on this model must be byte-identical
// to what Storm emitted before composite support.
struct Widget {
    [[= storm::primary]] int id{};
    std::string name;
    int weight{};
};

// The canonical association-table shape: a composite key whose parts are FKs.
// Exercises the `is_fk_field` branch of bind_one_pk_part (an FK part binds the
// REFERENCED row's key, not the whole object) and the "<name>_id" column naming
// in the WHERE clause — emitting the bare member would name a column that does
// not exist. `Person` comes from test_models.h.
struct StockEntry {
    [[= storm::primary_part]][[= storm::fk<>]] Person warehouse;
    [[= storm::primary_part]] int sku{};
    int qty{};
};

// #504 — Base⟷Owner reference cycle between OrderLineWithShipments and
// Shipment, broken the same way tests/query/test_reverse_fk_models.h breaks
// RfPerson⟷RfTask: forward-declare the owner so the container member below
// tolerates an incomplete value_type, then define it fully afterward (by
// which point OrderLineWithShipments is complete, so Shipment::line can be a
// by-value member).
struct Shipment; // forward declaration breaks the Base⟷Owner cycle

// #504 — reverse-FK destination with a composite-PK owner: "all OrderLines,
// each with the Shipments that reference them". A separate struct from
// OrderLine (not added to it directly) so existing OrderLine-shape assertions
// in the #501/#502 test suites stay untouched.
struct OrderLineWithShipments {
    [[= storm::primary_part]] int order_id{};
    [[= storm::primary_part]] int product_id{};
    int quantity{};
    std::string note;
    [[= storm::reverse_fk<^^Shipment>]] std::vector<Shipment> shipments;
};

// #504 — FK targeting a composite PK. Shipment references
// OrderLineWithShipments's 2-part key (order_id, product_id) via the SAME
// fk<> annotation as any single-column FK — the composite-ness lives entirely
// in the target's own declaration. Doubles as the reverse-FK owner for
// OrderLineWithShipments::shipments above: reverse_fk<^^Shipment> requires
// Shipment to carry exactly one fk<> member whose type is
// OrderLineWithShipments, which is why this targets that type and not the
// plain OrderLine (which nothing references back to).
struct Shipment {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] OrderLineWithShipments line;
    std::string carrier;
};

// #504 — many-to-many with a composite-PK side. LedgerTag is the plain
// single-PK related model; LedgerWithTags is Ledger's 3-part composite key
// (region, account, period) PLUS an m2m container, proving m2m eager-load
// stitching works when the OWNER side has a composite key.
struct LedgerTag {
    [[= storm::primary_autoincrement]] int id{};
    std::string label;
};
struct LedgerWithTags {
    [[= storm::primary_part]] int region{};
    [[= storm::primary_part]] std::string account;
    [[= storm::primary_part]] std::int64_t period{};
    double balance{};
    [[= storm::many_to_many<>]] std::vector<LedgerTag> tags;
};

#endif // STORM_TESTS_TEST_COMPOSITE_PK_MODELS_H
