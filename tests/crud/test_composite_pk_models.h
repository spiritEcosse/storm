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

// #504 Task 7 fix — FK target with a 3-part composite PK. Ledger already
// exists (above) as a plain composite-PK model but was never used as an FK
// target anywhere, so no test exercised the JOIN ON-clause AND-chain past
// N=2 parts. LedgerEntryRef is a single such FK (3 parts, deficit 6 bytes
// under the pre-fix sizer) — kept as the minimal round-trip/JOIN-execution
// fixture. It is NOT wide enough on its own to overflow the stacked
// SMALL_BUFFER slack (calculate_complete_sql_size / calculate_join_sql_size /
// calculate_select_fields_size each add their own 10-byte pad, so ~30 bytes of
// slack absorb a single 6-byte deficit) — see LedgerEntryRefWide below for the
// fixture that actually forces a measurable, non-slack-absorbed deficit.
struct LedgerEntryRef {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] Ledger ledger;
    std::string note;
};

// #504 Task 7 fix — SIX composite-FK fields to Ledger (3 parts each), mirroring
// the WideJoin idiom (9 single-column FKs) but for composite targets. Each FK's
// ON-clause body has 2 "after-first" parts, so each contributes a 6-byte
// deficit under the pre-fix sizer (total 36 bytes) — enough to exceed the ~30
// bytes of stacked SMALL_BUFFER slack across calculate_complete_sql_size /
// calculate_join_sql_size / calculate_select_fields_size, where a single
// 3-part FK (LedgerEntryRef above) does not. This is the fixture that actually
// forces ConstexprString::append to silently truncate under the pre-fix sizer.
struct LedgerEntryRefWide {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] Ledger ledger1;
    [[= storm::fk<>]] Ledger ledger2;
    [[= storm::fk<>]] Ledger ledger3;
    [[= storm::fk<>]] Ledger ledger4;
    [[= storm::fk<>]] Ledger ledger5;
    [[= storm::fk<>]] Ledger ledger6;
};

// #504 review fix — a NULLABLE composite-FK member. Proves the composite-FK
// DDL column emission gates NOT NULL on the FK MEMBER's own optionality
// (std::optional<OrderLineWithShipments>), not on the target's PK-part type
// (which is never optional — a PK can't be nullable), and that it emits the
// suffix exactly once, not doubled.
struct OptionalShipment {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] std::optional<OrderLineWithShipments> line;
    std::string carrier;
};

// #504 review fix — a composite-FK member declared BEFORE an optional
// single-column-FK member in the same struct. Exercises extract_column_fast's
// col_idx threading: `composite` consumes 2 SQL columns
// (composite_order_id, composite_product_id), so `single` (an optional FK
// targeting single-PK Person) must read from col_idx 3, not from its member
// index (2). Before the fix, extract_optional_fk_column read column 2
// (composite_product_id) instead.
struct MixedFkOrder {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] OrderLineWithShipments composite;
    [[= storm::fk<>]] std::optional<Person> single;
};

#endif // STORM_TESTS_TEST_COMPOSITE_PK_MODELS_H
