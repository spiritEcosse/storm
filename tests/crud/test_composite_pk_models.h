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

#endif // STORM_TESTS_TEST_COMPOSITE_PK_MODELS_H
