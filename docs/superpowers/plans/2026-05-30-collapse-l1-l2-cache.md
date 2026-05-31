# Collapse L1/L2 Statement Caching (Option C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the L1 (QuerySet statement cache) and L2 (Statement prepared-pointer cache) levels entirely, leaving the single mutex-protected L3 Connection cache, with no behavioral or performance regression.

**Architecture:** Approach A — every result proxy stops holding `Statement&` and instead owns its `Statement` by value; `query(...)` factories move a freshly-constructed statement into the returned proxy, so proxy + statement share one stack lifetime. All `cached_*` pointer/string members, fast-path branches, and the `invalidate_cache()` machinery are deleted across the statement classes; the four `get_*_statement()` lazy accessors and their `unique_ptr` members are deleted from QuerySet and call sites construct a statement inline. The `STORM_DISABLE_L1/L2` flags and the `storm_cache_probe` bench are deleted.

**Tech Stack:** C++26 / clang-p2996, CMake + Ninja, GoogleTest, Google Benchmark, SQLite + PostgreSQL.

**Spec:** `docs/superpowers/specs/2026-05-30-collapse-l1-l2-cache-design.md`

---

## File structure

- `src/orm/statements/select.cppm` — biggest: 6 cached members, 5 fast-path helpers, `invalidate_cache()`, 5 proxies (`Query`/`FirstQuery`/`GetQuery` own-by-value).
- `src/orm/statements/update.cppm` — `cached_update_stmt_`, `ensure_cached_stmt()`, 2 proxies.
- `src/orm/statements/insert.cppm` — `cached_insert_returning_stmt_`, 4 proxies.
- `src/orm/statements/erase.cppm` — `cached_single_stmt_`, `cached_max_bulk_stmt_`, `invalidate_cache()`, 3 proxies.
- `src/orm/statements/distinct.cppm`, `aggregate.cppm` — verify (no `cached_` members found; confirm no `Statement&` proxy / L1 dependence).
- `src/orm/queryset.cppm` — 4 `unique_ptr` members + 4 `get_*_statement()` accessors + 12 call sites + `reset()`/`invalidate_cache()` chain.
- `benchmarks/cache_probe.cpp` (delete), `benchmarks/CMakeLists.txt` (remove target).
- `docs/architecture/STATEMENT_CACHING.md` (rewrite to single-level).

---

### Task 1: Branch setup

**Files:** none (git)

- [ ] **Step 1: Create the collapse branch off develop**

The investigation branch (`feature/214-cache-investigation`) holds the spec, plan, results, and the
`#ifdef` scaffolding. The collapse is new work; branch off the current branch so it inherits the spec
+ plan, then it will be rebased/PR'd onto develop.

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && git checkout -b feature/214-collapse-l1-l2
```
Expected: switched to a new branch `feature/214-collapse-l1-l2`.

- [ ] **Step 2: Verify baseline is green before any change**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -5 && ctest --preset ninja-debug 2>&1 | tail -15
```
Expected: build clean, all tests pass (the suite is the behavioral spec for this refactor). If
anything fails here it is a pre-existing issue — STOP and report; do not start the refactor on a red
baseline.

---

### Task 2: Collapse L2 in select.cppm

**Files:**
- Modify: `src/orm/statements/select.cppm`

This is the largest task. SelectStatement currently has the `#ifdef STORM_DISABLE_L2` guards from the
investigation — they make the disabled path `if (true)`. The collapse makes that the *only* path and
deletes the cached members. **Keep the `#ifdef` blocks for now** (they are removed wholesale in
Task 8); this task removes the cached-member *reads/writes* and converts the proxies to own-by-value.

The proxies hold `SelectStatement& stmt;` (`QueryBase` at ~line 151). Approach A changes this to
`SelectStatement stmt;` by value, and the `query`/`make_first_or_get` factories move `*this` in.

- [ ] **Step 1: Read the current proxy + factory + cached members**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && sed -n '146,262p;425,520p;625,665p' src/orm/statements/select.cppm
```
Confirm the structures match what this task edits (QueryBase `SelectStatement& stmt`; `query()`
returns `{*this, …}`; `make_first_or_get` returns `{{*this, …}, fast}`; the cached members at the
bottom; `invalidate_cache()`; the fast-path helpers `prepare_simple_path`, `rebind_where_only`,
`can_use_addr_fast_path`, `prepare_and_bind`, `prepare_statement`).

- [ ] **Step 2: Convert `QueryBase` to own the statement by value**

Change the member declaration in `QueryBase` (~line 151) from:
```cpp
        struct QueryBase {
            SelectStatement&                    stmt;
```
to:
```cpp
        struct QueryBase {
            SelectStatement                     stmt;
```
(All three proxies — `Query`, `FirstQuery`, `GetQuery` — derive from `QueryBase`, so they inherit the
by-value `stmt`. Their method bodies use `this->stmt.foo()` and need no change.)

- [ ] **Step 3: Move `*this` into the proxy in the factories**

`query()` (~226):
```cpp
        auto query(...) -> Query {
            return {std::move(*this), jw, we, lv, ov, ob};
        }
```
`make_first_or_get` (~242):
```cpp
            return {{std::move(*this), std::move(jw), we, lv, ov, ob}, fast};
```
(Keep the parameter lists exactly as they are; only `*this` → `std::move(*this)`.)

- [ ] **Step 4: Delete the cached-member reads in the fast paths**

The fast-path helpers exist only to reuse `cached_*` pointers. With L2 gone they reduce to a plain
`prepare_cached → (bind) → return`. Replace the bodies:

`prepare_simple_path()` (~440) becomes:
```cpp
        [[nodiscard]] __attribute__((always_inline)) auto prepare_simple_path() -> std::expected<Statement*, Error> {
            return conn_->prepare_cached(get_select_sql());
        }
```

`prepare_and_bind(...)` (~460): drop the `cached_stmt_/cached_sql_` guard and the address-cache write;
it becomes a straight prepare-then-bind:
```cpp
        [[nodiscard]] __attribute__((always_inline)) auto
        prepare_and_bind(std::string sql, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<Statement*, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(Error{-1, "Failed to prepare statement"});
            }
            Statement* stmt = *prepare_result;
            if (where_expr) {
                if (auto r = Base::template bind_where_params<Statement, Error>(stmt, where_expr); !r) {
                    return std::unexpected(r.error());
                }
            }
            return stmt;
        }
```
> NOTE: confirm the exact spelling of the WHERE-bind call by reading the current `prepare_and_bind`
> and `bind_where_or_propagate` (the original calls a `bind_where_or_propagate(where_expr)` helper).
> Reuse whatever helper the original used to bind — do not invent a new binding call. The point of
> this step is only to remove the `cached_stmt_ == nullptr || cached_sql_ != sql` short-circuit and
> the `cached_sql_ = std::move(sql)` write, keeping the prepare+bind behavior identical.

`prepare_statement(...)` (~499): remove the `can_use_addr_fast_path` / `rebind_where_only` branch and
the `cached_where_addr_ = where_addr` write, leaving:
```cpp
        [[nodiscard]] __attribute__((always_inline)) auto prepare_statement(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> std::expected<Statement*, Error> {
            if (is_simple_select(join_wrapper, where_expr, limit, offset, order_by_wrapper)) {
                return prepare_simple_path();
            }
            return prepare_and_bind(build_sql(join_wrapper, where_expr, limit, offset, order_by_wrapper), where_expr);
        }
```

`execute_one_fast()` (~288) and `execute_get_fast()` (~328): drop the cached-pointer guard, prepare
locally each call:
```cpp
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_one_fast()
                -> std::expected<std::optional<T>, Error> {
            auto prepare_result = conn_->prepare_cached(select_limit1_sql_string);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            return execute_single_row(*prepare_result, [](Statement* stmt, T& obj) {
                Base::extract_all_columns(stmt, obj);
            });
        }
```
```cpp
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_get_fast() -> std::expected<T, Error> {
            auto prepare_result = conn_->prepare_cached(select_limit2_sql_string);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            return execute_exact_one(*prepare_result, [](Statement* stmt, T& obj) {
                Base::extract_all_columns(stmt, obj);
            });
        }
```

- [ ] **Step 5: Delete `rebind_where_only`, `can_use_addr_fast_path`, `invalidate_cache`, and the cached members**

Delete the entire `rebind_where_only(...)` method (~445), the entire `can_use_addr_fast_path(...)`
method (~494), the entire `invalidate_cache()` method (~254), and the six cached member declarations
at the bottom (~651-662): `cached_simple_stmt_`, `cached_first_stmt_`, `cached_get_stmt_`,
`cached_stmt_`, `cached_where_addr_`, `cached_sql_`.

> NOTE: `invalidate_cache()` is called from `QuerySet::reset()`/`invalidate_cache()` (queryset.cppm).
> Removing it here will break those callers — that is expected and is fixed in Task 7 (L1). To keep
> the suite green *per task*, this task must keep compiling: since `get_select_statement()` still
> exists in Task 2's world (L1 not yet removed) and QuerySet still calls the statement's
> `invalidate_cache()`, you have two options — (a) leave a no-op `invalidate_cache() noexcept {}` stub
> in SelectStatement for now and delete it in Task 7, or (b) do the QuerySet caller cleanup in this
> task. **Choose (a): leave an empty `auto invalidate_cache() noexcept -> void {}` stub** so Task 2 is
> self-contained and green; Task 7 removes the stub and its callers together. Replace the body of
> `invalidate_cache()` with empty rather than deleting the method in this task.

- [ ] **Step 6: Build + test**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -15 && ctest --preset ninja-debug 2>&1 | tail -15
```
Expected: clean build, all tests pass. If "malformed or corrupted precompiled file" → rebuild once
with `-j1`. If a move-constructor error appears (SelectStatement not movable), read the class for a
deleted/odd special member and report — do not force a fix that changes semantics.

- [ ] **Step 7: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/statements/select.cppm && git commit -m "refactor(cache): collapse L2 in SelectStatement — proxy owns statement by value"
```

---

### Task 3: Collapse L2 in update.cppm

**Files:**
- Modify: `src/orm/statements/update.cppm`

UpdateStatement has `cached_update_stmt_` + `ensure_cached_stmt()` (~193) and 2 proxies (`SingleQuery`,
`BulkQuery`) holding `UpdateStatement& stmt`.

- [ ] **Step 1: Read the relevant regions**

Run: `cd /home/ihor/projects/storm/storm_develop && sed -n '125,310p' src/orm/statements/update.cppm`
Confirm: `SingleQuery`/`BulkQuery` hold `UpdateStatement& stmt`; `query()` returns `{{}, *this, obj}` /
`{{}, *this, objects}`; `ensure_cached_stmt()` and `cached_update_stmt_` exist; the
`execute_single_optimized` path uses `cached_update_stmt_`.

- [ ] **Step 2: Convert proxies to own by value**

`SingleQuery` (~139) and `BulkQuery` (~150): change `UpdateStatement&   stmt;` → `UpdateStatement   stmt;`.

- [ ] **Step 3: Move into the proxy in the factories**

`query(const T& obj …)` (~161): `return {{}, std::move(*this), obj};`
`query(std::span<const T> objects …)` (~164): `return {{}, std::move(*this), objects};`

- [ ] **Step 4: Replace `ensure_cached_stmt()` + cached uses with per-call prepare**

`ensure_cached_stmt()` (~193) exists to lazily fill `cached_update_stmt_`. Inline a local prepare into
the methods that called it (`execute_single_optimized` ~285 and the other caller ~228). Each callsite
that did `if (auto ready = ensure_cached_stmt(); !ready) return …; … cached_update_stmt_->…` becomes:
```cpp
            auto prepare_result = conn_->prepare_cached(get_update_sql());
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt = *prepare_result;
            stmt->reset();
            auto bind_result = inline_bind_all_fields(stmt, obj, typename Base::field_indices_t{});
            // … then the existing execute/reset logic, using `stmt` instead of `cached_update_stmt_`
```
> NOTE: read the two current callers of `ensure_cached_stmt()` and reproduce their exact
> bind/execute/reset sequence, substituting a local `stmt` for `cached_update_stmt_`. Do not change
> the bind helper (`inline_bind_all_fields`) or the reset ordering — only remove the caching.

- [ ] **Step 5: Delete `ensure_cached_stmt()` and `cached_update_stmt_`**

Remove the `ensure_cached_stmt()` method and the `cached_update_stmt_` member declaration.

- [ ] **Step 6: Build + test**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -15 && ctest --preset ninja-debug --output-on-failure -R "Update|update" 2>&1 | tail -20 && ctest --preset ninja-debug 2>&1 | tail -8
```
Expected: clean build, update tests pass, full suite passes.

- [ ] **Step 7: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/statements/update.cppm && git commit -m "refactor(cache): collapse L2 in UpdateStatement — proxy owns statement by value"
```

---

### Task 4: Collapse L2 in insert.cppm

**Files:**
- Modify: `src/orm/statements/insert.cppm`

InsertStatement has `cached_insert_returning_stmt_` (~380) and 4 proxies (`SingleQuery`, `VoidQuery`,
`BulkQuery`, `BulkReturningQuery`) holding `InsertStatement& stmt`.

- [ ] **Step 1: Read the regions**

Run: `cd /home/ihor/projects/storm/storm_develop && sed -n '215,420p' src/orm/statements/insert.cppm`
Confirm the 4 proxies hold `InsertStatement& stmt`, the `query<R>(...)` factories return
`{*this, …}`-shaped aggregates, and `cached_insert_returning_stmt_` is used in the RETURNING path
(~380-405).

- [ ] **Step 2: Convert all 4 proxies to own by value**

Change `InsertStatement&   stmt;` / `InsertStatement&             stmt;` → `InsertStatement   stmt;` in
`SingleQuery` (~221), `VoidQuery` (~235), `BulkQuery` (~249), `BulkReturningQuery` (~264). Keep the
`// NOSONAR(cpp:S1659)` comment on `BulkReturningQuery` if it remains an aggregate.

- [ ] **Step 3: Move into the proxies in the `query<R>` factories**

In each `query<R>(...)` factory (~279 onward), change the returned aggregate's first element from
`*this` to `std::move(*this)`. Read each factory and apply the move; the brace shapes differ per proxy
so match each one's existing initializer.

- [ ] **Step 4: Replace `cached_insert_returning_stmt_` with a per-call local**

In the RETURNING execution path (~378-405), replace the `if (cached_insert_returning_stmt_ == nullptr)
{ … } … cached_insert_returning_stmt_->…` block with a local prepare:
```cpp
            auto stmt_result = conn_->prepare_cached(insert_returning_sql_string);
            if (!stmt_result) [[unlikely]] {
                return std::unexpected(stmt_result.error());
            }
            Statement* stmt = *stmt_result;
            // … reproduce the existing bind/step/extract/reset sequence using `stmt`
            //     in place of cached_insert_returning_stmt_
```
> NOTE: read the exact current sequence (bind_all_fields, step_raw, extract_int64, reset) and
> substitute `stmt` for `cached_insert_returning_stmt_` verbatim.

- [ ] **Step 5: Delete `cached_insert_returning_stmt_`**

Remove the member declaration.

- [ ] **Step 6: Build + test**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -15 && ctest --preset ninja-debug --output-on-failure -R "Insert|insert" 2>&1 | tail -20 && ctest --preset ninja-debug 2>&1 | tail -8
```
Expected: clean build, insert tests pass, full suite passes.

- [ ] **Step 7: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/statements/insert.cppm && git commit -m "refactor(cache): collapse L2 in InsertStatement — proxy owns statement by value"
```

---

### Task 5: Collapse L2 in erase.cppm

**Files:**
- Modify: `src/orm/statements/erase.cppm`

EraseStatement has `cached_single_stmt_`, `cached_max_bulk_stmt_`, `invalidate_cache()` (~427), and 3
proxies holding `EraseStatement& stmt`.

- [ ] **Step 1: Read the regions**

Run: `cd /home/ihor/projects/storm/storm_develop && sed -n '175,300p;300,440p' src/orm/statements/erase.cppm`
Confirm: 3 proxies at ~180/194/208 hold `EraseStatement& stmt`; their `query()` factories; the
single-erase path uses `cached_single_stmt_` (~303-318); the bulk path uses `cached_max_bulk_stmt_`
(~347-373); `invalidate_cache()` at ~427 nulls both.

- [ ] **Step 2: Convert the 3 proxies to own by value**

Change `EraseStatement&    stmt;` → `EraseStatement    stmt;` in all three proxy structs.

- [ ] **Step 3: Move into the proxies in the factories**

In each `query(...)` factory, change `*this` → `std::move(*this)` in the returned initializer.

- [ ] **Step 4: Replace both cached pointers with per-call locals**

Single path (~303-318): replace `if (cached_single_stmt_ == nullptr) { … } cached_single_stmt_->…`
with a local prepare from the same SQL, reproducing the existing reset/bind_pk_at/execute sequence on
a local `Statement* stmt`. Bulk path (~347-373): same — local prepare, reproduce the chunk loop's
`bind_pks_and_execute(*stmt, chunk)` calls on a local.

> NOTE: read each path's exact bind/execute sequence and substitute a local for the cached pointer.
> The bulk path prepares once and reuses across chunks within one call — keep that (prepare the local
> once before the chunk loop, reuse it for all chunks of THIS call). Do not change chunking logic.

- [ ] **Step 5: Delete `cached_single_stmt_`, `cached_max_bulk_stmt_`, and replace `invalidate_cache()` with an empty stub**

Delete the two member declarations. Replace `invalidate_cache()`'s body with empty:
`auto invalidate_cache() noexcept -> void {}` (kept as a stub for the same reason as SelectStatement —
QuerySet still calls it until Task 7).

- [ ] **Step 6: Build + test**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -15 && ctest --preset ninja-debug --output-on-failure -R "Erase|erase|Delete|delete" 2>&1 | tail -20 && ctest --preset ninja-debug 2>&1 | tail -8
```
Expected: clean build, erase/delete tests pass, full suite passes.

- [ ] **Step 7: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/statements/erase.cppm && git commit -m "refactor(cache): collapse L2 in EraseStatement — proxy owns statement by value"
```

---

### Task 6: Verify distinct.cppm and aggregate.cppm

**Files:**
- Modify (if needed): `src/orm/statements/distinct.cppm`, `src/orm/statements/aggregate.cppm`

Grep found no `cached_*` members in these two. But they may hold `Statement&` proxies or be
constructed via QuerySet in a way that breaks when L1 goes (Task 7). This task confirms they need no
L2 change and are ready for the L1 transition.

- [ ] **Step 1: Audit both files for L2/proxy patterns**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && grep -n "cached_\|& stmt\|Statement&\|struct.*Query\| stmt;\|invalidate_cache\|prepare_cached" src/orm/statements/distinct.cppm src/orm/statements/aggregate.cppm
```
Expected: no `cached_*` members. If a proxy holds `Statement&` (or `DistinctStatement&`/
`AggregateStatement&`), apply the same Approach-A conversion as Tasks 2-5 (by-value + `std::move(*this)`
in the factory) and rebuild+test. If there are no such proxies (these statements may execute directly
without a returned proxy), this task is a no-op verification.

- [ ] **Step 2: Build + test (only if changes were made; otherwise just confirm baseline)**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -10 && ctest --preset ninja-debug --output-on-failure -R "Distinct|distinct|Aggregate|aggregate|Count|Sum|Avg" 2>&1 | tail -20
```
Expected: pass.

- [ ] **Step 3: Commit (only if changes were made)**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/statements/distinct.cppm src/orm/statements/aggregate.cppm && git commit -m "refactor(cache): collapse L2 proxies in Distinct/Aggregate statements"
```
If no changes were needed, skip the commit and note "Task 6: no L2 in distinct/aggregate, verified" in
the report.

---

### Task 7: Collapse L1 in queryset.cppm

**Files:**
- Modify: `src/orm/queryset.cppm`

Remove the four `get_*_statement()` accessors + `unique_ptr` members, rewrite the 12 call sites to
construct a statement inline, and remove the now-dead `invalidate_cache()` chain (the statement-level
stubs from Tasks 2/5 get deleted here too).

- [ ] **Step 1: Read the call sites, accessors, members, and reset/invalidate chain**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && sed -n '55,100p;154,195p;270,315p;525,610p' src/orm/queryset.cppm
```
Confirm the 12 call sites (erase ~60/65/70, insert ~80/89/95, select ~158/167/177/189, update
~277/282), the four accessors (~526-572), the four `unique_ptr` members (~603-606), and the
`reset()`/`invalidate_cache()` methods (~297-310).

- [ ] **Step 2: Rewrite the 12 call sites to construct inline**

Replace each `get_X_statement()` with a constructed temporary. The statement constructors take
`conn_` (a `std::shared_ptr<ConnType>`). Examples:

`select()` (~157):
```cpp
        [[nodiscard]] __attribute__((hot)) auto select() {
            return orm::statements::SelectStatement<T, ConnType>(conn_)
                    .query(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_);
        }
```
Apply the same pattern to `rows()` (~166, `.rows_generator(...)`), `first()` (~174, `.query_first(...)`),
`get()` (~186, `.query_get(...)`), the three `get_insert_statement()` calls (~80/89/95,
`.template query<R>(...)` / `.query(...)`), the three `get_erase_statement()` calls (~60/65/70,
`.query(obj)` / `.query(objects)` / `.query_all()`), and the two `get_update_statement()` calls
(~277/282, `.query(obj)` / `.query(objects)`). Each becomes
`orm::statements::XStatement<T, ConnType>(conn_).method(...)`.

> NOTE: read each call site and preserve its exact method name and arguments — only replace the
> `get_X_statement()` receiver with a freshly-constructed `XStatement<T, ConnType>(conn_)`. The
> `first()`/`get()` `fast` bool computation (lines ~175-176, ~187-188) stays.

- [ ] **Step 3: Delete the four accessors and four members**

Delete `get_insert_statement()`, `get_erase_statement()`, `get_update_statement()`,
`get_select_statement()` (~526-572, including their `#ifdef STORM_DISABLE_L1` guards) and the four
`mutable std::unique_ptr<…Statement> *_stmt_;` members (~603-606). Leave `join_stmt_` (~608) — it is
query state, not a statement cache.

- [ ] **Step 4: Remove the dead invalidate_cache chain (DECIDED: delete the L1 API + its tests)**

`QuerySet::reset()` (~297) calls `invalidate_cache()` (~304) which forwarded to the statement-level
`invalidate_cache()`. With no cached statements on the QuerySet, there is nothing to invalidate. The
user has decided: **delete the QuerySet→L2 invalidation API entirely.**

- `reset()` keeps ONLY the query-state clears (`join_stmt_.reset()`, `where_expr_.reset()`,
  `limit_value_.reset()`, `offset_value_.reset()`, `order_by_wrapper_.reset()`) and drops its
  `invalidate_cache()` call.
- Delete `QuerySet::invalidate_cache()` entirely.
- **`Connection::clear_statement_cache()` and `clear_statement_cache(table)` STAY** — they clear L3,
  which is still valid and still has its own tests (`CacheInvalidationLevel3Test`). Do NOT touch them.
- **Delete the `CacheInvalidationLevel1Test` suite** in `tests/db/test_cache_invalidation.cpp` — it
  asserts L1/L2 pointer invalidation (`qs.invalidate_cache()` propagation, `reset()` clearing Level 2
  caches), behavior that no longer exists. Remove only the `CacheInvalidationLevel1Test` TYPED_TEST
  suite and its fixture; **keep `CacheInvalidationLevel3Test`** (PointersStableAcrossRehash,
  PerTableClearKeepsUnrelatedEntries, PerTableClearRespectsWordBoundaries) and the
  `SqlReferencesTableFastOut` test — those test L3 and stay.
- Also grep for any OTHER caller of `qs.invalidate_cache()`:
  ```bash
  cd /home/ihor/projects/storm/storm_develop && grep -rn "\.invalidate_cache()\|->invalidate_cache()" src/ tests/ benchmarks/
  ```
  Every hit outside the deleted Level1 suite must be removed (there should be none in src/ once the
  method is gone). Report what you found.

- [ ] **Step 5: Delete the statement-level invalidate_cache stubs**

Now that QuerySet no longer forwards to them, delete the empty `invalidate_cache()` stubs left in
`select.cppm` (Task 2 Step 5) and `erase.cppm` (Task 5 Step 5).

- [ ] **Step 6: Build + test**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug 2>&1 | tail -15
```
Expected: clean build, full suite passes. This is the task most likely to surface a dangling-proxy or
movability problem — if any test crashes or fails, that is the Approach-A lifetime contract being
violated; report the failing test and the exact symptom.

- [ ] **Step 7: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add src/orm/queryset.cppm src/orm/statements/select.cppm src/orm/statements/erase.cppm && git commit -m "refactor(cache): collapse L1 — QuerySet constructs statements per call, no cache members"
```

---

### Task 8: Remove the STORM_DISABLE_L1/L2 scaffolding and the probe

**Files:**
- Modify: `src/orm/statements/select.cppm`, `src/orm/queryset.cppm` (remove leftover `#ifdef` blocks)
- Delete: `benchmarks/cache_probe.cpp`
- Modify: `benchmarks/CMakeLists.txt` (remove `storm_cache_probe` target)
- Modify: `.lint-skip` (remove the select.cppm `complexity, length` tags added in the investigation, if no longer needed)

By now most `#ifdef STORM_DISABLE_L2` blocks were rewritten away in Tasks 2-5, and the
`STORM_DISABLE_L1` guards were deleted with the accessors in Task 7. This task removes any residual
guard text and the bench scaffolding.

- [ ] **Step 1: Find any residual flag references**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && grep -rn "STORM_DISABLE_L1\|STORM_DISABLE_L2\|cache_probe\|storm_cache_probe" src/ benchmarks/ .lint-skip
```
Expected: ideally only `benchmarks/cache_probe.cpp`, the `storm_cache_probe` block in
`benchmarks/CMakeLists.txt`, and possibly stray `#ifdef` lines in src. Remove every src-side guard so
no `STORM_DISABLE_*` token remains in `src/`.

- [ ] **Step 2: Delete the bench source and CMake target**

```bash
cd /home/ihor/projects/storm/storm_develop && git rm benchmarks/cache_probe.cpp
```
Then remove the `storm_cache_probe` `add_executable(...)` block and its associated
`target_link_libraries`/`link_sqlite`/per-source property lines from `benchmarks/CMakeLists.txt`
(the block added in the investigation Task 5).

- [ ] **Step 3: Re-check .lint-skip**

The investigation added `complexity, length` to the `select.cppm` `.lint-skip` entry because the
`#ifdef` blocks confused lizard. With the guards gone and the file simpler, those tags may no longer
be needed. Read `.lint-skip`; if the original entry had only `file-size`, restore it to that. If
removing the tags causes the file-size/complexity hook to fire on commit, keep whatever the current
file genuinely needs and report.

- [ ] **Step 4: Configure + build (default) to confirm nothing references the removed target**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --preset ninja-release 2>&1 | tail -10 && cmake --build --preset ninja-release 2>&1 | tail -10
```
Expected: configures without the `storm_cache_probe` target, builds clean. (Use ninja-release here
because Task 9 benchmarks from it.)

- [ ] **Step 5: Commit**

```bash
cd /home/ihor/projects/storm/storm_develop && git add -A && git commit -m "chore(cache): remove STORM_DISABLE_L1/L2 scaffolding and storm_cache_probe"
```

---

### Task 9: Full gate run (sanitizers + Core 8 benchmark)

**Files:** none (verification)

Per the spec's required gates. The object-lifetime change makes ASAN/TSAN load-bearing here.

- [ ] **Step 1: ASAN+UBSAN**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan 2>&1 | tail -8 && timeout 1200 ctest --preset ninja-asan-ubsan 2>&1 | tail -20
```
Expected: all tests pass, no ASAN/UBSAN reports. (ASAN runs are 4-5× slower — the timeout is
generous. A use-after-free here would mean a proxy outlived its statement: report it.)

- [ ] **Step 2: TSAN**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --preset ninja-tsan && cmake --build --preset ninja-tsan 2>&1 | tail -8 && timeout 1200 ctest --preset ninja-tsan 2>&1 | tail -20
```
Expected: all tests pass, no data races (the #271 concurrent cache tests run here — they must stay
clean, proving the L3 shared_mutex is still the only shared mutable state).

- [ ] **Step 3: Core 8 Release benchmark**

The `#ifdef` bypass numbers do not transfer to the real deletion (struct layout/inlining shift), so
re-measure. Run the project's standard regression filter from the release build (Task 8 built it):
```bash
cd /home/ihor/projects/storm/storm_develop && ./build/release/benchmarks/storm_bench --benchmark_repetitions=10 --benchmark_report_aggregates_only=true 2>&1 | tail -40
```
> NOTE: read `benchmarks/README.md` / `CLAUDE.md` for the exact "Core 8" filter string if a subset is
> wanted; the full run is acceptable. Compare medians against develop's published baseline (or run the
> same bench on develop in a separate build dir). Expected: within ±5%. If any category regresses
> >5%, STOP and report — the collapse would need revisiting (the investigation predicted no
> regression, so a >5% drop is a real finding).

- [ ] **Step 4: Coverage**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && cmake --preset ninja-debug && cmake --build --preset ninja-debug && cmake --build --preset ninja-debug-coverage --target coverage 2>&1 | tail -25
```
Expected: 100% (or matching develop's baseline). Deleted code reduces total lines; ensure no newly
*uncovered* lines were introduced. If coverage drops, identify the uncovered lines (likely an error
branch in the new per-call prepare) and add a mock-error test, or report.

- [ ] **Step 5: Record gate results**

No commit (verification task). Record in the execution report: ASAN/UBSAN result, TSAN result, Core 8
median deltas vs baseline, coverage %. These feed Task 10's docs.

---

### Task 10: Update docs and rewrite #273

**Files:**
- Modify: `docs/architecture/STATEMENT_CACHING.md`
- Modify: `.claude/agents/*.md` if any describe the 3-level cache (per CLAUDE.md rule #8)
- GitHub: rewrite issue #273

- [ ] **Step 1: Rewrite STATEMENT_CACHING.md to single-level**

Read the current doc, then rewrite so it describes a single L3 Connection-level cache (mutex-protected,
#271). Remove the L1 (QuerySet) and L2 (Statement pointer) sections and the "LRU eviction (future
enhancement)" note's dependence on L2 invalidation. State that statements are constructed per call and
owned by the result proxy (Approach A), and that L3 carries all caching. Cite the #214 investigation
results doc for the "no measurable benefit" evidence.

- [ ] **Step 2: Check agent files for stale cache descriptions**

Run:
```bash
cd /home/ihor/projects/storm/storm_develop && grep -rln "3-level\|three-level\|Level 1\|Level 2\|cached_stmt\|get_select_statement\|statement cache" .claude/agents/
```
Update any agent file that describes the old 3-level architecture to the single-level model. (Per
CLAUDE.md rule #8: code + docs + agent files commit together.)

- [ ] **Step 3: Commit docs**

```bash
cd /home/ihor/projects/storm/storm_develop && git add docs/architecture/STATEMENT_CACHING.md .claude/agents/ && git commit -m "docs(cache): document single-level L3 cache after L1/L2 collapse (#214)"
```

- [ ] **Step 4: Rewrite issue #273**

With L2 gone, #273's "L2 pointer invalidation on eviction is automatic" requirement no longer applies.
Rewrite the body so it covers only: LRU eviction on the L3 `unordered_map` + hit/miss/eviction stats +
configurable capacity + memory-growth test. Remove the L2-dangling-pointer section and the "automatic
callback path" bullet. Use:
```bash
cd /home/ihor/projects/storm/storm_develop && gh issue view 273 --json body -q .body   # read current
# then craft the new body and:
gh issue edit 273 --body "$(cat /tmp/273_new_body.md)"
```
> NOTE: do not close #273; just rewrite its scope. Note in a comment that #214 concluded Option C and
> the L2-invalidation requirement is obsolete.

- [ ] **Step 5: Report completion**

Summarize: all tasks done, gate results (from Task 9), and that #273 is rewritten. Stop — do NOT open
a PR or merge without explicit user approval (CLAUDE.md safety rule #2).

---

## Notes for the implementer

- **Tests green at every task** — this is a refactor of fully-covered behavior; a red suite mid-task
  means the collapse changed observable behavior, which it must not.
- **The lifetime contract is the whole risk** (Approach A): a proxy must never outlive its statement.
  Since proxies own the statement by value, the only way to break this is to return a reference to a
  proxy's internal statement, or to store a proxy beyond the full-expression. The existing API uses
  `qs.select().execute()` (single full-expression), which is safe. ASAN in Task 9 is the backstop.
- **Movability**: each statement holds only `shared_ptr<ConnType> conn_` + compile-time constants after
  L2 removal, so implicit moves work. If a class declared a destructor or copy member that suppresses
  the implicit move, add `= default` moves explicitly and report.
- **PCM hazards**: "malformed or corrupted precompiled file" on a module build → rebuild once with
  `-j1`, then resume.
- **No PR/merge** without user approval. The plan ends at "report completion."
