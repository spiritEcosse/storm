# Composite PK INSERT (#502) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make INSERT work for composite-PK models: every key part joins the column list/placeholders/bind loop in declaration order, `.execute()` returns `std::expected<void, Error>` with no `RETURNING`, and an explicit `ReturnId::Yes` is a compile-time error.

**Architecture:** One consteval iterator (`FieldNameGrammar::for_each_field_name`) feeds the column list, its sizer, and the placeholders, so fixing its skip test fixes all three at once. The bind side reuses #501's `skips_pk_column` policy point. A new `ReturnIdSupported<T, R>` concept + `default_return_id<T>()` consteval function route composite models to the existing `ReturnId::No` fast path and reject `ReturnId::Yes` at the call site.

**Tech Stack:** C++26 reflection (custom clang-p2996), GoogleTest `TYPED_TEST` over SQLite + PostgreSQL, CMake presets `ninja-debug`/`ninja-release`.

**Spec:** `docs/superpowers/specs/2026-07-22-502-composite-pk-insert-design.md` (approved). Decision B recorded on issue #502 before implementation.

## Global Constraints

- Working directory: `/home/ihor/projects/storm/worktrees/feature-502-composite-pk-insert` (branch `feature/502-composite-pk-insert`, base `develop`).
- Single-PK INSERT SQL must stay **byte-identical**; the `RETURNING id` path unchanged (regression-asserted in Task 1).
- Tests are written BEFORE implementation and must FAIL first (CLAUDE.md rule 9). For concept tests the failure mode is a compile error — that counts.
- **ONE code commit at the end** (Task 10), not per-task: CLAUDE.md rule 8 requires code + docs + agent files to commit together, and the pre-commit hook (format → tidy → tests → coverage) costs ~9 min per run. This deviates from "frequent commits" deliberately, matching how #500/#501 landed.
- `storm-code-reviewer` agent MUST run on the staged diff before the commit (rule 13).
- NEVER run sanitizer presets locally — CI runs them (rule 7).
- Benchmark before committing (rule 6): Release build, single-PK INSERT within noise, revert if ANY slowdown.
- Sonar rules apply to new code: `||`/`&&` not `or`/`and` (S3659), no commented-out code (S125), no TODO comments (S1135).
- Build quirk: module cache corruption — if a build fails oddly, run the build a second time before debugging.
- Do NOT modify the existing UPDATE/DELETE fixtures' raw-SQL seeds in `test_composite_pk_crud.cpp` — those tests must stay independent of the INSERT path.

## File Map

- Modify: `src/orm/statements/field_names.cppm` — skip guard in `for_each_field_name` (Task 2)
- Modify: `src/orm/statements/insert.cppm` — `default_return_id`, `ReturnIdSupported`, `query()` constraints, `placeholders_count()` (Tasks 2, 4)
- Modify: `src/orm/queryset.cppm` — `insert()` overload defaults + constraints (Task 4)
- Modify: `src/orm/statements/base.cppm` — `skips_pk_column` composite branch + stale comments (Task 6)
- Modify: `tests/crud/test_composite_pk_sql.cpp` — INSERT SQL text + concept asserts (Tasks 1, 3)
- Modify: `tests/crud/test_composite_pk_crud.cpp` — live INSERT tests (Task 5)
- Modify: `CLAUDE.md`, `docs/guide/reference/FIELD_TYPES.md` — docs (Task 9)

Model fixtures already exist in `tests/crud/test_composite_pk_models.h` (from #501) and are reused unchanged: `OrderLine` (2 int parts + `quantity`/`note`), `Inventory` (int + string parts + `on_hand`), `Ledger` (3 parts int/string/int64 + `balance`), `StockEntry` (FK part `warehouse` referencing `Person` + int `sku` + `qty`), `Widget` (single-PK control: `id`/`name`/`weight`).

---

### Task 1: Failing SQL-text tests for composite INSERT

**Files:**
- Modify: `tests/crud/test_composite_pk_sql.cpp`

**Interfaces:**
- Consumes: `storm::orm::statements::InsertStatement<T, ConnType>` (re-exported by `import storm;`). Its public nested `VoidQuery::sql()` and `SingleQuery::sql()` are `static` — the text derives purely from reflection, no live connection needed.
- Produces: the pinned SQL strings that Task 2 must satisfy.

- [ ] **Step 1: Update the stale header comment**

In `tests/crud/test_composite_pk_sql.cpp`, replace the line:

```cpp
// INSERT stays out of scope (#502).
```

with:

```cpp
// INSERT joined in #502: a composite key is never DB-generated, so every part
// is caller data — all key columns appear in the INSERT, and no RETURNING is
// emitted (there is no generated value to return).
```

- [ ] **Step 2: Append the INSERT SQL-text section**

Add at the end of the file, BEFORE the closing `// NOLINTEND(readability-implicit-bool-conversion)`:

```cpp
// ── (#502) SQL text: INSERT ──────────────────────────────────────────────────
// A composite PK has no auto-generation mechanism (AUTOINCREMENT and
// GENERATED ... AS IDENTITY are single-integer-column features), so every part
// is caller data: all key columns join the column list and placeholder list in
// DECLARATION order, and no RETURNING clause is emitted.

namespace {

    // VoidQuery::sql()/SingleQuery::sql() are static: the text derives purely
    // from reflection, so no live connection is involved (same rationale as the
    // EraseGrammar asserts above).
    template <typename T> auto insert_void_sql() -> std::string {
        return storm::orm::statements::InsertStatement<T, storm::db::sqlite::Connection>::VoidQuery::sql();
    }
    template <typename T> auto insert_returning_sql() -> std::string {
        return storm::orm::statements::InsertStatement<T, storm::db::sqlite::Connection>::SingleQuery::sql();
    }

} // namespace

TEST(CompositePkInsertSql, EveryKeyPartJoinsTheColumnList) {
    EXPECT_EQ(
            insert_void_sql<OrderLine>(),
            "INSERT INTO OrderLine (order_id, product_id, quantity, note) VALUES (?, ?, ?, ?)"
    );
}

TEST(CompositePkInsertSql, ThreePartKeyInDeclarationOrder) {
    EXPECT_EQ(insert_void_sql<Ledger>(), "INSERT INTO Ledger (region, account, period, balance) VALUES (?, ?, ?, ?)");
}

// An FK part's COLUMN is "<name>_id" — emitting the bare member would INSERT
// into a column that does not exist. Same reason #500 asserts this for DDL.
TEST(CompositePkInsertSql, FkKeyPartUsesColumnNameNotIdentifier) {
    EXPECT_EQ(insert_void_sql<StockEntry>(), "INSERT INTO StockEntry (warehouse_id, sku, qty) VALUES (?, ?, ?)");
}

TEST(CompositePkInsertSql, MixedTypeKeyParts) {
    EXPECT_EQ(insert_void_sql<Inventory>(), "INSERT INTO Inventory (warehouse, sku, on_hand) VALUES (?, ?, ?)");
}

// Single-PK INSERT text must be byte-identical to before this issue, on both
// the void and the RETURNING variants.
TEST(CompositePkInsertSql, SinglePkTextIsByteIdentical) {
    EXPECT_EQ(insert_void_sql<Widget>(), "INSERT INTO Widget (name, weight) VALUES (?, ?)");
    EXPECT_EQ(insert_returning_sql<Widget>(), "INSERT INTO Widget (name, weight) VALUES (?, ?) RETURNING id");
}
```

- [ ] **Step 3: Build and run — the composite tests MUST fail**

```bash
cmake --build --preset ninja-debug
./build/debug/tests/storm_tests --gtest_filter="CompositePkInsertSql.*"
```

Expected: `SinglePkTextIsByteIdentical` PASSES; the four composite tests FAIL — today's text drops the FIRST key part only (e.g. `INSERT INTO OrderLine (product_id, quantity, note) VALUES (?, ?, ?)`), because `for_each_field_name` skips on `member == primary_key_` (the first part). If the build fails oddly, run it a second time (module cache).

No commit yet (single-commit policy — see Global Constraints).

---

### Task 2: Grammar fix — column list, placeholders, and `placeholders_count()`

**Files:**
- Modify: `src/orm/statements/field_names.cppm:27-38` (`for_each_field_name`)
- Modify: `src/orm/statements/field_names.cppm:83-91` (comments only)
- Modify: `src/orm/statements/insert.cppm:526-531` (`placeholders_count`)

**Interfaces:**
- Consumes: `Base::has_composite_pk_` (public `static constexpr bool` on `BaseStatement`, from #500), `Base::primary_key_`, `Base::field_count_`, `Base::all_members_`.
- Produces: for composite models, `build_non_pk_field_names_list()` / `build_placeholders()` / `calculate_field_names_size_impl<true>()` now cover ALL fields; for single-PK models output is byte-identical. `placeholders_count()` returns `field_count_` for composite, `field_count_ - 1` for single-PK.

- [ ] **Step 1: Gate the skip on composite-ness in `for_each_field_name`**

In `src/orm/statements/field_names.cppm`, replace:

```cpp
        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (Base::all_members_[i] == Base::primary_key_) {
                        continue;
                    }
                }
                body(i, !first);
                first = false;
            }
        }
```

with:

```cpp
        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    // Only a DB-generated key is omitted from INSERT, and only a
                    // single-column PK can be DB-generated. A composite key has no
                    // auto-generation mechanism — every part is caller data (#502) —
                    // so nothing is skipped. Plain `if` on has_composite_pk_: the
                    // loop variable makes `if constexpr` on all_members_[i] ill-formed.
                    if (!Base::has_composite_pk_ && Base::all_members_[i] == Base::primary_key_) {
                        continue;
                    }
                }
                body(i, !first);
                first = false;
            }
        }
```

- [ ] **Step 2: Update the two now-imprecise comments in the same file**

Replace:

```cpp
        // Build comma-separated list of NON-PRIMARY KEY fields (for INSERT statements)
        // Excludes primary key to allow auto-increment
        static consteval auto build_non_pk_field_names_list() {
```

with:

```cpp
        // Build the INSERT column list: excludes a DB-generated single-column PK
        // (auto-increment); a composite key is caller data, so all fields (#502).
        static consteval auto build_non_pk_field_names_list() {
```

Replace:

```cpp
        // "?, ?, ..." placeholders for the SQL VALUES clause, one per non-PK field
        // (skips the primary key for auto-increment). Shared by InsertStatement's
        // VALUES clause and UpsertGrammar's re-derived INSERT prefix (#205).
        static consteval auto build_placeholders() {
```

with:

```cpp
        // "?, ?, ..." placeholders for the SQL VALUES clause, one per INSERTed field
        // (a DB-generated single-column PK is skipped; a composite key is not, #502).
        // Shared by InsertStatement's VALUES clause and UpsertGrammar's re-derived
        // INSERT prefix (#205).
        static consteval auto build_placeholders() {
```

- [ ] **Step 3: Widen `placeholders_count()` in insert.cppm**

Replace:

```cpp
        // Non-PK field count — the number of VALUES placeholders in a plain INSERT.
        // Used by the DO UPDATE upsert path to find where the auto_update now() tail
        // starts binding (right after the VALUES params).
        static consteval auto placeholders_count() -> std::size_t {
            return Base::field_count_ - 1;
        }
```

with:

```cpp
        // The number of VALUES placeholders in a plain INSERT: every field except a
        // DB-generated single-column PK; a composite key is caller data, so all
        // fields (#502). Used by the DO UPDATE upsert path to find where the
        // auto_update now() tail starts binding (right after the VALUES params).
        static consteval auto placeholders_count() -> std::size_t {
            return Base::has_composite_pk_ ? Base::field_count_ : Base::field_count_ - 1;
        }
```

Note: `placeholders_count()` is consumed only by the upsert bind path, which #503 will exercise on composite models; it is widened here because it is part of the placeholder-count DoD item and getting it wrong would misbind the `auto_update` tail one slot early.

- [ ] **Step 4: Build and run — Task 1's tests now pass**

```bash
cmake --build --preset ninja-debug
./build/debug/tests/storm_tests --gtest_filter="CompositePkInsertSql.*"
```

Expected: ALL `CompositePkInsertSql.*` PASS (the sizer, list, and placeholders share `for_each_field_name`, so they cannot disagree).

- [ ] **Step 5: Full-suite regression check**

```bash
ctest --preset ninja-debug --output-on-failure
```

Expected: all tests pass — single-PK output is byte-identical, and nothing else in-tree inserts composite models yet.

---

### Task 3: Failing concept tests — `ReturnIdSupported` + `default_return_id`

**Files:**
- Modify: `tests/crud/test_composite_pk_sql.cpp`

**Interfaces:**
- Consumes: names that DO NOT EXIST yet — `storm::orm::statements::ReturnIdSupported<T, R>` (concept) and `storm::orm::statements::default_return_id<T>()` (consteval function). That is the point: this task's "failure" is a compile error.
- Produces: the exact assertions Task 4's implementation must satisfy.

- [ ] **Step 1: Append the gate assertions**

Add at the end of `tests/crud/test_composite_pk_sql.cpp`, BEFORE the closing `// NOLINTEND(readability-implicit-bool-conversion)`:

```cpp
// ── (#502) ReturnId gate ─────────────────────────────────────────────────────
// ReturnId::Yes means "return the DB-generated key", which a composite model
// does not have. Unconstrained it would compile and emit "RETURNING <first
// part>" — the issue's rejected option C (silently lossy) through a back door.
// Asserted on the gate predicate, not via an ill-formed insert<Yes>() call —
// the established negative-compile pattern (a TU cannot contain the ill-formed
// call it asserts about).

namespace {

    using storm::orm::statements::default_return_id;
    using storm::orm::statements::ReturnId;
    using storm::orm::statements::ReturnIdSupported;

    static_assert(!ReturnIdSupported<OrderLine, ReturnId::Yes>, "a composite model has no DB-generated key to return");
    static_assert(ReturnIdSupported<OrderLine, ReturnId::No>, "opting OUT of the id is valid on every model shape");
    static_assert(ReturnIdSupported<Widget, ReturnId::Yes>, "single-PK models keep the RETURNING path");
    static_assert(ReturnIdSupported<Widget, ReturnId::No>, "single-PK models keep the explicit void path");
    static_assert(!ReturnIdSupported<Ledger, ReturnId::Yes>, "the gate is not arity-limited (3-part key)");
    static_assert(!ReturnIdSupported<StockEntry, ReturnId::Yes>, "an FK-part composite key is gated too");

    // Plain insert() picks its default from the model shape: void for composite
    // (nothing to return), id for single-PK (unchanged).
    static_assert(
            default_return_id<OrderLine>() == ReturnId::No,
            "plain insert() on a composite model resolves to the void path"
    );
    static_assert(
            default_return_id<Widget>() == ReturnId::Yes,
            "plain insert() on a single-PK model still returns the id"
    );

} // namespace
```

- [ ] **Step 2: Build — MUST fail to compile**

```bash
cmake --build --preset ninja-debug 2>&1 | tail -20
```

Expected: compile error — `no member named 'ReturnIdSupported' in namespace 'storm::orm::statements'` (or equivalent). This is the failing-first evidence for the concept work.

---

### Task 4: Implement the gate — `default_return_id`, `ReturnIdSupported`, constrained overloads

**Files:**
- Modify: `src/orm/statements/insert.cppm:27-32` (add the two names after `InsertOptions`)
- Modify: `src/orm/statements/insert.cppm:362,381` (constrain `query()` overloads)
- Modify: `src/orm/queryset.cppm:85-108` (constrain `insert()` overloads)

**Interfaces:**
- Consumes: `BaseStatement<T>::has_composite_pk_` (public, #500).
- Produces: `storm::orm::statements::default_return_id<T>() -> ReturnId` (consteval; `No` for composite, `Yes` for single-PK) and `storm::orm::statements::ReturnIdSupported<T, R>` (concept; false only for composite + `Yes`). Both live in the `export namespace` of `storm_orm_statements_insert`, so `import storm;` sees them (Task 3's asserts, Task 5's tests).

- [ ] **Step 1: Add the two names in insert.cppm**

Directly AFTER the `InsertOptions` struct (after its closing `};`, before the `template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement;` forward declaration), insert:

```cpp
    // A composite PK is never DB-generated (#502): AUTOINCREMENT and
    // GENERATED ... AS IDENTITY are single-integer-column features, so every key
    // part is caller-supplied and RETURNING could only echo the input back. Plain
    // insert() therefore defaults to the void/no-RETURNING path on a composite
    // model, and keeps returning the generated id on a single-PK model.
    template <typename T> consteval auto default_return_id() -> ReturnId {
        return BaseStatement<T>::has_composite_pk_ ? ReturnId::No : ReturnId::Yes;
    }

    // An explicit ReturnId::Yes on a composite model is rejected at the call
    // site: unconstrained it would emit "RETURNING <first part>" — silently
    // lossy. ReturnId::No stays valid on every model shape, so generic code that
    // spells it out keeps compiling. (Named concept per the #472/#477/#478
    // precedent; the #413 auto-DEFAULT caveat was checked and does not fire —
    // that DEFAULT is recovered from a C++ default-member-initializer, a value
    // already in the caller's object.)
    template <typename T, ReturnId R>
    concept ReturnIdSupported = (R == ReturnId::No) || !BaseStatement<T>::has_composite_pk_;
```

- [ ] **Step 2: Constrain the `query()` overloads in insert.cppm**

Replace:

```cpp
        template <ReturnId R = ReturnId::Yes> [[nodiscard]] auto query(const T& obj [[clang::lifetimebound]]) {
```

with:

```cpp
        template <ReturnId R = default_return_id<T>()>
            requires ReturnIdSupported<T, R>
        [[nodiscard]] auto query(const T& obj [[clang::lifetimebound]]) {
```

Replace (the `ReturnId`-templated bulk overload — the non-templated one above it returns the void `BulkQuery` and needs no constraint):

```cpp
        template <ReturnId R>
        [[nodiscard]] auto
        query(std::span<const T> objects [[clang::lifetimebound]], std::optional<InsertOptions> opts = std::nullopt) {
```

with:

```cpp
        template <ReturnId R>
            requires ReturnIdSupported<T, R>
        [[nodiscard]] auto
        query(std::span<const T> objects [[clang::lifetimebound]], std::optional<InsertOptions> opts = std::nullopt) {
```

- [ ] **Step 3: Constrain the `QuerySet::insert()` overloads in queryset.cppm**

Constraining BOTH layers is deliberate: `QuerySet::insert` is the public entry point — an unconstrained wrapper would report the failure from inside `query()` instead of the user's call site. Replace:

```cpp
        // Insert single object - returns proxy with .execute() and .to_sql()
        // ReturnId::Yes (default): .execute() returns std::expected<int64_t, Error>
        // ReturnId::No: .execute() returns std::expected<void, Error> (faster, no RETURNING clause)
        // (SFINAE: only accept T, not span/container)
        template <orm::statements::ReturnId R = orm::statements::ReturnId::Yes, typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T>
        [[nodiscard]] auto insert(const U& obj [[clang::lifetimebound]]) {
```

with:

```cpp
        // Insert single object - returns proxy with .execute() and .to_sql()
        // ReturnId::Yes (single-PK default): .execute() returns std::expected<int64_t, Error>
        // ReturnId::No: .execute() returns std::expected<void, Error> (faster, no RETURNING clause)
        // Composite-PK models (#502) default to ReturnId::No — a composite key is never
        // DB-generated, so there is nothing to return — and reject an explicit
        // ReturnId::Yes at compile time (ReturnIdSupported).
        // (SFINAE: only accept T, not span/container)
        template <orm::statements::ReturnId R = orm::statements::default_return_id<T>(), typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T> && orm::statements::ReturnIdSupported<T, R>
        [[nodiscard]] auto insert(const U& obj [[clang::lifetimebound]]) {
```

Replace (the templated bulk overload — the non-templated one stays untouched):

```cpp
        template <orm::statements::ReturnId R>
        [[nodiscard]] auto
        insert(std::span<const T>                            objects [[clang::lifetimebound]],
               std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
```

with:

```cpp
        template <orm::statements::ReturnId R>
            requires orm::statements::ReturnIdSupported<T, R>
        [[nodiscard]] auto
        insert(std::span<const T>                            objects [[clang::lifetimebound]],
               std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
```

- [ ] **Step 4: Build and run — Task 3's asserts now pass, nothing regresses**

```bash
cmake --build --preset ninja-debug
ctest --preset ninja-debug --output-on-failure
```

Expected: clean build (the `static_assert`s compile true), all tests pass. `tests/crud/test_insert_returning.cpp` (explicit `ReturnId::Yes` on single-PK models) and `test_insert_no_return.cpp` (explicit `No`) are the live proof the constraint does not over-reject.

---

### Task 5: Failing live INSERT tests (both backends)

**Files:**
- Modify: `tests/crud/test_composite_pk_crud.cpp`

**Interfaces:**
- Consumes: `CompositePkFixture<Model, ConnType>` (already in the file: creates the model's table from generated DDL; `row_count()`, `find_one(filter)`, `seed(sql)` helpers), `storm::test::ensure_tables`, `DatabaseTypes`, `storm::orm::statements::ReturnId`.
- Produces: the runtime behavior contract Task 6 must satisfy.

- [ ] **Step 1: Update the stale header comment**

Replace:

```cpp
// INSERT by composite key is #502, so every fixture seeds through raw SQL — the
// same approach #500's execution test used.
```

with:

```cpp
// The UPDATE/DELETE fixtures seed through raw SQL (as when they were written,
// pre-#502) so those tests stay independent of the INSERT path they don't
// test. The INSERT suites below (#502) use qs.insert() itself.
```

- [ ] **Step 2: Append the INSERT test section**

Add at the end of the file, BEFORE the closing `// NOLINTEND(readability-implicit-bool-conversion)`:

```cpp
// ── (#502) INSERT by composite key ───────────────────────────────────────────
// Every key part is caller data: the INSERT carries all columns in declaration
// order, .execute() returns std::expected<void, Error> (a composite key is
// never DB-generated, so there is nothing to RETURN), and a duplicate full key
// surfaces the PRIMARY KEY violation as an Error.

namespace {
    // Insert tests need an EMPTY table, so they use the base fixture directly.
    template <typename ConnType> class OrderLineInsertTest : public CompositePkFixture<OrderLine, ConnType> {};
    template <typename ConnType> class InventoryInsertTest : public CompositePkFixture<Inventory, ConnType> {};
    template <typename ConnType> class LedgerInsertTest : public CompositePkFixture<Ledger, ConnType> {};
} // namespace

TYPED_TEST_SUITE(OrderLineInsertTest, DatabaseTypes);
TYPED_TEST_SUITE(InventoryInsertTest, DatabaseTypes);
TYPED_TEST_SUITE(LedgerInsertTest, DatabaseTypes);

TYPED_TEST(OrderLineInsertTest, SingleInsertLandsEveryKeyPart) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       row{.order_id = 7, .product_id = 42, .quantity = 3, .note = "a"};
    auto                                  result = qs.insert(row).execute();
    static_assert(
            std::is_same_v<decltype(result), std::expected<void, typename TypeParam::Error>>,
            "composite insert has nothing to return — the caller supplied the whole key"
    );
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(this->row_count(), 1);
    using storm::orm::where::f;
    auto found = this->find_one(f<^^OrderLine::order_id>() == 7 && f<^^OrderLine::product_id>() == 42);
    ASSERT_TRUE(found.has_value()) << "the full key landed";
    EXPECT_EQ(found->quantity, 3);
    EXPECT_EQ(found->note, "a");
}

// The core failure this issue prevents: pre-#502 the column list dropped the
// FIRST key part while the bind loop skipped it too, so every later value
// shifted one column left — order_id would have received product_id's value.
TYPED_TEST(OrderLineInsertTest, FirstKeyPartIsNotDefaultedOrShifted) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       row{.order_id = 5, .product_id = 6, .quantity = 9, .note = "x"};
    ASSERT_TRUE(qs.insert(row).execute().has_value());

    using storm::orm::where::f;
    EXPECT_FALSE(this->find_one(f<^^OrderLine::order_id>() == 6).has_value())
            << "product_id's value must not land in order_id";
    auto found = this->find_one(f<^^OrderLine::order_id>() == 5 && f<^^OrderLine::product_id>() == 6);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->quantity, 9) << "every non-key value landed in its own column";
}

// Redundant but accepted (generic code may spell it out): identical to plain insert().
TYPED_TEST(OrderLineInsertTest, ExplicitReturnIdNoIsAcceptedAndIdentical) {
    using storm::orm::statements::ReturnId;
    storm::QuerySet<OrderLine, TypeParam> qs;
    const OrderLine                       row{.order_id = 1, .product_id = 2, .quantity = 1, .note = "n"};
    auto                                  result = qs.template insert<ReturnId::No>(row).execute();
    static_assert(std::is_same_v<decltype(result), std::expected<void, typename TypeParam::Error>>);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(this->row_count(), 1);
}

TYPED_TEST(OrderLineInsertTest, DuplicateFullKeyIsAnError) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.insert(OrderLine{.order_id = 1, .product_id = 10, .quantity = 5, .note = "a"})
                        .execute()
                        .has_value());

    auto result = qs.insert(OrderLine{.order_id = 1, .product_id = 10, .quantity = 99, .note = "b"}).execute();
    EXPECT_FALSE(result.has_value()) << "a duplicate composite key violates the PRIMARY KEY constraint";
    EXPECT_EQ(this->row_count(), 1) << "the duplicate did not land";
}

// Sharing one part is NOT a duplicate — only the full key is unique.
TYPED_TEST(OrderLineInsertTest, SharedSinglePartIsNotADuplicate) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.insert(OrderLine{.order_id = 1, .product_id = 10, .quantity = 1, .note = "a"})
                        .execute()
                        .has_value());
    EXPECT_TRUE(qs.insert(OrderLine{.order_id = 1, .product_id = 20, .quantity = 2, .note = "b"})
                        .execute()
                        .has_value());
    EXPECT_TRUE(qs.insert(OrderLine{.order_id = 2, .product_id = 10, .quantity = 3, .note = "c"})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->row_count(), 3);
}

TYPED_TEST(OrderLineInsertTest, BatchInsertLandsEveryRow) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          rows{
                     {.order_id = 1, .product_id = 10, .quantity = 5, .note = "a"},
                     {.order_id = 1, .product_id = 20, .quantity = 7, .note = "b"},
                     {.order_id = 2, .product_id = 10, .quantity = 9, .note = "c"},
    };
    ASSERT_TRUE(qs.insert(std::span<const OrderLine>(rows)).execute().has_value());
    EXPECT_EQ(this->row_count(), 3);

    using storm::orm::where::f;
    auto row = this->find_one(f<^^OrderLine::order_id>() == 2 && f<^^OrderLine::product_id>() == 10);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->quantity, 9);
}

TYPED_TEST(OrderLineInsertTest, EmptyBatchIsANoOp) {
    storm::QuerySet<OrderLine, TypeParam> qs;
    const std::vector<OrderLine>          empty;
    EXPECT_TRUE(qs.insert(std::span<const OrderLine>(empty)).execute().has_value());
    EXPECT_EQ(this->row_count(), 0);
}

// The 999-parameter ceiling divides by ALL fields now that the key columns are
// in the statement: OrderLine binds 4 params/row, so one bulk statement caps at
// 999/4 == 249 rows and 250 forces the chunked path (one max chunk + remainder).
TYPED_TEST(OrderLineInsertTest, BatchAtTheChunkBoundary) {
    constexpr int          ROWS = 250;
    std::vector<OrderLine> rows;
    rows.reserve(ROWS);
    for (int i = 0; i < ROWS; ++i) {
        rows.push_back({.order_id = i, .product_id = i * 2, .quantity = i, .note = "n"});
    }
    storm::QuerySet<OrderLine, TypeParam> qs;
    ASSERT_TRUE(qs.insert(std::span<const OrderLine>(rows)).execute().has_value());
    EXPECT_EQ(this->row_count(), ROWS);

    using storm::orm::where::f;
    auto last = this->find_one(
            f<^^OrderLine::order_id>() == ROWS - 1 && f<^^OrderLine::product_id>() == (ROWS - 1) * 2
    );
    ASSERT_TRUE(last.has_value()) << "the row past the chunk boundary landed with its full key";
    EXPECT_EQ(last->quantity, ROWS - 1);
}

// ── Mixed-type key parts (int + std::string) ─────────────────────────────────

TYPED_TEST(InventoryInsertTest, TextKeyPartBindsPerType) {
    storm::QuerySet<Inventory, TypeParam> qs;
    ASSERT_TRUE(qs.insert(Inventory{.warehouse = 1, .sku = "apple", .on_hand = 5}).execute().has_value());
    ASSERT_TRUE(qs.insert(Inventory{.warehouse = 1, .sku = "pear", .on_hand = 7}).execute().has_value());

    using storm::orm::where::f;
    auto row = this->find_one(f<^^Inventory::warehouse>() == 1 && f<^^Inventory::sku>() == std::string("apple"));
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->on_hand, 5);
}

TYPED_TEST(InventoryInsertTest, DuplicateTextKeyIsAnError) {
    storm::QuerySet<Inventory, TypeParam> qs;
    ASSERT_TRUE(qs.insert(Inventory{.warehouse = 1, .sku = "apple", .on_hand = 5}).execute().has_value());
    EXPECT_FALSE(qs.insert(Inventory{.warehouse = 1, .sku = "apple", .on_hand = 9}).execute().has_value());
    EXPECT_EQ(this->row_count(), 1);
}

// ── Three-part key ───────────────────────────────────────────────────────────

TYPED_TEST(LedgerInsertTest, ThreePartKeyInsert) {
    storm::QuerySet<Ledger, TypeParam> qs;
    ASSERT_TRUE(qs.insert(Ledger{.region = 1, .account = "cash", .period = 202601, .balance = 10.5})
                        .execute()
                        .has_value());
    // Differs in exactly ONE part each — none is a duplicate of the first row.
    EXPECT_TRUE(qs.insert(Ledger{.region = 2, .account = "cash", .period = 202601, .balance = 1.0})
                        .execute()
                        .has_value());
    EXPECT_TRUE(qs.insert(Ledger{.region = 1, .account = "debt", .period = 202601, .balance = 2.0})
                        .execute()
                        .has_value());
    EXPECT_TRUE(qs.insert(Ledger{.region = 1, .account = "cash", .period = 202602, .balance = 3.0})
                        .execute()
                        .has_value());
    EXPECT_EQ(this->row_count(), 4);
}

// ── FK key parts: the association-table shape ────────────────────────────────
// An FK part binds the REFERENCED row's key into the "<name>_id" column.

namespace {
    template <typename ConnType> class StockEntryInsertTest : public CompositePkFixture<StockEntry, ConnType> {
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
        }

        // Same full-scan matcher as StockEntryTest above, for the same reason:
        // filtering on an FK MEMBER is an unrelated WHERE-layer gap.
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

TYPED_TEST_SUITE(StockEntryInsertTest, DatabaseTypes);

TYPED_TEST(StockEntryInsertTest, FkKeyPartBindsTheReferencedKey) {
    storm::QuerySet<StockEntry, TypeParam> qs;
    ASSERT_TRUE(qs.insert(StockEntry{.warehouse = {.id = 2}, .sku = 10, .qty = 5}).execute().has_value());

    EXPECT_EQ(this->qty_of(2, 10), 5) << "warehouse_id bound the referenced key, not a defaulted value";
    EXPECT_EQ(this->qty_of(1, 10), -1) << "no row for the other warehouse";
}

TYPED_TEST(StockEntryInsertTest, DuplicateFkKeyIsAnError) {
    storm::QuerySet<StockEntry, TypeParam> qs;
    ASSERT_TRUE(qs.insert(StockEntry{.warehouse = {.id = 1}, .sku = 10, .qty = 5}).execute().has_value());
    EXPECT_FALSE(qs.insert(StockEntry{.warehouse = {.id = 1}, .sku = 10, .qty = 9}).execute().has_value());
    EXPECT_EQ(this->row_count(), 1);
}
```

- [ ] **Step 3: Build and run — the new tests MUST fail**

```bash
cmake --build --preset ninja-debug
./build/debug/tests/storm_tests --gtest_filter="*InsertTest*"
ctest --preset ninja-debug-sqlite --output-on-failure -R storm
```

Expected: the new composite `*InsertTest*` tests FAIL. After Task 2 the SQL has all 4 placeholders, but the bind loop still skips the first PK part (`skips_pk_column` plain branch), so every value shifts one column left on SQLite (or the backend reports a parameter-count/NULL-key error). Either failure mode is the expected pre-implementation evidence. Single-PK suites still pass.

---

### Task 6: Bind fix — `skips_pk_column` composite branch

**Files:**
- Modify: `src/orm/statements/base.cppm:900-909` (`skips_pk_column`)
- Modify: `src/orm/statements/base.cppm:856-861` (stale comment)
- Modify: `src/orm/statements/base.cppm:918-923` (stale comment)

**Interfaces:**
- Consumes: `has_composite_pk_`, `is_pk_member`, `primary_key_` (all already on `BaseStatement`).
- Produces: `bind_non_pk_fields_impl` / `bind_non_pk_objects_bulk_impl` (both pass plain `SkipPK=true`) now bind EVERY field of a composite model in declaration order — matching the column list Task 2 produces. Single-PK bind order unchanged.

- [ ] **Step 1: Widen `skips_pk_column`**

Replace:

```cpp
        // Does the caller's PK-skip policy exclude `member` from the bind order?
        // SkipAllPK covers every part of a composite key (#501); plain SkipPK keeps the
        // historical first-PK-only skip that INSERT relies on. The two coincide on a
        // single-PK model.
        template <bool SkipPK, bool SkipAllPK> static consteval auto skips_pk_column(std::meta::info member) -> bool {
            if (!SkipPK) {
                return false;
            }
            return SkipAllPK ? is_pk_member(member) : member == primary_key_;
        }
```

with:

```cpp
        // Does the caller's PK-skip policy exclude `member` from the bind order?
        // SkipAllPK covers every part of a composite key (#501) — the UPDATE policy,
        // whose SET clause omits the whole key. Plain SkipPK is the INSERT policy: it
        // skips the DB-generated key, which only a single-column PK can be — a
        // composite key has no auto-generation mechanism, so every part is caller
        // data and nothing is skipped (#502). The two policies coincide on a
        // single-PK model.
        template <bool SkipPK, bool SkipAllPK> static consteval auto skips_pk_column(std::meta::info member) -> bool {
            if (!SkipPK) {
                return false;
            }
            if (SkipAllPK) {
                return is_pk_member(member);
            }
            return !has_composite_pk_ && member == primary_key_;
        }
```

- [ ] **Step 2: Update the stale #500 comment (base.cppm:856-861)**

Replace:

```cpp
        // Composite primary key (#500). primary_key_members_ is the full PK list in
        // declaration order; primary_key_ / pk_name_ above stay the FIRST element, so
        // every single-PK caller is untouched. has_composite_pk_ is the branch the
        // schema generator takes to emit a table-level PRIMARY KEY (a, b) instead of a
        // column-level one. CRUD paths do not consult these yet — a composite model
        // reaching INSERT/UPDATE/DELETE/JOIN still fails at a named concept (#501-#504).
```

with:

```cpp
        // Composite primary key (#500). primary_key_members_ is the full PK list in
        // declaration order; primary_key_ / pk_name_ above stay the FIRST element, so
        // every single-PK caller is untouched. has_composite_pk_ branches the schema
        // generator (table-level PRIMARY KEY (a, b), #500), the by-key WHERE clause
        // (#501), and the INSERT column/bind policy (#502). JOIN on a composite
        // model is still out of scope (#504).
```

- [ ] **Step 3: Update the stale SkipAllPK comment (base.cppm:918-923)**

Replace:

```cpp
        // SkipAllPK widens the skip from primary_key_ to EVERY primary-key member (#501),
        // which is what UPDATE needs: its SET clause omits the whole composite key, so the
        // bind order must too. Deliberately a SEPARATE flag rather than widening SkipPK
        // itself — INSERT also passes SkipPK, and how a composite key is INSERTed is #502.
        // Changing SkipPK here would silently alter the INSERT bind order as a side effect
        // of a DELETE/UPDATE issue. No effect on single-PK models, where the two coincide.
```

with:

```cpp
        // SkipAllPK widens the skip from primary_key_ to EVERY primary-key member (#501),
        // which is what UPDATE needs: its SET clause omits the whole composite key, so the
        // bind order must too. INSERT keeps plain SkipPK, which since #502 skips NOTHING
        // on a composite model (see skips_pk_column: every key part is caller data).
        // No effect on single-PK models, where the two flags coincide.
```

- [ ] **Step 4: Build and run — everything passes**

```bash
cmake --build --preset ninja-debug
ctest --preset ninja-debug --output-on-failure
```

Expected: ALL tests pass, including every Task 1/3/5 addition and the whole pre-existing suite (single-PK bind order untouched: `skips_pk_column` returns identical values when `has_composite_pk_` is false).

---

### Task 7: Full verification sweep

**Files:** none (verification only)

- [ ] **Step 1: Release build + full test suite on both presets**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
ctest --preset ninja-debug --output-on-failure
```

Expected: release build clean (tidy's modmap needs it anyway for the commit hook), full ctest green on SQLite + PostgreSQL (PG skips gracefully if not running — if it IS running, the TYPED_TESTs must pass there too).

- [ ] **Step 2: Focused regression filters**

```bash
./build/debug/tests/storm_tests --gtest_filter="CompositePk*:*InsertTest*:InsertReturning*:InsertNoReturn*"
```

Expected: all pass — composite additions plus the single-PK `ReturnId::Yes`/`No` suites proving the constraint does not over-reject.

---

### Task 8: Benchmark (Release, A/B, single-PK INSERT)

**Files:** none

- [ ] **Step 1: Invoke the benchmark skill**

Use the `benchmark` skill (`/benchmark`) with focus on the INSERT category (`--benchmark_filter='.*INSERT.*'`), comparing this branch against `develop` with alternating A/B interleaved runs (sub-2% deltas need interleaving — established practice).

Expected: single-PK INSERT `% of raw` within noise (check cv; uniform-sign deltas across the whole sweep are NOT noise even below cv). The hot path is compile-time-only changed for single-PK models (`skips_pk_column` and `for_each_field_name` are consteval; the emitted SQL and bind sequence are identical), so any real regression means an implementation mistake — investigate, do not commit.

---

### Task 9: Documentation

**Files:**
- Modify: `CLAUDE.md` (composite-PK feature paragraph + QuerySet API section)
- Modify: `docs/guide/reference/FIELD_TYPES.md` (composite-PK section)

- [ ] **Step 1: Add the #502 paragraph to CLAUDE.md**

Insert directly AFTER the existing "**Composite PK — UPDATE and DELETE (#501)**" paragraph:

```markdown
**Composite PK — INSERT (#502)**: a composite key is never DB-generated — `AUTOINCREMENT` (SQLite)
and `GENERATED ... AS IDENTITY` (PG) are single-integer-column features, so every key part is
caller data. That inverts the single-PK rule twice. (1) **The key columns join the INSERT**: the
`SkipPrimaryKey` skip in `FieldNameGrammar::for_each_field_name` and the plain-`SkipPK` branch of
`skips_pk_column` are both gated on `!has_composite_pk_`, so a composite model emits and binds ALL
fields in declaration order (one shared consteval iterator feeds the column list, its sizer, and
the placeholders — they cannot drift). (2) **There is nothing to RETURN**: `insert().execute()` on
a composite model returns `std::expected<void, Error>` with no `RETURNING` emitted, riding the
pre-existing `ReturnId::No` fast path. The plain call resolves there via `default_return_id<T>()`
(consteval: `No` for composite, `Yes` for single-PK), so the call site is IDENTICAL across model
shapes; an explicit `insert<ReturnId::Yes>` on a composite model is a **compile-time error** via
the `ReturnIdSupported<T, R>` concept (unconstrained it would emit `RETURNING <first part>` —
silently lossy), constraining BOTH `QuerySet::insert` and `InsertStatement::query` so the
diagnostic fires at the user's call site. `ReturnId::No` stays valid on every model shape (generic
code that spells it out keeps compiling). The #413 auto-`DEFAULT` caveat was checked and does not
fire: that `DEFAULT` is recovered from a C++ default-member-initializer — a compile-time constant
already in the caller's object, not a DB-generated value. `placeholders_count()` widened to
`field_count_` for composite models (feeds the upsert `auto_update` tail offset — #503's path);
chunking needed NO change (the auto batch size already divides by ALL fields, which becomes exactly
right once the key columns are bound). Single-PK INSERT SQL is byte-identical
(regression-asserted) and the `RETURNING id` path is unchanged. Upsert/`ON CONFLICT` is #503,
JOIN #504.
```

- [ ] **Step 2: Record the return type in CLAUDE.md's QuerySet API section**

In the big QuerySet API code block, after the upsert lines, add:

```cpp
// Composite-PK INSERT (#502) — every key part is caller data (a composite key is
// never DB-generated), so there is nothing to return: no RETURNING is emitted.
qs.insert(order_line).execute();               // → std::expected<void, Error>
// insert<ReturnId::Yes> on a composite model is a compile-time error (ReturnIdSupported).
```

- [ ] **Step 3: Update FIELD_TYPES.md**

Read `docs/guide/reference/FIELD_TYPES.md`, find the composite-primary-key section (added by #500, extended by #501), and append an INSERT subsection stating: every key part must be supplied by the caller and appears in the INSERT column list in declaration order; `insert().execute()` returns `std::expected<void, Error>` and emits no `RETURNING`; `insert<ReturnId::Yes>` is a compile-time error; `insert<ReturnId::No>` is accepted and identical to the plain call; a duplicate full key surfaces as `Error`. Include the OrderItem-style example from the issue:

```cpp
OrderItem oi{.order_id = 7, .product_id = 42, .quantity = 3};  // caller supplies both PK parts
qs.insert(oi).execute();   // → std::expected<void, Error>
// SQL: INSERT INTO order_item (order_id, product_id, quantity) VALUES (?, ?, ?)
```

Match the surrounding section's formatting and tone (mirror how the #501 UPDATE/DELETE subsection is written).

---

### Task 10: Review, commit, issue bookkeeping, PR

**Files:** none new (commits everything)

- [ ] **Step 1: Pre-clear the hook**

```bash
cmake --build --preset ninja-debug && cmake --build --preset ninja-release
ctest --preset ninja-debug --output-on-failure
```

(Both presets built + tests green BEFORE committing — a blocked hook costs ~9 min per retry.)

- [ ] **Step 2: Show files, get user approval**

```bash
git status --short
```

Present the list to the user and get approval before committing (CLAUDE.md rule 5).

- [ ] **Step 3: Stage and run the reviewer agent**

```bash
git add -A
git diff --cached --stat
```

Dispatch the `storm-code-reviewer` agent on the staged diff (rule 13). Address any findings, re-stage, re-review if code changed.

- [ ] **Step 4: Commit (single commit — code + tests + docs together)**

```bash
git commit -m "feat(502): composite PK — INSERT column list, binding, and void return

A composite key is never DB-generated (AUTOINCREMENT / IDENTITY are
single-integer-column features), so every part is caller data: the INSERT
column list, placeholders, and bind loop now include all key parts in
declaration order (for_each_field_name and skips_pk_column gate their skip
on !has_composite_pk_; one shared consteval iterator keeps list/sizer/
placeholders in lockstep). Nothing to RETURN: plain insert() resolves to
std::expected<void, Error> via default_return_id<T>(), and an explicit
ReturnId::Yes is a compile-time error via ReturnIdSupported<T, R>
(unconstrained it would emit 'RETURNING <first part>' — silently lossy).
ReturnId::No stays valid on every model shape. placeholders_count()
widened for the upsert auto_update tail (#503's path); chunking already
divides by all fields, so the 999-param ceiling holds unchanged.
Single-PK INSERT SQL byte-identical (regression-asserted), RETURNING id
path untouched, benchmarks within noise.

Closes #502

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

The pre-commit hook runs format → tidy → tests → coverage; the self-heal path (#489) covers fresh-worktree gaps.

- [ ] **Step 5: Check off the issue's Definition of done**

```bash
gh issue view 502 --json body -q .body
```

Edit the body with `gh issue edit 502 --body "..."`, flipping to `- [x]` every item genuinely delivered: A/B decision recorded (done — issue comment), key columns in column/placeholder/bind lists in declaration order, `SkipPK` logic correct, bulk prefix + cache + chunking, return type per decision B documented in CLAUDE.md, single-PK byte-identical regression assert, all listed tests, cross-backend TYPED_TEST, tests written before implementation and failing first.

- [ ] **Step 6: Rebase on develop, push, PR**

```bash
git fetch origin develop && git rebase origin/develop
git push -u origin feature/502-composite-pk-insert
gh pr create --base develop --title "feat(502): composite PK — INSERT column list, binding, and void return" --body "$(cat <<'EOF'
Step 3 of 5 for composite primary keys (#90), on top of #500 (annotation + DDL) and #501 (UPDATE/DELETE).

A composite key is never DB-generated, so every part is caller data. Two consequences, both delivered:

1. **Key columns join the INSERT** — column list, placeholders, and bind loop include all parts in declaration order; single-PK output byte-identical (regression-asserted).
2. **Nothing to RETURN** — decision **B** (recorded on the issue): plain `insert().execute()` on a composite model returns `std::expected<void, Error>`, no `RETURNING` emitted; explicit `ReturnId::Yes` is a compile-time error (`ReturnIdSupported<T, R>`); `ReturnId::No` accepted on every model shape.

Out of scope: upsert/`ON CONFLICT` (#503), JOIN (#504).

Closes #502

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 7: Gate checks and merge**

Wait ~30 s, then run the `/sonarcloud-status` skill (PR mode — it waits for analysis; verify `head_sha` matches HEAD). Zero new issues required. Then:

```bash
gh pr checks --watch
gh pr merge --squash --auto
```

All four CI jobs (ninja-debug, ninja-release, ninja-asan-ubsan, ninja-tsan) must pass. If Sonar or CI reports anything, fix on the branch, push, re-check until clean.

- [ ] **Step 8: Post-merge bookkeeping**

After the merge completes:

```bash
gh issue close 502
cd /home/ihor/projects/storm/storm_develop && git checkout develop && git pull
git worktree remove ../worktrees/feature-502-composite-pk-insert
```

---

## Self-Review Notes

- **Spec coverage:** column list/placeholders/bind (Tasks 1-2, 5-6), `SkipPK` at base.cppm (Task 6), bulk prefix + cache (falls out of Task 2 — same `build_non_pk_field_names_list`; exercised by Task 5's batch tests), chunking asserted not modified (Task 5 boundary test), return type B + `ReturnId` semantics (Tasks 3-4), CLAUDE.md documentation (Task 9), byte-identical regression (Task 1), all DoD test categories (Tasks 1, 3, 5), cross-backend TYPED_TEST (Task 5), tests-first-and-failing (Tasks 1, 3, 5 each end with a mandatory failure check).
- **Known-safe non-changes:** `calculate_insert_sql_size` and `calculate_bulk_insert_prefix_size` already use the NO-skip sizer (`calculate_field_names_size()`), so buffers were already sized for all fields — no sizer edits needed. The `RETURNING`-variant constants for composite models compile to a harmless unused string; the gate guarantees no execution path reaches them.
- **Type consistency:** `ReturnIdSupported<T, R>` and `default_return_id<T>()` spelled identically in Tasks 3, 4, 5, 9; `skips_pk_column<SkipPK, SkipAllPK>` signature unchanged from #501.
