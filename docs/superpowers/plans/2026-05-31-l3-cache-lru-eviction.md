# L3 Statement Cache — LRU Eviction + Statistics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add CLOCK/second-chance LRU eviction + hit/miss/eviction/size statistics to the single Connection-level statement cache, configurable via a capacity (default 512, `0` = unbounded), on both SQLite and PostgreSQL backends.

**Architecture:** Bundle the cache map + mutex + atomic counters + capacity into one shared `StatementCacheState<Statement>` struct (in `storm_db_concept`) that each `Connection` embeds, replacing the current separate `statement_cache_` + `cache_mutex_` members. All eviction/stats logic lives in the shared `storm::db::cache_*` helpers so both backends share one implementation. Hit only flips a per-entry atomic ref bit under the existing `shared_lock`; eviction (a clock sweep) runs inside `cache_try_insert` under the `unique_lock` it already takes.

**Tech Stack:** C++26, clang-p2996, `std::unordered_map`, `std::atomic`, `std::shared_mutex`, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-05-31-l3-cache-lru-eviction-design.md`

---

## File Structure

- **Modify** `src/db/concept.cppm` — add `CacheEntry`, `StatementCacheState`, `CacheStats`; rewrite `cache_find_hit` / `cache_try_insert` / `cache_clear_all` / `cache_clear_table` / `cache_count` to take `StatementCacheState&`; add `cache_stats` helper; add `cache_stats()` to the `CachedDatabaseConnection` concept.
- **Modify** `src/db/sqlite.cppm` — replace the `StatementValue`/`StatementCache`/`statement_cache_`/`cache_mutex_` machinery with one `StatementCacheState<Statement> cache_`; add a `Config` struct + `open(path, Config)`; add `cache_stats()`; set `cache_.capacity` in the ctor.
- **Modify** `src/db/postgresql_connection.cppm` — same shape as sqlite.cppm (different prepare step).
- **Modify** `src/db/pool.cppm` — add `statement_cache_capacity` to `PoolConfig`; thread it through both `ConnType::open(conninfo_)` calls.
- **Modify** `tests/db/test_cache_invalidation.cpp` — add eviction + stats + memory-growth + unbounded + clear-preserves-counters tests (SQLite-direct, matching the existing file's style).
- **Modify** `docs/architecture/STATEMENT_CACHING.md` — replace unbounded-growth note with the eviction policy.

> **No CMake changes** — `src/` and `tests/` use GLOB auto-discovery; no files are added.

---

## Task 1: Shared cache types — `CacheEntry`, `StatementCacheState`, `CacheStats`

**Files:**
- Modify: `src/db/concept.cppm` (insert after `string_equal`, before the `MovableSharedMutex` class at line ~130, OR right after `MovableSharedMutex` — must come before the `cache_*` helpers)

- [ ] **Step 1: Add the three types in `storm::db`**

Insert immediately after the `MovableSharedMutex` class (after its closing `};`, currently line 165), before the `is_sql_ident_char` predicate:

```cpp
    // Issue #273: CLOCK/second-chance LRU eviction + statistics on the single L3
    // statement cache. Each entry carries a reference bit set on a cache hit;
    // eviction sweeps the entries, clearing set bits (second chance) and evicting
    // the first unreferenced one. The hit path only flips an atomic bit under the
    // shared_lock — no structural mutation, so the #271 parallel-read hot path is
    // preserved.
    template <typename Stmt> struct CacheEntry {
        std::unique_ptr<Stmt> stmt;
        std::atomic<bool>     referenced{false}; // CLOCK second-chance bit
    };

    // Snapshot of per-Connection cache statistics. Counters are lifetime totals
    // (not reset by clear_statement_cache); current_size is the live entry count.
    struct CacheStats {
        std::uint64_t hits         = 0;
        std::uint64_t misses       = 0;
        std::uint64_t evictions    = 0;
        std::size_t   current_size = 0;
    };

    // Bundles the L3 cache map, its mutex, the stat counters, and the capacity.
    // Each Connection embeds one; both backends share the cache_* helpers that
    // operate on it. std::atomic is non-movable, so the move ops are explicit:
    // the map moves, the atomics reset to zero, capacity is copied — equivalent
    // to MovableSharedMutex's "fresh on move" contract (a Connection is only moved
    // during construction, before any thread shares it). Issue #273.
    template <typename Stmt> struct StatementCacheState {
        std::unordered_map<std::string, CacheEntry<Stmt>, string_hash, string_equal> map;
        mutable MovableSharedMutex mutex;
        std::atomic<std::uint64_t> hits{0};
        std::atomic<std::uint64_t> misses{0};
        std::atomic<std::uint64_t> evictions{0};
        std::size_t                capacity = 0; // 0 = unbounded

        StatementCacheState()  = default;
        ~StatementCacheState() = default;

        StatementCacheState(StatementCacheState&& other) noexcept
            : map(std::move(other.map)), capacity(other.capacity) {}
        auto operator=(StatementCacheState&& other) noexcept -> StatementCacheState& {
            if (this != &other) {
                map      = std::move(other.map);
                capacity = other.capacity;
                hits.store(0, std::memory_order_relaxed);
                misses.store(0, std::memory_order_relaxed);
                evictions.store(0, std::memory_order_relaxed);
            }
            return *this;
        }
        StatementCacheState(const StatementCacheState&)                    = delete;
        auto operator=(const StatementCacheState&) -> StatementCacheState& = delete;
    };
```

- [ ] **Step 2: Build the concept module to verify it compiles**

Run: `cmake --build --preset ninja-debug --target storm_db_concept 2>&1 | tail -20`
Expected: builds clean (no use of the new types yet, just definitions). If the target name differs, run `cmake --build --preset ninja-debug 2>&1 | tail -30` and confirm no errors referencing concept.cppm.

- [ ] **Step 3: Commit**

```bash
git add src/db/concept.cppm
git commit -m "feat(cache): add CacheEntry/StatementCacheState/CacheStats types (#273)"
```

---

## Task 2: Rewrite the shared `cache_*` helpers to use `StatementCacheState`

**Files:**
- Modify: `src/db/concept.cppm:205-250` (the five helper templates + the leading comment block)

- [ ] **Step 1: Replace the helper block**

Replace the entire region from the `// Issue #271: shared L3-cache locking helpers.` comment (line ~196) through the closing `}` of `cache_count` (line ~250) with:

```cpp
    // Issue #271/#273: shared L3-cache helpers. Both backends call cache_find_hit()
    // then cache_try_insert() around their own prepare step; the lock discipline,
    // CLOCK eviction, and statistics are identical across backends and live here.

    // Hot path: shared_lock find + reset. On a hit, sets the entry's CLOCK ref bit
    // and bumps `hits`; on a miss, bumps `misses`. Returns the cached Statement* on
    // a hit, nullptr on a miss (caller then prepares + inserts).
    template <typename Stmt>
    [[nodiscard]] auto cache_find_hit(StatementCacheState<Stmt>& state, std::string_view sql) -> Stmt* {
        std::shared_lock read_lock(state.mutex);
        auto             it = state.map.find(sql);
        if (it != state.map.end()) [[likely]] {
            it->second.referenced.store(true, std::memory_order_relaxed);
            state.hits.fetch_add(1, std::memory_order_relaxed);
            it->second.stmt->reset();
            return it->second.stmt.get();
        }
        state.misses.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    // CLOCK/second-chance eviction. Caller holds the unique_lock. Sweeps entries:
    // an entry whose ref bit is set gets it cleared (second chance) and is skipped;
    // the first entry with a clear bit is evicted. If a full pass clears every bit
    // without finding a victim, the next entry is evicted to guarantee progress.
    // Bumps `evictions`. Bounded to ~2 passes over a map of size <= capacity.
    template <typename Stmt> auto cache_evict_one(StatementCacheState<Stmt>& state) -> void {
        for (auto it = state.map.begin(); it != state.map.end(); ++it) {
            if (it->second.referenced.load(std::memory_order_relaxed)) {
                it->second.referenced.store(false, std::memory_order_relaxed);
                continue;
            }
            state.map.erase(it);
            state.evictions.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // Every bit was set; all are now cleared. Evict the first entry.
        if (!state.map.empty()) {
            state.map.erase(state.map.begin());
            state.evictions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Insert a freshly-prepared statement under a unique_lock. Evicts first if the
    // cache is at capacity (capacity 0 = unbounded). try_emplace keeps any entry a
    // racing thread inserted while we prepared (the lock was dropped during
    // prepare); the passed-in statement is then dropped by the caller. Returns the
    // live Statement* for the key.
    template <typename Stmt>
    [[nodiscard]] auto
    cache_try_insert(StatementCacheState<Stmt>& state, std::string_view sql, std::unique_ptr<Stmt> stmt) -> Stmt* {
        std::unique_lock write_lock(state.mutex);
        if (state.capacity != 0 && state.map.size() >= state.capacity && !state.map.contains(sql)) {
            cache_evict_one(state);
        }
        auto [it, inserted] = state.map.try_emplace(std::string(sql));
        if (inserted) {
            it->second.stmt = std::move(stmt);
        }
        return it->second.stmt.get();
    }

    // Drop every cached entry (unique_lock). Counters are lifetime totals and are
    // NOT reset here.
    template <typename Stmt> auto cache_clear_all(StatementCacheState<Stmt>& state) noexcept -> void {
        std::unique_lock write_lock(state.mutex);
        state.map.clear();
    }

    // Drop cached entries whose SQL references `table`, word-boundary aware
    // (unique_lock). Issue #215.
    template <typename Stmt>
    auto cache_clear_table(StatementCacheState<Stmt>& state, std::string_view table) -> void {
        std::unique_lock write_lock(state.mutex);
        std::erase_if(state.map, [table](const auto& entry) { return sql_references_table(entry.first, table); });
    }

    // Entry count (shared_lock).
    template <typename Stmt>
    [[nodiscard]] auto cache_count(const StatementCacheState<Stmt>& state) noexcept -> std::size_t {
        std::shared_lock read_lock(state.mutex);
        return state.map.size();
    }

    // Stats snapshot (shared_lock). Relaxed loads — the snapshot is advisory.
    template <typename Stmt>
    [[nodiscard]] auto cache_stats(const StatementCacheState<Stmt>& state) noexcept -> CacheStats {
        std::shared_lock read_lock(state.mutex);
        return CacheStats{
            state.hits.load(std::memory_order_relaxed),
            state.misses.load(std::memory_order_relaxed),
            state.evictions.load(std::memory_order_relaxed),
            state.map.size(),
        };
    }
```

> **Note on the `try_emplace` shape change.** The old helper passed the `unique_ptr` straight into `try_emplace`. Now the mapped type is `CacheEntry` (default-constructed by `try_emplace`), and the `unique_ptr` is moved into `.stmt` only when the key is newly inserted — on a race-loss (`inserted == false`) the prepared statement is dropped by the caller as before.

> **Note on `!state.map.contains(sql)` in the capacity check.** Re-preparing an SQL already present (a write that lost the find-race, then re-checks) must not evict to make room for a key that's already there. Guarding the eviction with `!contains` keeps `size` correct.

- [ ] **Step 2: Add `cache_stats()` to the `CachedDatabaseConnection` concept**

In `src/db/concept.cppm`, the concept block (lines ~60-70), add after the `cached_statement_count` line:

```cpp
        { conn.cache_stats() } -> std::same_as<CacheStats>;
```

- [ ] **Step 3: Build the concept module**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -40`
Expected: `concept.cppm` itself compiles. The backends (`sqlite.cppm`, `postgresql_connection.cppm`) will now FAIL to build because they still call the old `(cache, mutex)` signatures and don't satisfy the updated concept — that is expected and fixed in Tasks 3-4. Confirm the only errors are in the two backend modules, not in `concept.cppm`.

- [ ] **Step 4: Commit**

```bash
git add src/db/concept.cppm
git commit -m "feat(cache): CLOCK eviction + stats in shared cache_* helpers (#273)"
```

---

## Task 3: Wire the SQLite backend to `StatementCacheState` + add `Config`/`cache_stats()`

**Files:**
- Modify: `src/db/sqlite.cppm:251-449`

- [ ] **Step 1: Replace the cache member typedefs (lines 252-260)**

Replace:
```cpp
        using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
        // Issue #215: storing `unique_ptr<Statement>` (not `Statement`) keeps the
        // Statement object pinned in place across map rehashes. Upstream Level 2
        // caches hold raw `Statement*` pointers obtained from prepare_cached();
        // with value storage those pointers would dangle after any insert that
        // triggers a rehash.
        using StatementValue = std::unique_ptr<Statement>;
        using StatementCache =
                std::unordered_map<std::string, StatementValue, storm::db::string_hash, storm::db::string_equal>;
```
with:
```cpp
        using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
```

- [ ] **Step 2: Add a `Config` struct in the `public:` section (after the dialect traits, before `open`)**

Insert before the `// Factory method` comment at line 280:
```cpp
        // Issue #273: per-Connection cache configuration. capacity 0 = unbounded.
        struct Config {
            std::size_t statement_cache_capacity = 512;
        };
```

- [ ] **Step 3: Change `open` to accept `Config` (line 281)**

Replace the signature line:
```cpp
        [[nodiscard]] static auto open(std::string_view db_path) -> std::expected<Connection, Error> {
```
with:
```cpp
        [[nodiscard]] static auto open(std::string_view db_path, Config config = {})
                -> std::expected<Connection, Error> {
```
and change the final `return Connection{SqlitePtr{raw_db}};` (line 300) to:
```cpp
            return Connection{SqlitePtr{raw_db}, config};
```

- [ ] **Step 4: Update `prepare_cached` to the new helper signatures (lines 338, 346-348)**

Replace:
```cpp
            if (auto* hit = storm::db::cache_find_hit(statement_cache_, cache_mutex_, sql)) [[likely]] {
                return hit;
            }

            auto prepared = prepare_raw(sql);
            if (!prepared.has_value()) {
                return std::unexpected(prepared.error());
            }
            return storm::db::cache_try_insert(
                    statement_cache_, cache_mutex_, sql, std::make_unique<Statement>(std::move(*prepared))
            );
```
with:
```cpp
            if (auto* hit = storm::db::cache_find_hit(cache_, sql)) [[likely]] {
                return hit;
            }

            auto prepared = prepare_raw(sql);
            if (!prepared.has_value()) {
                return std::unexpected(prepared.error());
            }
            return storm::db::cache_try_insert(cache_, sql, std::make_unique<Statement>(std::move(*prepared)));
```

- [ ] **Step 5: Update `clear_statement_cache`, `clear_statement_cache(table)`, `cached_statement_count` (lines 354-369)**

Replace the three helper-call bodies:
```cpp
            storm::db::cache_clear_all(statement_cache_, cache_mutex_); // Issue #271
```
→ `storm::db::cache_clear_all(cache_); // Issue #271`
```cpp
            storm::db::cache_clear_table(statement_cache_, cache_mutex_, table); // Issue #271
```
→ `storm::db::cache_clear_table(cache_, table); // Issue #271`
```cpp
            return storm::db::cache_count(statement_cache_, cache_mutex_); // Issue #271
```
→ `return storm::db::cache_count(cache_); // Issue #271`

- [ ] **Step 6: Add the `cache_stats()` accessor after `cached_statement_count` (after line 369)**

```cpp
        // Issue #273: snapshot of hit/miss/eviction counters + current size.
        [[nodiscard]] auto cache_stats() const noexcept -> storm::db::CacheStats {
            return storm::db::cache_stats(cache_);
        }
```

- [ ] **Step 7: Update the private constructor (lines 419-424)**

Replace:
```cpp
        explicit Connection(SqlitePtr db_ptr) : db(std::move(db_ptr)) {
            // Reserve capacity to keep early inserts on the same rehash bucket.
            // Pointer stability across rehash is now guaranteed by the
            // `unique_ptr<Statement>` value type (Issue #215).
            statement_cache_.reserve(cache::STMT_CACHE_RESERVE);
        }
```
with:
```cpp
        explicit Connection(SqlitePtr db_ptr, Config config) : db(std::move(db_ptr)) {
            cache_.capacity = config.statement_cache_capacity; // Issue #273
            // Reserve to keep early inserts on the same rehash bucket. Pointer
            // stability across rehash is guaranteed by the unique_ptr in CacheEntry
            // (Issue #215).
            cache_.map.reserve(cache::STMT_CACHE_RESERVE);
        }
```

- [ ] **Step 8: Replace the member variables (lines 436-442)**

Replace:
```cpp
        SqlitePtr      db;
        StatementCache statement_cache_;
        // Issue #271: guards statement_cache_. shared_lock on the cache-hit hot
        // path, unique_lock for insert + clear. mutable so const accessors
        // (cached_statement_count) can take a shared_lock. MovableSharedMutex
        // keeps Connection's move operations defaulted.
        mutable storm::db::MovableSharedMutex cache_mutex_;
```
with:
```cpp
        SqlitePtr db;
        // Issue #271/#273: the L3 cache, its shared_mutex, stat counters, and
        // capacity in one movable bundle. shared_lock on the hit path, unique_lock
        // for insert/clear/evict. mutable so const accessors (cached_statement_count,
        // cache_stats) can take a shared_lock.
        mutable storm::db::StatementCacheState<Statement> cache_;
```

- [ ] **Step 9: Build SQLite backend**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -40`
Expected: `sqlite.cppm` compiles and satisfies `CachedDatabaseConnection` (the `static_assert` at line 447 passes). PostgreSQL may still fail (fixed in Task 4). Confirm no errors in sqlite.cppm.

- [ ] **Step 10: Commit**

```bash
git add src/db/sqlite.cppm
git commit -m "feat(cache): wire SQLite Connection to StatementCacheState + Config/cache_stats (#273)"
```

---

## Task 4: Wire the PostgreSQL backend (same shape)

**Files:**
- Modify: `src/db/postgresql_connection.cppm:23-231`

- [ ] **Step 1: Remove the cache typedefs (lines 25-30) and the reserve constant if now unused**

Replace:
```cpp
        // Issue #215: storing `unique_ptr<Statement>` keeps Statement objects
        // pinned across map rehashes, so Level 2 callers can hold
        // `Statement*` from prepare_cached() safely.
        using StatementValue = std::unique_ptr<Statement>;
        using StatementCache =
                std::unordered_map<std::string, StatementValue, storm::db::string_hash, storm::db::string_equal>;
```
with nothing (delete these lines). Keep `static constexpr std::size_t STMT_CACHE_RESERVE = 32;` (line 33) — it's still used in the ctor.

- [ ] **Step 2: Add `Config` in the `public:` section (after the dialect traits, before `prepare_common_statements` at line 45)**

```cpp
        // Issue #273: per-Connection cache configuration. capacity 0 = unbounded.
        struct Config {
            std::size_t statement_cache_capacity = 512;
        };
```

- [ ] **Step 3: Change `open` to accept `Config` (line 52)**

Replace:
```cpp
        [[nodiscard]] static auto open(std::string_view conninfo) -> std::expected<Connection, Error> {
```
with:
```cpp
        [[nodiscard]] static auto open(std::string_view conninfo, Config config = {})
                -> std::expected<Connection, Error> {
```
and change `return Connection{PGconnPtr{raw_conn}};` (line 66) to:
```cpp
            return Connection{PGconnPtr{raw_conn}, config};
```

- [ ] **Step 4: Update `prepare_cached` (lines 105, 115)**

Replace:
```cpp
            if (auto* hit = storm::db::cache_find_hit(statement_cache_, cache_mutex_, sql)) [[likely]] {
                return hit;
            }
```
with:
```cpp
            if (auto* hit = storm::db::cache_find_hit(cache_, sql)) [[likely]] {
                return hit;
            }
```
and replace:
```cpp
            return storm::db::cache_try_insert(statement_cache_, cache_mutex_, sql, std::move(new_stmt));
```
with:
```cpp
            return storm::db::cache_try_insert(cache_, sql, std::move(new_stmt));
```

- [ ] **Step 5: Update clear/count helpers (lines 122, 129, 133)**

```cpp
            storm::db::cache_clear_all(statement_cache_, cache_mutex_); // Issue #271
```
→ `storm::db::cache_clear_all(cache_); // Issue #271`
```cpp
            storm::db::cache_clear_table(statement_cache_, cache_mutex_, table); // Issue #271
```
→ `storm::db::cache_clear_table(cache_, table); // Issue #271`
```cpp
            return storm::db::cache_count(statement_cache_, cache_mutex_); // Issue #271
```
→ `return storm::db::cache_count(cache_); // Issue #271`

- [ ] **Step 6: Add `cache_stats()` after `cached_statement_count` (after line 134)**

```cpp
        // Issue #273: snapshot of hit/miss/eviction counters + current size.
        [[nodiscard]] auto cache_stats() const noexcept -> storm::db::CacheStats {
            return storm::db::cache_stats(cache_);
        }
```

- [ ] **Step 7: Update the private constructor (lines 165-167)**

Replace:
```cpp
        explicit Connection(PGconnPtr conn_ptr) : conn_(std::move(conn_ptr)) {
            statement_cache_.reserve(STMT_CACHE_RESERVE);
        }
```
with:
```cpp
        explicit Connection(PGconnPtr conn_ptr, Config config) : conn_(std::move(conn_ptr)) {
            cache_.capacity = config.statement_cache_capacity; // Issue #273
            cache_.map.reserve(STMT_CACHE_RESERVE);
        }
```

- [ ] **Step 8: Replace the member variables (lines 226-231)**

Replace:
```cpp
        StatementCache statement_cache_;
        // Issue #271: guards statement_cache_. shared_lock on the cache-hit hot
        // path, unique_lock for insert + clear. mutable so const accessors
        // (cached_statement_count) can take a shared_lock. MovableSharedMutex
        // keeps Connection's move operations defaulted.
        mutable storm::db::MovableSharedMutex cache_mutex_;
```
with:
```cpp
        // Issue #271/#273: L3 cache + mutex + stat counters + capacity in one
        // movable bundle. mutable so const accessors take a shared_lock.
        mutable storm::db::StatementCacheState<Statement> cache_;
```

> Verify the surrounding member declarations: `conn_` (the `PGconnPtr`) stays as-is. Only the two cache members are replaced by one.

- [ ] **Step 9: Build both backends**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -40`
Expected: full build succeeds; both `static_assert(CachedDatabaseConnection<Connection>)` pass.

- [ ] **Step 10: Commit**

```bash
git add src/db/postgresql_connection.cppm
git commit -m "feat(cache): wire PostgreSQL Connection to StatementCacheState + Config/cache_stats (#273)"
```

---

## Task 5: Thread capacity through `ConnectionPool`

**Files:**
- Modify: `src/db/pool.cppm:14-21` (PoolConfig), `:217`, `:258` (open calls)

- [ ] **Step 1: Add the field to `PoolConfig` (after line 20)**

In the `PoolConfig` struct, add after `validate_on_checkout`:
```cpp
        std::size_t statement_cache_capacity = 512; // Issue #273: 0 = unbounded
```

- [ ] **Step 2: Pass it at both `ConnType::open` callsites (lines 217 and 258)**

Replace each occurrence of:
```cpp
                auto result = ConnType::open(conninfo_);
```
with:
```cpp
                auto result = ConnType::open(
                        conninfo_, {.statement_cache_capacity = config_.statement_cache_capacity});
```

> **Note:** Both backends name the field `statement_cache_capacity` inside their own `Config`, so the designated-initializer `{.statement_cache_capacity = ...}` deduces `ConnType::Config` at each callsite. There are exactly two callsites (grep `ConnType::open(conninfo_)` to confirm).

- [ ] **Step 3: Build**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -30`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/db/pool.cppm
git commit -m "feat(cache): thread statement_cache_capacity through ConnectionPool (#273)"
```

---

## Task 6: Tests — eviction, stats, memory-growth, unbounded, clear-preserves-counters

**Files:**
- Modify: `tests/db/test_cache_invalidation.cpp` (add tests in the existing anonymous namespace, before its closing `}`)

These run SQLite-direct, matching the existing file. The shared `StatementCacheState` + helpers are identical for PG, so SQLite coverage exercises the same code path for both backends; the cross-backend requirement is met by the shared implementation.

- [ ] **Step 1: Write the failing eviction-bound test**

Add inside the anonymous namespace (before the closing `} // namespace`):

```cpp
    // ------------------------------------------------------------------ #273
    // Capacity-bounded eviction: a connection opened with capacity N keeps at
    // most N entries; inserting M > N distinct SQLs evicts M - N of them.
    TEST(CacheEvictionTest, BoundsSizeAtCapacity) {
        auto conn = Connection::open(":memory:", {.statement_cache_capacity = 4}).value();
        constexpr int kInserts = 10;
        for (int i = 0; i < kInserts; ++i) {
            ASSERT_TRUE(conn.prepare_cached(std::format("SELECT {}", i)).has_value()) << "insert " << i;
        }
        const auto stats = conn.cache_stats();
        EXPECT_EQ(stats.current_size, 4U);
        EXPECT_EQ(stats.evictions, static_cast<std::uint64_t>(kInserts) - 4U);
    }
```

- [ ] **Step 2: Run it — expect FAIL (won't compile: `open` has no Config arg / `cache_stats` missing) UNLESS Tasks 1-5 are done**

Run: `cmake --build --preset ninja-debug 2>&1 | tail -20 && ./build/debug/tests/storm_tests --gtest_filter='CacheEvictionTest.BoundsSizeAtCapacity'`
Expected (if Tasks 1-5 complete): PASS. (If you are doing strict red-green and reordered tests before impl, it FAILs to compile — that is the red. With Tasks 1-5 already committed, this test validates them and should PASS.)

- [ ] **Step 3: Write the second-chance / stats / memory-growth / unbounded / clear tests**

Add after the eviction test:

```cpp
    // Second chance: an entry re-hit between cold inserts keeps its ref bit set
    // and survives eviction. With capacity 2, hammering "HOT" while churning cold
    // SQLs must leave "HOT" a cache hit (not re-prepared).
    TEST(CacheEvictionTest, SecondChanceKeepsHotEntry) {
        auto conn = Connection::open(":memory:", {.statement_cache_capacity = 2}).value();
        ASSERT_TRUE(conn.prepare_cached("SELECT 100").has_value()); // the HOT entry
        const auto baseline_misses = conn.cache_stats().misses;

        for (int i = 0; i < 8; ++i) {
            ASSERT_TRUE(conn.prepare_cached("SELECT 100").has_value());        // re-hit HOT (sets ref bit)
            ASSERT_TRUE(conn.prepare_cached(std::format("SELECT c{}", i)).has_value()); // cold churn
        }
        // Re-preparing HOT must be a hit: misses must NOT increase from this call.
        const auto before = conn.cache_stats().misses;
        ASSERT_TRUE(conn.prepare_cached("SELECT 100").has_value());
        EXPECT_EQ(conn.cache_stats().misses, before) << "HOT entry was evicted despite being re-hit";
        EXPECT_GT(before, baseline_misses); // sanity: cold churn did cause misses
    }

    // Stats correctness over a known sequence.
    TEST(CacheStatsTest, CountsHitsMissesSize) {
        auto conn = Connection::open(":memory:").value(); // default capacity, no eviction here
        ASSERT_TRUE(conn.prepare_cached("SELECT 1").has_value()); // miss
        ASSERT_TRUE(conn.prepare_cached("SELECT 2").has_value()); // miss
        ASSERT_TRUE(conn.prepare_cached("SELECT 1").has_value()); // hit
        ASSERT_TRUE(conn.prepare_cached("SELECT 1").has_value()); // hit
        const auto stats = conn.cache_stats();
        EXPECT_EQ(stats.misses, 2U);
        EXPECT_EQ(stats.hits, 2U);
        EXPECT_EQ(stats.evictions, 0U);
        EXPECT_EQ(stats.current_size, 2U);
    }

    // Memory-growth bound: 10k unique SQLs at capacity 512 stays at 512.
    TEST(CacheEvictionTest, MemoryGrowthBounded) {
        auto conn = Connection::open(":memory:", {.statement_cache_capacity = 512}).value();
        constexpr int kUnique = 10000;
        for (int i = 0; i < kUnique; ++i) {
            ASSERT_TRUE(conn.prepare_cached(std::format("SELECT {}", i)).has_value());
        }
        const auto stats = conn.cache_stats();
        EXPECT_EQ(stats.current_size, 512U);
        EXPECT_EQ(stats.evictions, static_cast<std::uint64_t>(kUnique) - 512U);
    }

    // Unbounded mode (capacity 0): no eviction, every distinct SQL retained.
    TEST(CacheEvictionTest, UnboundedRetainsAll) {
        auto conn = Connection::open(":memory:", {.statement_cache_capacity = 0}).value();
        constexpr int kUnique = 2000;
        for (int i = 0; i < kUnique; ++i) {
            ASSERT_TRUE(conn.prepare_cached(std::format("SELECT {}", i)).has_value());
        }
        const auto stats = conn.cache_stats();
        EXPECT_EQ(stats.current_size, static_cast<std::size_t>(kUnique));
        EXPECT_EQ(stats.evictions, 0U);
    }

    // clear_statement_cache empties the map but preserves lifetime counters.
    TEST(CacheStatsTest, ClearPreservesCounters) {
        auto conn = Connection::open(":memory:").value();
        ASSERT_TRUE(conn.prepare_cached("SELECT 1").has_value()); // miss
        ASSERT_TRUE(conn.prepare_cached("SELECT 1").has_value()); // hit
        conn.clear_statement_cache();
        const auto stats = conn.cache_stats();
        EXPECT_EQ(stats.current_size, 0U);
        EXPECT_EQ(stats.hits, 1U) << "counters are lifetime totals, not reset by clear";
        EXPECT_EQ(stats.misses, 1U);
    }
```

- [ ] **Step 4: Build + run the new tests**

Run:
```bash
cmake --build --preset ninja-debug 2>&1 | tail -20 && \
./build/debug/tests/storm_tests --gtest_filter='CacheEvictionTest.*:CacheStatsTest.*'
```
Expected: all 6 new tests PASS.

- [ ] **Step 5: Run the full cache test file to confirm no regression**

Run: `./build/debug/tests/storm_tests --gtest_filter='*Cache*:*cache*:SqlReferencesTable*'`
Expected: all PASS (existing `CacheInvalidationLevel3Test.*` + new tests).

- [ ] **Step 6: Commit**

```bash
git add tests/db/test_cache_invalidation.cpp
git commit -m "test(cache): LRU eviction + stats + memory-growth tests (#273)"
```

---

## Task 7: Full suite + sanitizers

- [ ] **Step 1: Full SQLite + PG test suite**

Run: `ctest --preset ninja-debug 2>&1 | tail -25`
Expected: all pass (PG skips gracefully if not running).

- [ ] **Step 2: ASAN+UBSAN**

Run:
```bash
cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan 2>&1 | tail -5 && \
ctest --preset ninja-asan-ubsan --test-dir build/asan-ubsan 2>&1 | tail -20
```
Expected: no new ASAN/UBSAN reports. (Use the project's standard asan invocation if the `--test-dir` path differs — check CLAUDE.md presets.)

- [ ] **Step 3: TSAN (data races — the cache is shared_mutex-guarded)**

Run:
```bash
cmake --preset ninja-tsan && cmake --build --preset ninja-tsan 2>&1 | tail -5 && \
ctest --preset ninja-tsan 2>&1 | tail -20
```
Expected: no new TSAN reports. The atomic ref bit + atomic counters + shared_mutex must be race-clean.

- [ ] **Step 4: Commit (only if any sanitizer fix was needed; otherwise skip)**

```bash
git add -A && git commit -m "fix(cache): sanitizer cleanups for LRU eviction (#273)"
```

---

## Task 8: Benchmark gate (the Definition-of-Done blocker)

- [ ] **Step 1: Build Release**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release 2>&1 | tail -5`

- [ ] **Step 2: Run Core-8 filter, both this branch and develop, 20 reps**

Run:
```bash
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/.*' --benchmark_repetitions=20 \
  --benchmark_report_aggregates_only=true 2>&1 | tee /tmp/bench_273_branch.txt
```
Then `git stash` (if needed), `git checkout develop`, rebuild Release, run the same into `/tmp/bench_273_develop.txt`, and `git checkout feature/273-l3-cache-lru-eviction`.

> Verify both builds have `-O3` before trusting the comparison (a fresh worktree configure can leave `CMAKE_CXX_FLAGS_RELEASE` empty — confirm with `grep CMAKE_CXX_FLAGS_RELEASE build/release/CMakeCache.txt`).

- [ ] **Step 3: Compare medians**

Compare the cache-hit-dominated rows (single-row GET/PK reuse loops especially). 
- Within ±5% → keep default 512. Done.
- Hot path regresses >5% → change the default to `0` in all three places (`sqlite.cppm` Config, `postgresql_connection.cppm` Config, `pool.cppm` PoolConfig), document the measured numbers, rebuild, re-run. Eviction stays available, opt-in.

- [ ] **Step 4: Write the results doc**

Create `docs/superpowers/results/2026-05-31-l3-cache-lru-eviction-results.md` with the before/after median table and the keep-512-or-fallback decision.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/results/2026-05-31-l3-cache-lru-eviction-results.md src/db/ 2>/dev/null
git commit -m "perf(cache): Core-8 bench gate results for LRU eviction (#273)"
```

---

## Task 9: Docs

**Files:**
- Modify: `docs/architecture/STATEMENT_CACHING.md`

- [ ] **Step 1: Replace the unbounded-growth note with the eviction policy**

Find the section describing unbounded growth / no eviction and replace it with: the CLOCK/second-chance policy, the `Config{.statement_cache_capacity = N}` surface (default 512, `0` = unbounded), the `PoolConfig` field, and the `cache_stats()` accessor + `CacheStats` struct. Keep the existing single-cache framing from #214.

- [ ] **Step 2: Grep for other docs/agents referencing the cache**

Run: `grep -rln "statement_cache\|unbounded\|no eviction\|prepare_cached" docs/ .claude/agents/`
Update any that describe the old unbounded behavior (e.g. CLAUDE.md's design-decisions bullet 4 if it mentions unboundedness).

- [ ] **Step 3: Commit**

```bash
git add docs/ .claude/agents/ CLAUDE.md 2>/dev/null
git commit -m "docs(cache): document LRU eviction policy + cache_stats (#273)"
```

---

## Task 10: Issue subtasks, PR, gate

- [ ] **Step 1: Check off the issue's Definition-of-Done subtasks**

Run `gh issue view 273`, then `gh issue edit 273 --body "..."` replacing each completed `- [ ]` with `- [x]` (LRU eviction, cache_stats, no bench regression, memory-growth test, docs updated).

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin feature/273-l3-cache-lru-eviction
gh pr create --base develop --title "feat(cache): LRU eviction + statistics on L3 statement cache (#273)" \
  --body "Closes #273

CLOCK/second-chance eviction + hit/miss/eviction/size stats on the single Connection-level statement cache. Configurable capacity (default 512, 0 = unbounded), threaded through ConnectionPool. Bench gate result: <fill in>.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

- [ ] **Step 3: SonarCloud gate + CI**

Wait 30s, run `/sonarcloud-status`. Fix any new-code issues/duplication until clean. Then `gh pr checks <PR#> --watch`. Merge with `--squash` only after both pass.

- [ ] **Step 4: Close issue + return to develop**

```bash
gh issue close 273
git checkout develop && git pull
```

---

## Self-Review Notes

- **Spec coverage:** capacity config (T3/T4/T5) ✓, LRU eviction (T2 `cache_evict_one`) ✓, stats (T1 `CacheStats`, T2 `cache_stats` helper, T3/T4 accessor) ✓, both backends (T3/T4) ✓, tests incl. memory-growth + unbounded (T6) ✓, bench gate (T8) ✓, docs (T9) ✓.
- **Type consistency:** `StatementCacheState<Stmt>`, `CacheEntry<Stmt>`, `CacheStats`, `cache_*` helper names, and `.statement_cache_capacity` field name are used identically across all tasks.
- **No placeholders:** every code step shows complete code; bench-fallback path is concrete (change default in 3 named files).
