# Composite FK + JOIN Support (#504) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a foreign key (`[[= storm::fk<>]]`) reference a model with a composite (multi-column) primary key, and make every JOIN path — regular FK `join<>()`/`left_join<>()`, many-to-many, and reverse-FK — work correctly when the target's key spans more than one column.

**Architecture:** `fk<>` itself is unchanged — it already marks a member whose *type* is the related model, independent of that model's key shape. The gap is everywhere the code assumed "the FK target has exactly one PK column": (1) `find_fk_primary_key` returns a single `std::meta::info`, widened to `find_fk_primary_key_members` returning the target's `primary_key_members_`-shaped array; (2) the canonical column-name writer (`append_column_name`, #422) emits one `<name>_id` per FK member, widened to emit N columns `<name>_<part>_id` for a composite target while staying byte-identical for a single-column target; (3) every JOIN `ON`/WHERE-IN clause hardcodes one column, widened to an AND-joined (JOIN) or row-value (`IN`) multi-column predicate; (4) the m2m/reverse-FK stitch map is keyed on `std::int64_t`, widened to a fixed-size inline byte-buffer type (`StitchKey`) that degenerates to today's 8-byte layout for single-column keys. Schema DDL for auto-junction tables widens from 2 columns to N+M.

**Tech Stack:** C++26 modules + `std::meta` reflection (clang-p2996), consteval SQL generation via `ConstexprString`, SQLite + PostgreSQL backends, GoogleTest (`TYPED_TEST` cross-backend), Google Benchmark for the mandatory A/B.

## Global Constraints

- Single-PK JOIN SQL must stay **byte-identical** to pre-#504 output (regression-asserted in tests, per #500/#501/#502 precedent).
- No measurable slowdown on existing single-PK JOIN/m2m/reverse-FK benchmarks — Release build, interleaved A/B per the `benchmark` skill, before this ships.
- Every `ConstexprString` sizer must be **exact** — it truncates silently on overflow (CLAUDE.md rule, restated in #500/#501/#502 history).
- Tests are written **before** implementation for every new behavior (TDD, CLAUDE.md rule 9): write failing test → run (must fail) → implement → run (must pass).
- Cross-backend: new behavioral tests use `TYPED_TEST` over `DatabaseTypes` (SQLite + PostgreSQL).
- `storm-code-reviewer` agent runs on the staged diff before every commit (CLAUDE.md rule 13).
- No local sanitizer runs — CI runs `ninja-asan-ubsan`/`ninja-tsan` (CLAUDE.md rule 7); check `gh pr checks` after push.
- Column-name generation routes through the canonical writer (`append_column_name`/`column_name_size`, #422) — never open-code a "_id" suffix.
- `std::unreachable()` backstops stay wherever the existing code already places them (per #500's whole-SQL backstop pattern) — add one at each new consteval sizer that could theoretically fall through.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/orm/field_attr.cppm` | (Modify) Composite-aware column-name writer: `append_fk_column_names`/`fk_column_names_size` — the N-column FK sibling of the existing single-column `append_column_name`. |
| `src/orm/statements/base.cppm` | (Modify) `find_fk_primary_key_members` (widened FK→target-PK resolution), `ValidForeignKey`/`FKFieldOf` composite-arity gate, composite FK bind/extract (INSERT, SELECT, UPDATE/DELETE-by-key). |
| `src/orm/statements/join.cppm` | (Modify) `JoinStatement` (regular FK JOIN): multi-column `ON` clause, multi-column extraction. `TwoQueryJoinBase`/`M2MRelation`: `StitchKey`-based owner-key extraction. `M2MJoinStatement`/`ReverseFKJoinStatement`: multi-column junction/owner join predicates. |
| `src/orm/statements/select.cppm` | (Modify) `by_pk` map re-keyed from `std::unordered_map<std::int64_t, T*>` to `std::unordered_map<StitchKey, T*>`. |
| `src/orm/utilities.cppm` | (Modify — or new leaf module if size forces a split) `StitchKey` type: fixed-size inline byte buffer + `operator==` + `std::hash` specialization. |
| `src/orm/schema.cppm` | (Modify) `build_junction_sql`: N+M column junction DDL when either side has a composite PK. |
| `tests/crud/test_composite_pk_models.h` | (Modify) Add FK-target-side models: a child referencing `OrderLine`'s composite PK (regular FK JOIN), an m2m pair with a composite-PK side, a reverse-FK pair with a composite-PK owner. |
| `tests/query/test_composite_fk_join.cpp` | (New) All new behavioral tests: inner/left JOIN across composite FK, m2m with composite-PK owner, reverse-FK with composite-PK owner, fan-out ≥ 10, composite PK with a string part, empty relation, cross-backend. |
| `tests/schema/test_composite_fk_join_sql.cpp` | (New) Compile-time SQL text assertions (JOIN `ON` clause shape, junction DDL shape) + single-PK byte-identical regression assertions. |
| `benchmarks/` (existing JOIN bench file) | (Modify) Add composite-FK JOIN benchmark case; re-run existing single-PK JOIN benchmarks for the A/B. |

---

## Task 1: StitchKey type — fixed-size inline byte-buffer key

**Files:**
- Modify: `src/orm/utilities.cppm`
- Test: `tests/schema/test_stitch_key.cpp` (new)

**Interfaces:**
- Produces: `storm::orm::utilities::StitchKey` — a regular type with:
  - `static constexpr std::size_t CAPACITY = 32;` (bytes — covers a 3-part key of `(int32, int64, int32)` = 16 bytes, or a 2-part `(int64, 8-char-inline SSO string hash)`; documented rationale below)
  - `void append_int64(std::int64_t v) noexcept` — appends 8 bytes, little/native-endian, advances an internal length counter
  - `void append_string(std::string_view v) noexcept` — appends `std::hash<std::string_view>{}(v)` as 8 bytes (a long string part is HASHED, not stored verbatim — collisions are astronomically unlikely for the stitch map's purpose of "does this Q2 row belong to this Q1 owner", and a false-positive collision only mis-stitches one relation row, never crashes)
  - `bool operator==(const StitchKey&) const noexcept = default;`
  - `friend struct std::hash<StitchKey>` — hashes the occupied bytes (FNV-1a or `std::hash<std::string_view>` over the byte range)
  - Construction is always via a builder pattern: `StitchKey key; key.append_int64(5); key.append_int64(12);` — the caller (erased extractor function) knows the part count/types at compile time and calls the right sequence.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/schema/test_stitch_key.cpp
import storm;
#include <gtest/gtest.h>

TEST(StitchKeyTest, SingleInt64PartEqualsItself) {
    storm::orm::utilities::StitchKey a;
    a.append_int64(42);
    storm::orm::utilities::StitchKey b;
    b.append_int64(42);
    EXPECT_EQ(a, b);
}

TEST(StitchKeyTest, DifferentInt64PartsAreNotEqual) {
    storm::orm::utilities::StitchKey a;
    a.append_int64(42);
    storm::orm::utilities::StitchKey b;
    b.append_int64(43);
    EXPECT_NE(a, b);
}

TEST(StitchKeyTest, TwoPartOrderMatters) {
    storm::orm::utilities::StitchKey a;
    a.append_int64(1);
    a.append_int64(2);
    storm::orm::utilities::StitchKey b;
    b.append_int64(2);
    b.append_int64(1);
    EXPECT_NE(a, b); // (1,2) is a different key from (2,1)
}

TEST(StitchKeyTest, ThreePartCompositeKeyRoundTrips) {
    storm::orm::utilities::StitchKey a;
    a.append_int64(7);
    a.append_string("sku-123");
    a.append_int64(2026);
    storm::orm::utilities::StitchKey b;
    b.append_int64(7);
    b.append_string("sku-123");
    b.append_int64(2026);
    EXPECT_EQ(a, b);
}

TEST(StitchKeyTest, HashableInUnorderedMap) {
    std::unordered_map<storm::orm::utilities::StitchKey, int> map;
    storm::orm::utilities::StitchKey key;
    key.append_int64(99);
    map[key] = 7;
    storm::orm::utilities::StitchKey lookup;
    lookup.append_int64(99);
    ASSERT_TRUE(map.contains(lookup));
    EXPECT_EQ(map.at(lookup), 7);
}

TEST(StitchKeyTest, StringPartDifferentValuesHashDifferently) {
    storm::orm::utilities::StitchKey a;
    a.append_int64(1);
    a.append_string("alice-warehouse");
    storm::orm::utilities::StitchKey b;
    b.append_int64(1);
    b.append_string("bob-warehouse");
    EXPECT_NE(a, b);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -30`
Expected: FAIL — `StitchKey` is not a member of `storm::orm::utilities` (compile error, file doesn't exist yet).

- [ ] **Step 3: Write minimal implementation**

Add to `src/orm/utilities.cppm` (in the `export namespace storm::orm::utilities` block):

```cpp
// Stitch key (#504): the m2m/reverse-FK two-query eager-load stitches Q2 rows to
// their Q1 owner through a hash map keyed on the owner's primary key. A single-column
// PK key was a bare std::int64_t; a composite PK needs a multi-value key that still
// crosses the M2MRelation type-erased vtable (ErasedStatementPtr — no T in scope
// there), so it can't be a template parameter. StitchKey is a fixed-size inline byte
// buffer built by appending one part at a time: append_int64 for integral PK parts
// (8 bytes, matching today's exact single-PK layout so that case is byte-identical),
// append_string for text parts (hashed to 8 bytes — the stitch only needs equality
// for map lookup, never the original text back, so storing a hash instead of the
// bytes avoids capping composite key width by string length). CAPACITY=32 covers
// every composite PK in the codebase today (max 3 parts before #504 ships) with
// room to spare; a 4th+ part or two string parts still fits (4 x 8 = 32).
class StitchKey {
      public:
        static constexpr std::size_t CAPACITY = 32;

        __attribute__((always_inline)) void append_int64(std::int64_t v) noexcept {
            append_bytes(&v, sizeof(v));
        }

        __attribute__((always_inline)) void append_string(std::string_view v) noexcept {
            const std::size_t h = std::hash<std::string_view>{}(v);
            append_bytes(&h, sizeof(h));
        }

        friend auto operator==(const StitchKey& a, const StitchKey& b) noexcept -> bool {
            return a.len_ == b.len_ && std::equal(a.bytes_.begin(), a.bytes_.begin() + a.len_, b.bytes_.begin());
        }

        [[nodiscard]] auto data() const noexcept -> const std::byte* {
            return bytes_.data();
        }
        [[nodiscard]] auto size() const noexcept -> std::size_t {
            return len_;
        }

      private:
        __attribute__((always_inline)) void append_bytes(const void* src, std::size_t n) noexcept {
            std::memcpy(bytes_.data() + len_, src, n);
            len_ += n;
        }

        std::array<std::byte, CAPACITY> bytes_{};
        std::size_t                     len_ = 0;
    };
```

Add `std::hash<StitchKey>` specialization at namespace scope (outside `storm::orm::utilities`, since `std::hash` must live in `namespace std`):

```cpp
export template <> struct std::hash<storm::orm::utilities::StitchKey> {
    auto operator()(const storm::orm::utilities::StitchKey& k) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(
                std::string_view{reinterpret_cast<const char*>(k.data()), k.size()} // NOLINT
        );
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="StitchKeyTest.*" -v`
Expected: PASS — all 6 cases green.

- [ ] **Step 5: Commit**

```bash
git add src/orm/utilities.cppm tests/schema/test_stitch_key.cpp
git commit -m "feat(504): add StitchKey fixed-size byte-buffer type for composite stitch keys"
```

---

## Task 2: Widen `find_fk_primary_key` to `find_fk_primary_key_members`

**Files:**
- Modify: `src/orm/statements/base.cppm:749-760` (existing `find_fk_primary_key`)
- Test: `tests/schema/test_composite_fk_join_sql.cpp` (new — compile-time assertions)

**Interfaces:**
- Consumes: `T::primary_key_members_` (existing, `base.cppm:862`) — the target model's full PK list.
- Produces:
  ```cpp
  // Returns the target's full PK member list (1 element for a single-PK target,
  // N for composite) — the widened form of find_fk_primary_key. Every existing
  // call site of find_fk_primary_key becomes a loop over this array (Task 3).
  template <typename FKType>
      requires ValidForeignKey<FKType>
  static consteval auto find_fk_primary_key_members() -> std::array<std::meta::info, /* target PK count */>;
  ```
  Since the array size depends on the target's PK count (a `consteval` value), this needs the same two-function split `primary_key_members_` uses (`primary_key_count()` then `find_primary_key_members_impl()`):
  ```cpp
  template <typename FKType>
      requires ValidForeignKey<FKType>
  static consteval auto fk_primary_key_count() -> std::size_t {
      using InnerType = utilities::optional_inner_type_t<FKType>;
      return BaseStatement<InnerType>::primary_key_column_count_;
  }

  template <typename FKType>
      requires ValidForeignKey<FKType>
  static consteval auto find_fk_primary_key_members() -> std::array<std::meta::info, fk_primary_key_count<FKType>()> {
      using InnerType = utilities::optional_inner_type_t<FKType>;
      return BaseStatement<InnerType>::primary_key_members_;
  }
  ```
  **Keep `find_fk_primary_key` (singular) unchanged** — every existing single-column call site (INSERT/UPDATE/SELECT bind+extract for a single-PK FK target) keeps using it, byte-identical. `find_fk_primary_key_members` is additive, used only by the new composite-FK code paths (Tasks 3-7). This is the key simplification: **no existing call site changes in this task** — it only adds the new widened accessor, proven correct against `OrderLine` (2-part) and `Ledger` (3-part, mixed types) as targets.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/schema/test_composite_fk_join_sql.cpp
import storm;
#include "../test_models.h"
#include "../crud/test_composite_pk_models.h"
#include <gtest/gtest.h>

TEST(CompositeFkResolutionTest, FindFkPrimaryKeyMembersReturnsOnePartForSinglePkTarget) {
    // Message::sender is fk<> Person, Person has a single-column PK.
    using Base = storm::orm::statements::BaseStatement<Message>;
    constexpr auto members = Base::find_fk_primary_key_members<Person>();
    static_assert(members.size() == 1);
    SUCCEED();
}

TEST(CompositeFkResolutionTest, FindFkPrimaryKeyMembersReturnsTwoPartsForOrderLineTarget) {
    // Hypothetical FK type check: OrderLine has a 2-part composite PK
    // (order_id, product_id) — find_fk_primary_key_members must return both,
    // in declaration order, matching OrderLine::primary_key_members_.
    using Base = storm::orm::statements::BaseStatement<OrderLine>; // self-check: PK count on OrderLine itself
    static_assert(Base::primary_key_members_.size() == 2);
    SUCCEED();
}

TEST(CompositeFkResolutionTest, FkPrimaryKeyCountMatchesTargetPrimaryKeyColumnCount) {
    using OrderLineBase = storm::orm::statements::BaseStatement<OrderLine>;
    // fk_primary_key_count<OrderLine>() (as seen from some hypothetical FK holder)
    // must equal OrderLine's own primary_key_column_count_.
    EXPECT_EQ(OrderLineBase::primary_key_column_count_, 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -30`
Expected: FAIL — `find_fk_primary_key_members` does not exist on `BaseStatement`.

- [ ] **Step 3: Write minimal implementation**

In `src/orm/statements/base.cppm`, immediately after the existing `find_fk_primary_key` (ends line 760), add:

```cpp
        // Widened form of find_fk_primary_key (#504): returns the FK target's FULL
        // primary-key member list — 1 element for a single-column target (matching
        // find_fk_primary_key exactly), N for a composite target. Existing single-FK
        // call sites keep using find_fk_primary_key unchanged (byte-identical splice
        // shape); this is used only by the new composite-FK bind/extract/JOIN paths.
        template <typename FKType>
            requires ValidForeignKey<FKType>
        static consteval auto fk_primary_key_count() -> std::size_t {
            using InnerType = utilities::optional_inner_type_t<FKType>;
            return BaseStatement<InnerType>::primary_key_column_count_;
        }

        template <typename FKType>
            requires ValidForeignKey<FKType>
        static consteval auto find_fk_primary_key_members()
                -> std::array<std::meta::info, fk_primary_key_count<FKType>()> {
            using InnerType = utilities::optional_inner_type_t<FKType>;
            return BaseStatement<InnerType>::primary_key_members_;
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeFkResolutionTest.*" -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/orm/statements/base.cppm tests/schema/test_composite_fk_join_sql.cpp
git commit -m "feat(504): widen find_fk_primary_key to find_fk_primary_key_members for composite FK targets"
```

---

## Task 3: Composite-aware column-name writer for FK members

**Files:**
- Modify: `src/orm/field_attr.cppm` (near existing `append_column_name`/`column_name_size`, lines 291-304)
- Test: `tests/schema/test_composite_fk_join_sql.cpp` (add cases)

**Naming convention decision:** a composite FK member `[[= storm::fk<>]] OrderLine line;` referencing `OrderLine(order_id, product_id)` emits TWO columns: `line_order_id`, `line_product_id` — `<member_identifier>_<target_part_identifier>`. Rationale: the plain single-column case `Person sender` → `sender_id` is the degenerate form of `<member>_<target_part>` where the target part's identifier is elided in favor of the fixed `_id` (this is a **deliberate exception kept for byte-identical output**, not a generalization — the existing single-column rule stays a hardcoded special case, not derived from the general one, precisely so it can never accidentally drift). For composite, dropping the target part name would collide two columns into one name (`line_order_id` and `line_product_id` cannot both be spelled `line_id`), so the general rule spells the target part's identifier out.

**Interfaces:**
- Consumes: `BaseStatement<InnerType>::primary_key_members_` (via Task 2's `find_fk_primary_key_members`).
- Produces:
  ```cpp
  // Composite-aware sibling of append_column_name/column_name_size (#422). For a
  // single-column FK target, emits IDENTICAL output to append_column_name
  // (one "<member>_id" column) — this is the byte-identical guarantee. For a
  // composite target, emits N columns "<member>_<part1>, <member>_<part2>, ..."
  // separated by the caller-supplied separator (so it composes into both a
  // comma-separated field list and an AND-joined ON clause).
  template <std::size_t N>
  consteval auto fk_column_names_size(std::meta::info fk_member, const std::array<std::meta::info, N>& target_pk_members, std::string_view separator) -> std::size_t;

  template <typename Buf, std::size_t N>
  consteval auto append_fk_column_names(Buf& buf, std::meta::info fk_member, const std::array<std::meta::info, N>& target_pk_members, std::string_view separator) -> void;
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// Append to tests/schema/test_composite_fk_join_sql.cpp

TEST(FkColumnNamesTest, SingleColumnTargetMatchesLegacyAppendColumnName) {
    // Message::sender (fk<> Person, single-column PK) must emit exactly what
    // append_column_name emits today: "sender_id".
    using MessageBase = storm::orm::statements::BaseStatement<Message>;
    constexpr auto sender_member = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^Message, std::meta::access_context::unchecked()))
            if (std::meta::identifier_of(m) == "sender") return m;
        std::unreachable();
    }();
    constexpr auto target_pk = MessageBase::find_fk_primary_key_members<Person>();
    constexpr auto size = storm::meta::fk_column_names_size(sender_member, target_pk, ", ");
    std::string buf;
    buf.reserve(size);
    storm::meta::append_fk_column_names(buf, sender_member, target_pk, ", ");
    EXPECT_EQ(buf, "sender_id");
}

TEST(FkColumnNamesTest, CompositeTargetEmitsOneColumnPerPart) {
    // A hypothetical FK member `line` of type OrderLine (2-part composite PK:
    // order_id, product_id) must emit "line_order_id, line_product_id".
    // (Uses a hand-rolled member array since no in-tree model has this FK yet —
    // this test validates the writer in isolation before Task 4 wires up a real model.)
    constexpr auto target_pk = storm::orm::statements::BaseStatement<OrderLine>::primary_key_members_;
    // fk_member is a placeholder info — the writer only reads the FK member's own
    // identifier, so any member reflection with identifier "line" would do; here we
    // reuse OrderLine::order_id's info as a stand-in reflection value and rely on
    // the test asserting the TARGET side (order_id/product_id) is correctly spelled
    // to prove the per-part loop is right; Task 4 covers the member-identifier prefix
    // end to end against a real composite-FK-holding model.
    std::string buf;
    storm::meta::append_fk_column_names(buf, target_pk[0], target_pk, ", ");
    EXPECT_EQ(buf, "order_id_order_id, order_id_product_id");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -30`
Expected: FAIL — `fk_column_names_size`/`append_fk_column_names` not declared.

- [ ] **Step 3: Write minimal implementation**

In `src/orm/field_attr.cppm`, immediately after `column_name_size` (ends around line 304), add:

```cpp
    // Composite-aware sibling of append_column_name/column_name_size (#422, widened
    // #504). A single-column FK target degenerates to EXACTLY append_column_name's
    // output ("<member>_id") — kept as a hardcoded special case (not derived from
    // the N>=2 branch) so the single-PK byte-identical guarantee can never drift.
    // A composite target spells the target part's own identifier into each column
    // name ("<member>_<part>"), since eliding it (as the single-column "_id" does)
    // would collide multiple parts into the same name.
    template <std::size_t N>
    consteval auto fk_column_names_size(
            std::meta::info fk_member, const std::array<std::meta::info, N>& target_pk_members,
            std::string_view separator
    ) -> std::size_t {
        if constexpr (N == 1) {
            return column_name_size(fk_member); // "<member>_id" — byte-identical to today
        } else {
            const auto member_name = std::meta::identifier_of(fk_member);
            std::size_t total = 0;
            for (std::size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    total += separator.size();
                }
                total += member_name.size() + 1 + std::meta::identifier_of(target_pk_members[i]).size();
            }
            return total;
        }
    }

    template <typename Buf, std::size_t N>
    consteval auto append_fk_column_names(
            Buf& buf, std::meta::info fk_member, const std::array<std::meta::info, N>& target_pk_members,
            std::string_view separator
    ) -> void {
        if constexpr (N == 1) {
            append_column_name(buf, fk_member); // "<member>_id" — byte-identical to today
        } else {
            const auto member_name = std::meta::identifier_of(fk_member);
            for (std::size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    buf.append(separator);
                }
                buf.append(member_name);
                buf.append("_");
                buf.append(std::meta::identifier_of(target_pk_members[i]));
            }
        }
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="FkColumnNamesTest.*" -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/orm/field_attr.cppm tests/schema/test_composite_fk_join_sql.cpp
git commit -m "feat(504): composite-aware FK column-name writer (N columns per target PK part)"
```

---

## Task 4: Add composite-FK test model fixtures

**Files:**
- Modify: `tests/crud/test_composite_pk_models.h`

**Interfaces:**
- Produces new model types used by every remaining task's tests:
  ```cpp
  // Regular-FK JOIN target: a child row referencing OrderLine's 2-part composite PK.
  struct Shipment {
      [[= storm::primary_autoincrement]] int id{};
      [[= storm::fk<>]] OrderLine line;
      std::string carrier;
  };

  // Many-to-many with a composite-PK side. Ledger (3-part, mixed types: int,
  // string, int64_t) on one side, Widget (single-PK control) on the other —
  // proves m2m works when EITHER side (here: the owner side of the eager-load)
  // has a composite key.
  struct LedgerTag {
      [[= storm::primary_autoincrement]] int id{};
      std::string label;
  };
  // Ledger needs an m2m field added: see note below.

  // Reverse-FK with a composite-PK owner: "all OrderLines, each with the
  // Shipments that reference them" — reuses Shipment from above as the owning
  // side; OrderLine gains a reverse_fk container.
  ```

  Since `OrderLine`/`Ledger` are existing structs in this header (used by #501's tests), this task must ADD fields/new structs without changing existing field layouts (those are asserted byte-identical elsewhere). Concretely:
  - Add `Shipment` (new struct) — FK to `OrderLine` (regular JOIN test subject).
  - Add `OrderLineShipments` capability: add a `reverse_fk<^^Shipment::line>` container field to a NEW struct `OrderLineWithShipments` (do NOT modify `OrderLine` itself, since #501's tests assert its exact shape) — OR, if the reverse-FK annotation must live on the actual JOIN target, confirm with a quick read of `test_composite_pk_models.h`'s current full content immediately before writing this task's code, to decide whether adding a field to `OrderLine` directly is safe (check no test asserts `OrderLine`'s exact field COUNT via `field_count_ == N` literal).
  - Add `LedgerEntryTag` (new struct, single-PK) + add an m2m container field to `Ledger` OR a new `LedgerWithTags` struct — same field-count caution as above.

- [ ] **Step 1: Write the failing test**

Not applicable in the TDD sense (this task adds fixtures, not behavior) — instead, the "test" is a compile check: **before writing the new structs**, run the existing composite-PK test suite to record a baseline:

Run: `ctest --preset ninja-debug-sqlite --output-on-failure -R CompositePK 2>&1 | tail -20`
Expected: current tests pass (baseline — proves the header compiles before this task's edits).

- [ ] **Step 2: Read the current file in full and confirm no literal field-count assertions**

Run: `grep -n "field_count_\|primary_key_column_count_" tests/crud/test_composite_pk_sql.cpp tests/crud/test_composite_pk_crud.cpp`
Expected: either no hits, or hits that read the count via `Model::field_count_` symbolically (safe to add fields) rather than a hardcoded literal like `EXPECT_EQ(4, ...)` (would break if a field is added — in that case, add NEW structs instead of extending existing ones).

- [ ] **Step 3: Write the new structs**

Add to `tests/crud/test_composite_pk_models.h`, after the existing `StockEntry` struct (line 58), before the header guard close:

```cpp
// #504 — FK targeting a composite PK. Shipment references OrderLine's 2-part
// key (order_id, product_id) via the SAME fk<> annotation as any single-column
// FK; the composite-ness lives entirely in OrderLine's own declaration.
struct Shipment {
    [[= storm::primary_autoincrement]] int id{};
    [[= storm::fk<>]] OrderLine line;
    std::string carrier;
};

// #504 — reverse-FK destination with a composite-PK owner: "all OrderLines,
// each with the Shipments that reference them". A separate struct (not added
// to OrderLine itself) so existing OrderLine-shape assertions in the #501/#502
// test suites stay untouched.
struct OrderLineWithShipments {
    [[= storm::primary_part]] int order_id{};
    [[= storm::primary_part]] int product_id{};
    int quantity{};
    std::string note;
    [[= storm::reverse_fk<^^Shipment>]] std::vector<Shipment> shipments;
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
```

- [ ] **Step 4: Verify existing tests still pass (no regression from the header edit)**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -40`
Expected: builds clean (the new structs don't yet compile-check against FK/JOIN/m2m machinery for composite targets — that's Tasks 5-8; this task only proves the model DECLARATIONS themselves compile, i.e. `ModelPrimaryKeyValid`/`ModelAnnotationsValid` accept them and `fk<>`/`reverse_fk<>`/`many_to_many<>` accept a composite-PK type as their target. If this build FAILS here, that failure is itself informative — it means one of Tasks 2/3/5-8's concept widening is a hard prerequisite even to declare the model, and this task's "Step 4" becomes the trigger to jump to that task first.)

Run: `ctest --preset ninja-debug-sqlite --output-on-failure -R "CompositePK|Fk" 2>&1 | tail -30`
Expected: all PASS (no regression).

- [ ] **Step 5: Commit**

```bash
git add tests/crud/test_composite_pk_models.h
git commit -m "test(504): add composite-FK test fixtures (Shipment, OrderLineWithShipments, LedgerWithTags)"
```

---

## Task 5: `ValidForeignKey`/`FKFieldOf` composite-arity compile-time gate

**Files:**
- Modify: `src/orm/statements/base.cppm:240-247` (`ValidForeignKey`), `:443-456` (`FKFieldOf`)
- Test: `tests/schema/test_composite_fk_join_sql.cpp` (negative-compile test, per memory: gate the predicate, not a public call)

**Interfaces:**
- `ValidForeignKey<FieldType>` already accepts any target with `ModelWithPrimaryKey` (true for composite too, since #500 widened `is_primary_member`) — **no change needed here**, confirmed by reading `base.cppm:246-247`. The arity/type-match concern in the issue's DoD ("arity or type mismatch is a compile error at the named concept") applies to a DIFFERENT shape than this codebase has: since the local FK member's type IS the whole related object (not N separate local columns), there is no separate "local arity" to mismatch against — the local side is always "one member, whose type has its own N-part key," which is structurally always arity-correct by construction. **This resolves DoD item 3 as already satisfied** — document this finding rather than writing new code.
- Add one negative-compile-style assertion documenting the resolved finding.

- [ ] **Step 1: Write the test (documents the finding, doesn't test new code)**

```cpp
// Append to tests/schema/test_composite_fk_join_sql.cpp

TEST(CompositeFkArityTest, ValidForeignKeyAcceptsCompositePkTargetWithNoArityMismatchPossible) {
    // #504 finding: fk<> annotates ONE member whose TYPE is the whole related
    // object — there are no separate "local FK columns" to mismatch in count
    // against the target's PK arity (unlike SQLAlchemy's ForeignKeyConstraint,
    // which lists local columns explicitly). So ValidForeignKey<OrderLine> just
    // needs OrderLine to have A primary key (single or composite) — already
    // true via ModelWithPrimaryKey (#500 widened is_primary_member for
    // primary_part). No new concept code needed; this test is a permanent
    // regression guard on that reasoning.
    static_assert(storm::orm::statements::ValidForeignKey<OrderLine>);
    static_assert(storm::orm::statements::ValidForeignKey<std::optional<OrderLine>>);
    SUCCEED();
}
```

- [ ] **Step 2: Run test to verify it currently passes (confirms the finding without new code)**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeFkArityTest.*" -v`
Expected: PASS immediately — no implementation step needed, this is a documentation-via-test task.

- [ ] **Step 3: Commit**

```bash
git add tests/schema/test_composite_fk_join_sql.cpp
git commit -m "test(504): document ValidForeignKey already accepts composite-PK targets (no arity mismatch possible)"
```

---

## Task 6: Composite FK bind + extract (INSERT/SELECT plain FK column) for a single FK member

**Files:**
- Modify: `src/orm/statements/base.cppm` — new composite-aware sibling functions next to `bind_fk_field_at_index` (:1005-1027) and `extract_column_fast`'s FK branch (:1219-1227). **Do not modify the existing single-column functions** — add `if constexpr` branches keyed on `fk_primary_key_count<FKType>() == 1` vs `> 1` so the single-column path is untouched code, not a generalized-then-specialized path.
- Test: `tests/crud/test_composite_fk_join.cpp` (new)

**Interfaces:**
- `bind_fk_field_at_index` gains an `if constexpr (fk_primary_key_count<FKType>() > 1)` branch that loops `find_fk_primary_key_members<FKType>()` and binds each part (mirroring `bind_one_pk_part`'s per-part dispatch from #501), advancing `param_index` by the target's PK count instead of 1.
- `extract_column_fast`'s FK branch gains a parallel composite extraction branch, reading N consecutive columns starting at `Index` into the correspondingly-many parts of the FK's inner object, and widening the caller's column-index bookkeeping by `fk_primary_key_count<FieldType>()` instead of 1 (this ripples into `FieldNameGrammar`'s column-count math — check that file for a "1 column per member" assumption before finishing this task).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/crud/test_composite_fk_join.cpp
import storm;
#include "../test_models.h"
#include "../crud/test_composite_pk_models.h"
#include <gtest/gtest.h>

template <typename ConnType>
class CompositeFkInsertSelectTest : public storm::test::StormTestFixture<Shipment, ConnType, OrderLine> {};
TYPED_TEST_SUITE(CompositeFkInsertSelectTest, DatabaseTypes);

TYPED_TEST(CompositeFkInsertSelectTest, InsertAndSelectRoundTripsCompositeFkColumns) {
    storm::QuerySet<OrderLine, TypeParam> line_qs;
    OrderLine line{.order_id = 5, .product_id = 12, .quantity = 3, .note = "first"};
    ASSERT_TRUE(line_qs.insert(line).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    Shipment ship{.line = line, .carrier = "UPS"};
    auto insert_result = ship_qs.insert(ship).execute();
    ASSERT_TRUE(insert_result.has_value());

    auto loaded = ship_qs.first();
    ASSERT_TRUE(loaded.has_value());
    ASSERT_TRUE(loaded->has_value());
    EXPECT_EQ(loaded->value().line.order_id, 5);
    EXPECT_EQ(loaded->value().line.product_id, 12);
    EXPECT_EQ(loaded->value().carrier, "UPS");
}

TYPED_TEST(CompositeFkInsertSelectTest, SchemaEmitsTwoColumnsForCompositeFk) {
    // line_order_id, line_product_id must both exist as columns.
    const std::string& sql = storm::create_table_sql<Shipment>();
    EXPECT_NE(sql.find("line_order_id"), std::string::npos);
    EXPECT_NE(sql.find("line_product_id"), std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -40`
Expected: FAIL — likely a compile error inside `extract_column_fast`/`bind_fk_field_at_index`'s existing single-splice code attempting `obj.[:member:].[:fk_pk_member:]` where `fk_pk_member` is now ambiguous for a composite target, OR a schema-generation error since `schema.cppm`'s column-def loop doesn't yet know to emit 2 columns for one FK member. Record the EXACT failure before proceeding — it tells you whether schema.cppm needs a parallel widening in this task (likely yes: check `schema.cppm`'s per-member column-definition loop for a hardcoded "1 column per member" assumption and fix it alongside this task if so, since the test cannot pass without table creation working).

- [ ] **Step 3: Write minimal implementation**

In `src/orm/statements/base.cppm`, modify `bind_fk_field_at_index` (existing, :1005-1027) by adding a composite branch — full replacement of the function body:

```cpp
        template <typename ConnType, std::size_t Index>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_fk_field_at_index(typename ConnType::Statement* stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];
            using FKType          = std::remove_cvref_t<decltype(obj.[:member:])>;
            if constexpr (utilities::is_optional_v<FKType>) {
                if (!obj.[:member:].has_value()) {
                    // A composite-null FK binds N nulls, one per target PK part.
                    constexpr auto count = fk_primary_key_count<FKType>();
                    for (std::size_t i = 0; i < count; ++i) {
                        auto null_result = stmt->bind_null(param_index);
                        if (!null_result) {
                            return std::unexpected(null_result.error());
                        }
                        ++param_index;
                    }
                    return {};
                }
                return bind_fk_parts<ConnType, FKType>(stmt, obj.[:member:].value(), param_index);
            } else {
                return bind_fk_parts<ConnType, FKType>(stmt, obj.[:member:], param_index);
            }
        }

        // Bind every part of the FK target's primary key, in declaration order,
        // advancing param_index past all of them. Single-column targets bind
        // exactly one value (identical to the pre-#504 code this replaces).
        template <typename ConnType, typename FKType>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_fk_parts(typename ConnType::Statement* stmt, const auto& fk_obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto members = find_fk_primary_key_members<FKType>();
            std::expected<void, typename ConnType::Error> result{};
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((result = bind_value_by_type<ConnType>(*stmt, param_index, fk_obj.[:members[Is]:]),
                  result.has_value() ? (++param_index, true) : false),
                 ...);
            }(std::make_index_sequence<members.size()>{});
            return result;
        }
```

Modify `extract_column_fast`'s FK branch (existing, :1219-1227) similarly — replace the non-optional FK branch:

```cpp
                if constexpr (is_fk_field(member)) {
                    if constexpr (utilities::is_optional_v<FieldType>) {
                        extract_optional_fk_column<Index, Statement, FieldType>(stmt, obj);
                    } else {
                        obj.[:member:] = FieldType{};
                        extract_fk_parts<Index, Statement, FieldType>(stmt, obj.[:member:]);
                    }
                }
```

Add the new helper near `extract_optional_fk_column`:

```cpp
        // Extract every part of an FK target's primary key from consecutive columns
        // starting at Index, in declaration order. Single-column targets extract
        // exactly one value (identical to the pre-#504 code this generalizes).
        template <std::size_t Index, typename Statement, typename FieldType>
        __attribute__((always_inline)) static void extract_fk_parts(Statement* stmt, auto& fk_obj) noexcept {
            constexpr auto members = find_fk_primary_key_members<FieldType>();
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (
                        [&] {
                            using PartType = std::remove_cvref_t<decltype(fk_obj.[:members[Is]:])>;
                            fk_obj.[:members[Is]:] =
                                    ColumnExtractor::extract_column_value<PartType>(stmt, Index + static_cast<int>(Is));
                        }(),
                        ...
                );
            }(std::make_index_sequence<members.size()>{});
        }
```

**Note for the implementer:** this task's Step 2 failure will likely reveal that `schema.cppm`'s DDL column-emission loop and `FieldNameGrammar`'s field-name-list builder ALSO assume 1 column per member (they iterate `all_members_` and call `append_column_name` once per member). Both need the same `if constexpr (fk_primary_key_count<FieldType>() > 1)` branch, calling `append_fk_column_names` (Task 3) instead of `append_column_name`, with the per-column SQL TYPE also repeated once per part (an FK part's SQL type is the target part's own type, e.g. `INTEGER` for `order_id`, `INTEGER` for `product_id` — read each part's type via `std::meta::type_of(members[Is])` in the schema column-def builder). This is a real sub-task; if it surfaces here, execute it as a nested Task 6a (write its own failing test first — a schema SQL text assertion — before patching `schema.cppm`), then return to this task's Step 4.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeFkInsertSelectTest.*" -v`
Expected: PASS on both SQLite and PostgreSQL (TYPED_TEST).

- [ ] **Step 5: Run full existing suite for regressions**

Run: `ctest --preset ninja-debug 2>&1 | tail -40`
Expected: 100% pass — no existing single-column FK test regresses.

- [ ] **Step 6: Commit**

```bash
git add src/orm/statements/base.cppm src/orm/schema.cppm tests/crud/test_composite_fk_join.cpp
git commit -m "feat(504): bind/extract composite FK columns (INSERT/SELECT plain FK member, non-JOIN path)"
```

---

## Task 7: Regular FK `JoinStatement` — multi-column ON clause + extraction

**Files:**
- Modify: `src/orm/statements/join.cppm:300-619` (`JoinStatement` class — `column_offsets_`, `calculate_join_sql_size`, `build_join_sql_array`, `calculate_select_fields_size`, `build_select_fields_array`, `extract_fk_at`)
- Test: `tests/query/test_composite_fk_join.cpp` (new)

**Interfaces:**
- `column_offsets_` (existing, `:332-345`) already advances by `FKBase_at<Is>::field_count_` per FK — this is UNCHANGED (it already handles a multi-column FK target correctly, since it sums the target's full field count, not just its PK). Verify this by reading the arithmetic again: yes, `current_offset += FKBase_at<Is>::field_count_` already accounts for however many columns the target contributes. **No change needed here.**
- `calculate_join_sql_size`/`build_join_sql_array` (`:361-399`) hardcode ONE `ON t<alias>.<pk> = t1.<fk>_id` per FK. Widen to emit `ON t<alias>.<part1> = t1.<fk>_<part1>_id AND t<alias>.<part2> = t1.<fk>_<part2>_id ...` for a composite target, using `find_fk_primary_key_members<FK_type<Is>>()` for the target-side names and `append_fk_column_names` (Task 3) for the local-side names.
- `extract_fk_at` (`:558-582`) already extracts `field_count` columns per FK (the whole target row) via `extract_fk_fields_impl` looping `FKBase_at<Idx>::field_count_` — this is UNCHANGED for the SAME reason as `column_offsets_`: it was never PK-specific, it extracts the FULL related row. **No change needed here either** — confirm with a read before editing.

This task is smaller than it first appears: **only the JOIN `ON` clause SQL text (`calculate_join_sql_size`/`build_join_sql_array`) needs to change.** Everything else in `JoinStatement` already generalizes because it was written in terms of "the target's full column list," not "the target's PK."

- [ ] **Step 1: Write the failing test**

```cpp
// Append to tests/query/test_composite_fk_join.cpp

template <typename ConnType>
class CompositeFkRegularJoinTest : public storm::test::StormTestFixture<Shipment, ConnType, OrderLine> {};
TYPED_TEST_SUITE(CompositeFkRegularJoinTest, DatabaseTypes);

TYPED_TEST(CompositeFkRegularJoinTest, InnerJoinAcrossCompositeFkReturnsMatchingRow) {
    storm::QuerySet<OrderLine, TypeParam> line_qs;
    OrderLine line{.order_id = 5, .product_id = 12, .quantity = 3, .note = "first"};
    ASSERT_TRUE(line_qs.insert(line).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line, .carrier = "UPS"}).execute().has_value());

    auto results = ship_qs.template join<^^Shipment::line>().select();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1);
    auto& shipment = *results->begin();
    EXPECT_EQ(shipment.line.order_id, 5);
    EXPECT_EQ(shipment.line.product_id, 12);
    EXPECT_EQ(shipment.line.quantity, 3);
    EXPECT_EQ(shipment.carrier, "UPS");
}

TYPED_TEST(CompositeFkRegularJoinTest, LeftJoinKeepsRowWithNoMatchingCompositeFkTarget) {
    // Insert a Shipment whose OrderLine does NOT exist (dangling FK — allowed
    // pre-referential-integrity-check at the ORM layer for this SQL-shape test;
    // if the schema enforces FK integrity, this test instead deletes the
    // OrderLine after insert to produce the same "no match" condition).
    storm::QuerySet<OrderLine, TypeParam> line_qs;
    OrderLine line{.order_id = 99, .product_id = 1, .quantity = 1, .note = "temp"};
    ASSERT_TRUE(line_qs.insert(line).execute().has_value());
    storm::QuerySet<Shipment, TypeParam> ship_qs;
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = line, .carrier = "FedEx"}).execute().has_value());
    ASSERT_TRUE(line_qs.where(f<^^OrderLine::order_id>() == 99).erase().execute().has_value());

    auto results = ship_qs.template left_join<^^Shipment::line>().select();
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1); // LEFT keeps the shipment even with no OrderLine match
}

TEST(CompositeFkJoinSqlTest, JoinOnClauseAndJoinsBothCompositeKeyParts) {
    using JS = storm::orm::statements::JoinStatement<Shipment, storm::SqliteConnection, storm::orm::statements::JoinType::Inner, ^^Shipment::line>;
    const std::string& sql = JS::get_complete_sql();
    EXPECT_NE(sql.find("t2.order_id = t1.line_order_id"), std::string::npos);
    EXPECT_NE(sql.find("t2.product_id = t1.line_product_id"), std::string::npos);
    EXPECT_NE(sql.find(" AND "), std::string::npos);
}

TEST(CompositeFkJoinSqlTest, SinglePkJoinSqlStaysByteIdentical) {
    // Regression guard: Message::sender (single-column PK target) JOIN SQL must
    // be EXACTLY what it was before #504.
    using JS = storm::orm::statements::JoinStatement<Message, storm::SqliteConnection, storm::orm::statements::JoinType::Inner, ^^Message::sender>;
    const std::string& sql = JS::get_complete_sql();
    EXPECT_NE(sql.find("ON t2.id = t1.sender_id"), std::string::npos);
    EXPECT_EQ(sql.find(" AND "), std::string::npos); // no AND for a single-column key
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -40`
Expected: FAIL — `calculate_join_sql_size`/`build_join_sql_array` currently emit `ON t2.<pk_name_> = t1.<fk>_id` unconditionally using `FKBase_at<Is>::pk_name_` (the single FIRST PK part per #500's convention), so the composite test's second part (`product_id`) is silently missing from the `ON` clause — this is a **wrong-JOIN-result bug**, not a compile error, so the assertion on `results->size()` may actually pass spuriously if only one Shipment/OrderLine pair exists. Strengthen the test setup to insert a SECOND `OrderLine` sharing the same `order_id` but a different `product_id`, and a Shipment pointing at each, to prove the JOIN doesn't ALSO match the wrong `OrderLine` (only `order_id` in the `ON` clause). This is the concrete manifestation of the design doc's warning.

- [ ] **Step 3: Write minimal implementation**

Modify `calculate_join_sql_size` (`src/orm/statements/join.cppm:361-377`) — replace the per-FK size computation:

```cpp
        static consteval auto calculate_join_sql_size() -> std::size_t {
            using utilities::numeric::digits_of;
            using utilities::sql_len::SMALL_BUFFER;
            std::size_t total = 0;

            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((total += get_join_keyword().size() + FKBase_at<Is>::table_name_.size() + 2 + digits_of(Is + 2) +
                           5 + digits_of(Is + 2) + single_fk_on_clause_size<Is>()),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + SMALL_BUFFER;
        }

        // Byte size of "ON t<alias>.<p1> = t1.<fk>_<p1>[_id] AND t<alias>.<p2> = t1.<fk>_<p2> ..."
        // for FK index Is. N=1 collapses to " ON t<alias>.<pk> = t1.<fk>_id" — byte-identical
        // to the pre-#504 single-column clause.
        template <std::size_t Is> static consteval auto single_fk_on_clause_size() -> std::size_t {
            using utilities::sql_len::ON_EQUALS;
            constexpr auto target_pk = Base::template find_fk_primary_key_members<FK_type<Is>>();
            constexpr std::size_t AND_JOIN = 5; // " AND "
            std::size_t total = 4; // " ON "
            for (std::size_t p = 0; p < target_pk.size(); ++p) {
                if (p > 0) total += AND_JOIN;
                total += std::meta::identifier_of(target_pk[p]).size();
                total += ON_EQUALS; // " = "
                total += storm::meta::fk_column_names_size(FKFields...[Is], target_pk, "") / target_pk.size(); // per-part local name — see note
            }
            return total;
        }
```

**Implementer note:** the per-part local-name sizing above is awkward (dividing a joint size by count). Instead of that shortcut, use a dedicated per-part local-name sizer: since `append_fk_column_names`'s composite branch already builds `<fk>_<part>` per part with a known separator, refactor Task 3's helper to expose a `fk_column_name_size_for_part(fk_member, target_pk_members, part_index)` and `append_fk_column_name_for_part(...)` pair — add these to `field_attr.cppm` in this task (extending Task 3's interface) so both the comma-separated list form (Task 6/schema) and the AND-joined `ON`-clause form (this task) share one per-part primitive instead of duplicating the name-construction logic. Write this as a small addendum to Task 3's functions before finishing this task's implementation.

Then `build_join_sql_array` (`:379-399`) mirrors the sizing function structurally — replace its per-FK body to call a matching `append_single_fk_on_clause<Is>(result)` that loops the target PK parts with `AND` joins, using `append_fk_column_name_for_part` for the local side and `std::meta::identifier_of(target_pk[p])` for the target side (target side never gets a suffix — it's the target's OWN column name, exactly as `FKBase_at<Is>::pk_name_` was used before, just looped over N parts instead of 1).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeFkRegularJoinTest.*:CompositeFkJoinSqlTest.*" -v`
Expected: PASS, including the strengthened two-OrderLine disambiguation case and the single-PK byte-identical regression guard.

- [ ] **Step 5: Run full existing JOIN suite for regressions**

Run: `ctest --preset ninja-debug -R "Join" --output-on-failure 2>&1 | tail -40`
Expected: 100% pass.

- [ ] **Step 6: Commit**

```bash
git add src/orm/field_attr.cppm src/orm/statements/join.cppm tests/query/test_composite_fk_join.cpp
git commit -m "feat(504): multi-column AND-joined ON clause for regular FK JoinStatement over composite targets"
```

---

## Task 8: `TwoQueryJoinBase`/`M2MRelation` — StitchKey-based owner-key extraction

**Files:**
- Modify: `src/orm/statements/join.cppm:43-50` (`M2MRelation`), `:264-298` (`TwoQueryJoinBase::extract_q2_owner_pk`), `:1144-1166` (`make_relation_descriptor`)
- Modify: `src/orm/statements/select.cppm:660,716-738` (`by_pk` map + `run_q2_stitch`)
- Test: `tests/query/test_composite_fk_join.cpp` (new — m2m + reverse-FK composite-owner cases)

**Interfaces:**
- `M2MRelation::extract_q2_owner_pk_fn` type changes from `auto(*)(ErasedStatementPtr) -> std::int64_t` to `auto(*)(ErasedStatementPtr) -> utilities::StitchKey`.
- `TwoQueryJoinBase::extract_q2_owner_pk` changes from `stmt->extract_int64(0)` to a loop building a `StitchKey` from `Base::primary_key_members_` (using each part's own type to decide `append_int64` vs `append_string`), reading columns `[0, N)` instead of just column `0`.
- `by_pk` in `select.cppm:660` changes from `std::unordered_map<std::int64_t, T*>` to `std::unordered_map<utilities::StitchKey, T*>`; the key-building loop at `:662-664` changes from `static_cast<std::int64_t>(obj.[:Base::primary_key_:])` to a loop over `Base::primary_key_members_` appending each part into a `StitchKey`.
- Also requires: the Q1 SELECT column LIST (`build_base_subquery_sql`) and Q2's owner-key SELECT column(s) (`append_q2_select_head`, `append_in_subquery_open`) to emit ALL PK parts, not just `pk_name_`. This ripples into:
  - `append_in_subquery_open<Base>` (`:179-184`): `" IN (SELECT " + Base::pk_name_ + " FROM " + Base::table_name_` → needs `" IN (SELECT (" + <comma-joined PK parts> + ") FROM " + Base::table_name_` for a composite base, using the row-value form (matching #501's bulk-DELETE precedent: PG always supports it, SQLite ≥ 3.15 vs the project's 3.35 floor). Single-PK stays the bare-column form (row-value syntax `(col) IN (SELECT (col) FROM ...)` is valid SQL but NOT byte-identical to today's plain form — so this must branch on `has_composite_pk_` and keep the exact old string for N=1).
  - `append_q2_select_head`/Q2's owner-key SELECT (`:216-229`, and the two call sites at `:910` and `:1096`): `"SELECT t2." + owner_key + [_id]` → needs `"SELECT t2.<part1>, t2.<part2>, ..."` for composite, and the stitch extractor (`extract_q2_owner_pk`) must read from column 0 through column N-1 instead of assuming the owner key is exactly 1 column at index 0, then the RELATED columns shift their offset by `N` instead of `1` (this changes `append_related_q2`'s `insert_related<1>`/`insert_owner`'s `extract_relation_entity<..., 1>` column-offset constant in `M2MJoinStatement`/`ReverseFKJoinStatement` from a literal `1` to the base's PK column count).

This is the highest-risk task in the plan — it touches the hottest path (every m2m/reverse-FK query) and the most call sites. Because of that breadth, execute it in two sub-steps within this task: first prove the single-relation m2m case end-to-end, then the reverse-FK case (which reuses the same machinery), verifying the single-PK path stays byte-identical after EACH sub-step rather than only at the end.

- [ ] **Step 1: Write the failing test**

```cpp
// Append to tests/query/test_composite_fk_join.cpp

template <typename ConnType>
class CompositeM2MOwnerTest : public storm::test::StormTestFixture<LedgerWithTags, ConnType, LedgerTag> {};
TYPED_TEST_SUITE(CompositeM2MOwnerTest, DatabaseTypes);

TYPED_TEST(CompositeM2MOwnerTest, M2MEagerLoadStitchesCorrectlyWithCompositeOwnerKey) {
    storm::QuerySet<LedgerTag, TypeParam> tag_qs;
    ASSERT_TRUE(tag_qs.insert(LedgerTag{.label = "reconciled"}).execute().has_value());
    ASSERT_TRUE(tag_qs.insert(LedgerTag{.label = "flagged"}).execute().has_value());

    storm::QuerySet<LedgerWithTags, TypeParam> ledger_qs;
    LedgerWithTags a{.region = 1, .account = "acct-A", .period = 202601, .balance = 100.0};
    LedgerWithTags b{.region = 1, .account = "acct-B", .period = 202601, .balance = 200.0}; // SAME region+period, DIFFERENT account — proves all 3 parts matter
    ASSERT_TRUE(ledger_qs.insert(a).execute().has_value());
    ASSERT_TRUE(ledger_qs.insert(b).execute().has_value());

    // Attach tag 1 to `a` only, via the junction table directly (test setup helper
    // — exact mechanism depends on how many_to_many<> auto-junction insert is
    // exposed; if there's no direct-attach API yet, use the ORM's existing m2m
    // attach path from the #203/#392 test suite as a model for this line).

    auto results = ledger_qs.template join<^^LedgerWithTags::tags>().select();
    ASSERT_TRUE(results.has_value());
    // `a`'s tags must contain "reconciled"; `b`'s tags must be empty (INNER drops
    // it, or LEFT keeps it empty) — proves the stitch keyed on (region, account,
    // period) did NOT merge a and b despite sharing region+period.
    bool found_a_with_tag = false;
    for (const auto& ledger : *results) {
        if (ledger.account == "acct-A") {
            ASSERT_EQ(ledger.tags.size(), 1);
            EXPECT_EQ(ledger.tags[0].label, "reconciled");
            found_a_with_tag = true;
        }
        if (ledger.account == "acct-B") {
            FAIL() << "acct-B should have been dropped by INNER join (no tags) or, "
                      "if this becomes a LEFT-join test, asserted empty here";
        }
    }
    EXPECT_TRUE(found_a_with_tag);
}

TYPED_TEST(CompositeM2MOwnerTest, EmptyRelationOnCompositeOwnerReturnsNoInnerJoinRows) {
    storm::QuerySet<LedgerWithTags, TypeParam> ledger_qs;
    ASSERT_TRUE(ledger_qs.insert(LedgerWithTags{.region = 9, .account = "no-tags", .period = 1, .balance = 0}).execute().has_value());
    auto results = ledger_qs.template join<^^LedgerWithTags::tags>().select(); // INNER
    ASSERT_TRUE(results.has_value());
    EXPECT_TRUE(results->empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -60`
Expected: FAIL, likely at COMPILE time first (the `extract_q2_owner_pk_fn` signature mismatch once `LedgerWithTags::primary_key_members_.size() > 1` is fed through code still assuming `std::int64_t`), or if it compiles via silent truncation, FAIL at runtime with acct-A and acct-B's tags merged (both share region=1,period=202601, and today's stitch only keys on the FIRST PK part per model — wait, `primary_key_` is `region` for `LedgerWithTags`, an `int`, so today's `static_cast<std::int64_t>(obj.[:Base::primary_key_:])` would key SOLELY on `region=1` for both rows — a **collision**, proving the exact bug the issue describes).

- [ ] **Step 3: Write minimal implementation**

In `src/orm/statements/join.cppm`, change `M2MRelation` (`:43-50`):

```cpp
    struct M2MRelation {
        M2MClauseSqlFn build_q2_sql_fn                                                    = nullptr;
        auto (*extract_q2_owner_pk_fn)(ErasedStatementPtr) -> storm::orm::utilities::StitchKey = nullptr;
        auto (*append_related_q2_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void             = nullptr;
        auto (*container_empty_fn)(ErasedObjectPtr) -> bool                                   = nullptr;
        bool is_left = false;
    };
```

Change `TwoQueryJoinBase::extract_q2_owner_pk` (`:294-297`):

```cpp
        // Q2 row owner key (columns 0..N-1) — keys the stitch into the Q1 hash map.
        // N=1 (single-column PK) reads exactly column 0 as an int64 — byte-identical
        // codepath to before (StitchKey with one append_int64 call).
        static auto extract_q2_owner_pk(typename ConnType::Statement* stmt) noexcept -> storm::orm::utilities::StitchKey {
            storm::orm::utilities::StitchKey key;
            constexpr auto pk_members = Base::primary_key_members_;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (append_pk_part_to_key<Is>(key, stmt, pk_members), ...);
            }(std::make_index_sequence<pk_members.size()>{});
            return key;
        }

      private:
        template <std::size_t Is>
        static void append_pk_part_to_key(
                storm::orm::utilities::StitchKey& key, typename ConnType::Statement* stmt,
                const auto& pk_members
        ) noexcept {
            using PartType = std::remove_cvref_t<typename[:std::meta::type_of(pk_members[Is]):]>;
            if constexpr (std::same_as<PartType, std::string> || std::same_as<PartType, std::string_view>) {
                key.append_string(stmt->extract_text(static_cast<int>(Is)));
            } else {
                key.append_int64(static_cast<std::int64_t>(stmt->extract_int64(static_cast<int>(Is))));
            }
        }

      public:
```

Update `make_relation_descriptor` (`:1156-1158`):

```cpp
                .extract_q2_owner_pk_fn = +[](ErasedStatementPtr stmt) -> storm::orm::utilities::StitchKey {
                    return JS::extract_q2_owner_pk(static_cast<typename ConnType::Statement*>(stmt));
                },
```

Update `append_in_subquery_open`/`in_subquery_open_size` (`:179-189`) to branch on `Base::has_composite_pk_`:

```cpp
    template <typename Base> consteval auto append_in_subquery_open(auto& result) -> void {
        result.append(" IN (SELECT ");
        if constexpr (Base::has_composite_pk_) {
            result.append("(");
            bool first = true;
            for (const auto& m : Base::primary_key_members_) {
                if (!first) result.append(", ");
                result.append(std::meta::identifier_of(m));
                first = false;
            }
            result.append(")");
        } else {
            result.append(Base::pk_name_);
        }
        result.append(" FROM ");
        result.append(Base::table_name_);
    }

    template <typename Base> consteval auto in_subquery_open_size() -> std::size_t {
        std::size_t pk_part = 0;
        if constexpr (Base::has_composite_pk_) {
            pk_part = 2; // "()"
            for (const auto& m : Base::primary_key_members_) {
                pk_part += std::meta::identifier_of(m).size() + 2; // ", "
            }
        } else {
            pk_part = Base::pk_name_.size();
        }
        return 12 + pk_part + 6 + Base::table_name_.size();
    }
```

Update `append_q2_select_head` (`:219-229`) to accept the full PK member list instead of a single `owner_key` string, branching the same way (composite emits a comma-joined list, single-column keeps the exact old `t2.<key>[_id]` form) — thread this through both call sites (`M2MJoinStatement::build_q2_prefix:910` and `ReverseFKJoinStatement::build_q2_prefix:1096`), and shift `insert_related<1>`/`insert_owner`'s hardcoded column offset `1` to `Base::primary_key_column_count_` at both call sites (`:933-935` and `:1110-1112`, plus the `extract_relation_entity<..., 1>` offset inside `insert_related`/`insert_owner`).

In `src/orm/statements/select.cppm`, change `by_pk` and the key-building loop (`:660-664`):

```cpp
            plf::hive<T>                                              results = std::move(*q1);
            std::unordered_map<storm::orm::utilities::StitchKey, T*> by_pk;
            by_pk.reserve(results.size());
            for (T& obj : results) {
                storm::orm::utilities::StitchKey key;
                constexpr auto pk_members = Base::primary_key_members_;
                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    (append_obj_pk_part_to_key<Is>(key, obj, pk_members), ...);
                }(std::make_index_sequence<pk_members.size()>{});
                by_pk.emplace(key, &obj);
            }
```

with a private helper mirroring `append_pk_part_to_key` above but reading from the OBJECT (`obj.[:pk_members[Is]:]`) instead of the statement column.

Update `run_q2_stitch`'s parameter type (`:717`) from `std::unordered_map<std::int64_t, T*>&` to `std::unordered_map<storm::orm::utilities::StitchKey, T*>&`, and `const std::int64_t owner = ...` (`:726`) to `const auto owner = ...`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeM2MOwnerTest.*" -v`
Expected: PASS — acct-A and acct-B no longer collide.

- [ ] **Step 5: Add + run the reverse-FK composite-owner case**

```cpp
// Append to tests/query/test_composite_fk_join.cpp

template <typename ConnType>
class CompositeReverseFkOwnerTest : public storm::test::StormTestFixture<OrderLineWithShipments, ConnType, Shipment> {};
TYPED_TEST_SUITE(CompositeReverseFkOwnerTest, DatabaseTypes);

TYPED_TEST(CompositeReverseFkOwnerTest, ReverseFkStitchesCorrectlyWithCompositeOwnerKey) {
    storm::QuerySet<OrderLineWithShipments, TypeParam> line_qs;
    OrderLineWithShipments a{.order_id = 1, .product_id = 10, .quantity = 5, .note = "a"};
    OrderLineWithShipments b{.order_id = 1, .product_id = 20, .quantity = 7, .note = "b"}; // same order_id, different product_id
    ASSERT_TRUE(line_qs.insert(a).execute().has_value());
    ASSERT_TRUE(line_qs.insert(b).execute().has_value());

    storm::QuerySet<Shipment, TypeParam> ship_qs;
    OrderLine a_key{.order_id = 1, .product_id = 10};
    ASSERT_TRUE(ship_qs.insert(Shipment{.line = a_key, .carrier = "DHL"}).execute().has_value());

    auto results = line_qs.template join<^^OrderLineWithShipments::shipments>().select();
    ASSERT_TRUE(results.has_value());
    for (const auto& line : *results) {
        if (line.product_id == 10) {
            EXPECT_EQ(line.shipments.size(), 1);
        }
        if (line.product_id == 20) {
            FAIL() << "product_id=20 should have no shipments (dropped by INNER, or empty under LEFT)";
        }
    }
}
```

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="CompositeReverseFkOwnerTest.*" -v`
Expected: PASS.

- [ ] **Step 6: Run full existing suite + fan-out ≥ 10 + single-PK byte-identical regression**

Add a fan-out test (10+ related rows for one composite owner) and a single-PK byte-identical SQL assertion (`Message`/`Person` m2m or reverse-FK, if any exist in-tree — otherwise assert the Q2 prefix SQL text of an existing single-PK m2m case is unchanged from a recorded pre-#504 baseline string).

Run: `ctest --preset ninja-debug --output-on-failure 2>&1 | tail -60`
Expected: 100% pass, zero regressions across the ENTIRE suite (this task touches the hottest, most shared code path in the plan).

- [ ] **Step 7: Commit**

```bash
git add src/orm/statements/join.cppm src/orm/statements/select.cppm tests/query/test_composite_fk_join.cpp
git commit -m "feat(504): StitchKey-based m2m/reverse-FK stitch map for composite owner keys"
```

---

## Task 9: Junction-table DDL for composite PK on either side

**Files:**
- Modify: `src/orm/schema.cppm:977-1027` (`append_junction_fk`, `build_junction_sql`)
- Test: `tests/schema/test_composite_fk_join_sql.cpp` (add DDL text assertions)

**Interfaces:**
- `build_junction_sql<D, Member>` widens from a fixed 2-column junction (`<owner>_id`, `<related>_id`) to N+M columns when either `T` (owner) or the related model has a composite PK — each PK part becomes its own junction column, named `<side>_<part>` (reusing the `fk_column_names_size`/`append_fk_column_names` convention from Task 3, treating the junction's "virtual FK member" as named after the side, e.g. `"ledger"` for the owner side).
- `append_junction_fk` widens its `FOREIGN KEY (<name>_id) REFERENCES <name>(id)` to `FOREIGN KEY (<part1>, <part2>) REFERENCES <name>(<target_part1>, <target_part2>)` for composite sides — this also FIXES the pre-existing hardcoded `(id)` reference (line 987) which was already a latent bug for any non-`id`-named single PK; scope this fix narrowly to the composite branch only, leaving the single-PK branch's exact `(id)` text untouched (a broader fix is out of scope for #504).
- `PRIMARY KEY (<owner>_id, <related>_id)` widens to list every column from both sides.

- [ ] **Step 1: Write the failing test**

```cpp
// Append to tests/schema/test_composite_fk_join_sql.cpp

TEST(JunctionDdlTest, CompositeOwnerSideEmitsOneColumnPerPkPart) {
    const std::string sql =
            std::string(storm::orm::schema::SchemaStatement<LedgerWithTags>::template junction_table_sql<storm::orm::schema::Dialect::SQLite>());
    EXPECT_NE(sql.find("ledgerwithtags_region"), std::string::npos); // exact naming TBD by implementer, assert whatever convention Task 3 settled on
    EXPECT_NE(sql.find("ledgerwithtags_account"), std::string::npos);
    EXPECT_NE(sql.find("ledgerwithtags_period"), std::string::npos);
    EXPECT_NE(sql.find("PRIMARY KEY ("), std::string::npos);
}

TEST(JunctionDdlTest, SinglePkBothSidesJunctionSqlStaysByteIdentical) {
    // Existing in-tree single-PK m2m model (from #203/#392 tests) — assert its
    // junction SQL is UNCHANGED from the pre-#504 baseline. Pick an actual
    // existing m2m model from shared/models.h or the #392 test suite here.
    // (Implementer: grep for an existing `many_to_many<>` field in the test
    // tree, e.g. Student::courses, and assert its junction_table_sql() output
    // against a recorded baseline string captured on develop before this branch.)
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | tail -30`
Expected: FAIL — current DDL emits `ledgerwithtags_id` (singular, wrong — `LedgerWithTags` has no single `id` column at all, so this is actually a compile/runtime SQL error against a nonexistent column, not just a naming mismatch).

- [ ] **Step 3: Write minimal implementation**

Widen `build_junction_sql` (`src/orm/schema.cppm:996-1027`) to branch per side on `BaseStatement<T>::has_composite_pk_` / the related model's equivalent, emitting one column (existing behavior) or N columns (new) per side, with the `PRIMARY KEY (...)` clause listing all of them and the `FOREIGN KEY` clause referencing the target's own `primary_key_members_` identifiers (not the hardcoded `id`) in the composite branch specifically.

Exact byte budget: widen the `ConstexprString<...>` size expression at line 1006 to account for N+M columns instead of a fixed "5× name" budget — compute it from `T`'s and the related model's actual `primary_key_members_` sizes rather than a constant multiplier, to keep the exact-sizing guarantee.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="JunctionDdlTest.*" -v`
Expected: PASS.

- [ ] **Step 5: Live-execute the DDL on both backends (a text assertion isn't enough — SQLite/PG must actually accept the composite junction table)**

Run: `ctest --preset ninja-debug -R "CompositeM2MOwnerTest" --output-on-failure 2>&1 | tail -30`
Expected: PASS (Task 8's tests already exercise table creation for `LedgerWithTags`'s junction; if Task 8 was completed before this task, go back and confirm those tests were passing with the OLD broken junction DDL only by accident, e.g. skipped rather than genuinely exercised — if so, this task's fix is what makes them real).

- [ ] **Step 6: Commit**

```bash
git add src/orm/schema.cppm tests/schema/test_composite_fk_join_sql.cpp
git commit -m "fix(504): junction-table DDL widens to N+M columns when either m2m side has a composite PK"
```

---

## Task 10: Multi-relation `join<^^T::a, ^^T::b>()` regression check

**Files:**
- Test only: `tests/query/test_composite_fk_join.cpp`

**Interfaces:** none new — this task is pure verification that #392's multi-relation join still works when one of the relations involves a composite-PK side, since Tasks 8-9 changed shared machinery (`M2MRelation`, junction DDL) that #392's alias-chaining code (`build_m2m_complete_sql`, `append_complete_join`) depends on.

- [ ] **Step 1: Write the test**

```cpp
// Append to tests/query/test_composite_fk_join.cpp

template <typename ConnType>
class MultiRelationWithCompositeSideTest : public storm::test::StormTestFixture<LedgerWithTags, ConnType, LedgerTag> {};
TYPED_TEST_SUITE(MultiRelationWithCompositeSideTest, DatabaseTypes);

TYPED_TEST(MultiRelationWithCompositeSideTest, SingleCompositeM2MRelationStillWorksAlongsideExistingMultiJoinMachinery) {
    // #392 chains SEVERAL m2m fields via join<^^T::a, ^^T::b>() with alias
    // arithmetic (t2/t3 for relation 0, t4/t5 for relation 1, ...). This test
    // confirms LedgerWithTags::tags (composite owner) still produces correct
    // aliasing when it is the ONLY relation (alias t2/t3, matching the
    // single-relation case exactly) — full multi-relation-WITH-composite-side
    // coverage would need a second m2m field on LedgerWithTags, which is a
    // larger fixture change; scope this task to confirming no alias-arithmetic
    // regression on the existing single-relation path after Tasks 8-9's edits.
    storm::QuerySet<LedgerWithTags, TypeParam> ledger_qs;
    ASSERT_TRUE(ledger_qs.insert(LedgerWithTags{.region = 1, .account = "x", .period = 1, .balance = 1.0}).execute().has_value());
    auto results = ledger_qs.template join<^^LedgerWithTags::tags>().select();
    ASSERT_TRUE(results.has_value());
}
```

- [ ] **Step 2: Run**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter="MultiRelationWithCompositeSideTest.*" -v`
Expected: PASS.

- [ ] **Step 3: Run the FULL pre-existing #392 multi-m2m-join test suite for regressions**

Run: `ctest --preset ninja-debug -R "MultiM2M|392" --output-on-failure 2>&1 | tail -40`
Expected: 100% pass.

- [ ] **Step 4: Commit**

```bash
git add tests/query/test_composite_fk_join.cpp
git commit -m "test(504): confirm #392 multi-relation join machinery unaffected by composite-PK stitch changes"
```

---

## Task 11: Release A/B benchmark — no regression on single-PK JOIN/m2m paths

**Files:**
- Modify: existing JOIN/m2m benchmark file under `benchmarks/` (locate via `grep -rl "JoinType::Inner\|m2m" benchmarks/`)
- No new test file — this is the mandatory perf gate, not a correctness gate.

**Interfaces:** none new — pure verification task per the `benchmark` skill and CLAUDE.md rule 6.

- [ ] **Step 1: Build Release**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release`
Expected: clean build.

- [ ] **Step 2: Run the benchmark skill's interleaved A/B**

Invoke the `benchmark` skill directly (do not hand-roll a benchmark run) comparing this branch's JOIN/m2m benchmark numbers against `develop`, per its interleaving + cv-trust-check method. Target benchmarks: any existing `Storm/JOIN/.*` and `Storm/AGGREGATE.*JOIN.*` (or m2m-specific) filters — identify exact names via:

Run: `./build/release/benchmarks/storm_bench --benchmark_list_tests | grep -i "join\|m2m"`

- [ ] **Step 3: Verify zero regression**

Expected: every single-PK JOIN/m2m benchmark's median is within noise (per the `benchmark` skill's cv-trust threshold) of the `develop` baseline. If ANY shows a uniform-sign slowdown (per the `feedback_uniform_sign_deltas_are_not_noise` memory — a consistent-direction delta below cv is still real), STOP and investigate the specific call site before proceeding — likely candidates: the new `StitchKey` construction cost on the single-PK path (should be exactly one `append_int64` call, inlined) or the new `has_composite_pk_` branch in `append_in_subquery_open`/junction DDL (compile-time branch, should cost nothing at runtime — if it does, the `if constexpr` isn't actually eliminating the composite branch's code from the single-PK instantiation).

- [ ] **Step 4: Record results, no commit needed unless a fix is required**

If a regression is found and fixed, commit that fix separately with its own before/after numbers in the commit message, per the project's benchmark-commit convention (see `project_420_sum_double_truncation.md`-style history for the format).

---

## Task 12: Documentation + issue closure

**Files:**
- Modify: `CLAUDE.md` (add a `**Composite FK + JOIN (#504)**` paragraph in the Supported Field Types section, mirroring the style of the #500/#501/#502 paragraphs already there)
- Modify: `docs/guide/features/JOIN_OPERATIONS.md`, `docs/guide/features/REFERENTIAL_INTEGRITY.md` (or wherever the `storm-docs-writer` agent determines composite FK belongs, per CLAUDE.md's docs-agent guidance)
- Modify: `.claude/agents/*.md` if any agent file describes FK/JOIN behavior that changed

- [ ] **Step 1: Dispatch `storm-docs-writer` agent**

After all prior tasks are merged, dispatch the `storm-docs-writer` agent (per CLAUDE.md's mandate: "code + docs + `.claude/agents/*.md` commit together") summarizing: the `fk<>` annotation now supports composite-PK targets transparently (no new annotation syntax), `<member>_<part>` column naming for composite FK columns (vs. the single-column `<member>_id`), and the stitch-key mechanism for m2m/reverse-FK composite owners.

- [ ] **Step 2: Verify CLAUDE.md's own "JOIN on a composite model is still out of scope (#504)" comment at `base.cppm:861`**

This code comment must be updated/removed now that #504 ships — search for it and any similar "#504" scope-exclusion comments across the codebase and update them to reflect the shipped state.

Run: `grep -rn "#504" src/ CLAUDE.md`

- [ ] **Step 3: Update GitHub issue #504 checkboxes and close**

Per CLAUDE.md's GitHub Issue Workflow: check off every "Definition of done" item genuinely completed, verify each against the actual shipped code (not just this plan's intent), then `gh issue close 504`.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/ .claude/agents/
git commit -m "docs(504): document composite FK + JOIN support"
```

---

## Self-Review Notes (for the plan author, not the implementer)

**Spec coverage against issue #504's Definition of Done:**
1. "Design decision on stitch-key representation recorded before implementation" → done in conversation (StitchKey, fixed-size inline buffer, Task 1).
2. "Composite FK annotation form designed" → resolved as "no new form needed" (Task 5's finding) — `fk<>` unchanged, only `find_fk_primary_key_members` is new.
3. "ValidForeignKey/FKFieldOf accept composite targets, arity mismatch is compile error" → resolved as "no arity mismatch is structurally possible" (Task 5) — documented via test, not new code.
4. "find_fk_primary_key widened" → Task 2 (additive `find_fk_primary_key_members`, existing function untouched).
5. "JOIN ON clauses AND-joined, exact sizers" → Task 7 (regular JOIN), Task 8 (m2m/reverse-FK IN-subquery).
6. "Two-query stitching correct for composite owner keys incl. non-integral part" → Task 8 (StitchKey's `append_string` branch + `Inventory`/`Ledger`-style string-part test coverage via `LedgerWithTags::account`).
7. "Junction-table DDL correct for composite PK either side" → Task 9.
8. "Multi-relation join<a,b>() still correct" → Task 10.
9. "Single-PK byte-identical, no measurable slowdown" → regression assertions embedded in Tasks 2/3/7/8/9's tests + Task 11's dedicated benchmark gate.
10. "Tests: inner+left join, m2m composite owner, reverse-FK composite owner, fan-out≥10, string part, empty relation, cross-backend" → distributed across Tasks 6-10, all `TYPED_TEST`.
11. "Tests written before implementation" → every task's Step 1/2 precede Step 3.

**Known plan risk:** Task 8 is underspecified in exactly one place by design — the precise byte-offset threading through `append_q2_select_head`/`insert_related`/`insert_owner` when the owner-key column count changes from 1 to N is intricate enough that the implementing agent will need to re-derive the exact offsets against the real compiler rather than trust this plan's prose description verbatim. Flag this explicitly to whichever agent executes Task 8: read the current `M2MJoinStatement`/`ReverseFKJoinStatement` bodies fresh immediately before editing, don't rely solely on this plan's paraphrase.

**Known plan risk:** Task 6's note about `schema.cppm`/`FieldNameGrammar` needing a parallel widening is flagged as a "likely nested sub-task" rather than fully specified, because its exact shape depends on what Task 2/3 reveal when actually compiled. This is intentional — forcing it further would have meant guessing at code I have not yet read in `FieldNameGrammar`'s implementation (`storm_orm_statements_field_names` module, not yet opened in this investigation). The Task 6 implementer must open that module first.
