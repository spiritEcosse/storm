# L3 Statement Cache — LRU Eviction + Statistics (Issue #273)

**Date:** 2026-05-31
**Issue:** #273 — feat(cache): LRU eviction on L3 statement cache to bound memory growth (Phase 4 of #215)
**Parent:** #215 (acceptance criteria #5 LRU eviction, #6 statistics, #9 memory-leak tests)
**Predecessor:** #214 collapsed L1/L2 into a single L3 cache (PR #339); #271 made it `shared_mutex`-protected (PR #337).

## Problem

`Connection`'s statement cache is the only statement cache Storm has after #214. It is an
`unordered_map<string, unique_ptr<Statement>>`, `shared_mutex`-protected, with **no eviction**.
It grows by one entry per distinct SQL string ever prepared, for the life of the connection.
The only way to shrink it is the manual `clear_statement_cache()` / `clear_statement_cache(table)`.

Unbounded growth only bites a long-lived connection that prepares a genuinely unbounded set of
distinct SQL strings (dynamic literal-varying SQL, ad-hoc reporting). Storm parameterises WHERE
values, so the common ORM case plateaus on its own. **Priority: medium / opportunistic.**

Because #214 removed all raw `Statement*` caches, eviction now just erases a map entry and
destroys its `unique_ptr<Statement>` — there is nothing external to invalidate.

## Goals

1. Configurable max capacity on `Connection`, propagated through `ConnectionPool`.
2. Automatic LRU-style eviction when the cache exceeds capacity.
3. Per-connection hit / miss / eviction / size statistics via `cache_stats()`.
4. Tests on both backends (SQLite + PostgreSQL, TYPED_TEST).
5. **No Core-8 bench regression (±5%)** — the cache-hit hot path must not regress.

## Key design decisions

| Decision | Choice | Rationale |
|---|---|---|
| Eviction policy | **CLOCK / second-chance** approximate-LRU | Hit only flips a per-entry atomic ref bit under the existing `shared_lock` — no structural mutation, no per-hit write lock. Preserves the #271 parallel-read hot path. Strict list-LRU would turn every hit into a `unique_lock` write and likely fail the ±5% gate. |
| Capacity config | `Config{ .statement_cache_capacity = N }` param on `open()` | Minimal new surface; matches issue scope point 1. Threaded through `PoolConfig`. |
| Default capacity | **512** (bounded; `0` = unbounded) | Ships a sane ceiling. Hot-path cost re-measured against the bench gate; fall back to `0` only if 512 regresses Core-8 >5%. |
| Code location | Extend shared `storm::db::cache_*` helpers in `concept.cppm` | Both backends already delegate there. One implementation, no SonarCloud new-code duplication. |
| Stats shape | `CacheStats{ hits, misses, evictions, current_size }` snapshot | Exactly issue scope point 3; no capacity/hit_rate (derivable, YAGNI). |

## Design

### 1. Cache value type — second-chance bit per entry

The map's mapped type gains a CLOCK reference bit. `unique_ptr<Statement>` inside still pins the
`Statement` across rehashes (pointer stability preserved, #215).

```cpp
template <typename Stmt>
struct CacheEntry {
    std::unique_ptr<Stmt> stmt;
    std::atomic<bool>     referenced{false};  // CLOCK second-chance bit
};
```

> **Note on `std::atomic<bool>` in the map.** `CacheEntry` is non-movable/non-copyable because
> `std::atomic` is. `unordered_map` never moves its nodes, so this is fine for the map itself, but
> `try_emplace` must construct in place (it does). Eviction removes whole nodes, never relocates.

### 2. Bundled cache state — shared across backends

Counters and capacity must travel with the cache and be shared by both backends. Bundle the map,
mutex, atomics, capacity, and clock hand into one struct that each `Connection` embeds, replacing
the current separate `statement_cache_` + `cache_mutex_` members.

```cpp
template <typename Stmt>
struct StatementCacheState {
    std::unordered_map<std::string, CacheEntry<Stmt>, string_hash, string_equal> map;
    mutable MovableSharedMutex mutex;
    std::atomic<std::uint64_t> hits{0};
    std::atomic<std::uint64_t> misses{0};
    std::atomic<std::uint64_t> evictions{0};
    std::size_t capacity = 0;   // set from Config at construction; 0 = unbounded
    // No persistent clock hand: an iterator-stable cursor over a rehashing unordered_map is
    // awkward, and cache size is bounded at <= capacity, so each eviction sweep restarts from
    // map.begin(). The sweep is O(capacity) worst case and runs only on insert-past-capacity,
    // already under the unique_lock — it adds no lock acquisition to the hot path.
};
```

**Movability.** `std::atomic` is neither movable nor copyable, so a defaulted move on
`StatementCacheState` is deleted. A `Connection` is only ever moved during construction (before any
thread shares it), so the cache state never needs to carry live counter values across a move: give
`StatementCacheState` an explicit move constructor/assignment that move-constructs the `map` and
**value-initializes** the atomics (`hits/misses/evictions = 0`) — equivalent to the
`MovableSharedMutex` "fresh on move" contract (#271). `capacity` is copied. The `Connection` move
stays defaulted (it moves the one `StatementCacheState cache_` member via that explicit move).

### 3. `CacheStats` snapshot

```cpp
struct CacheStats {
    std::uint64_t hits         = 0;
    std::uint64_t misses       = 0;
    std::uint64_t evictions    = 0;
    std::size_t   current_size = 0;
};
```

### 4. Helper changes in `concept.cppm`

All operate on `StatementCacheState&` (or `const&`) instead of `(cache, mutex)` pairs.

- **`cache_find_hit`** — `shared_lock`. On hit: `entry.referenced.store(true, relaxed)`,
  `state.hits.fetch_add(1, relaxed)`, `entry.stmt->reset()`, return `entry.stmt.get()`.
  On miss: `state.misses.fetch_add(1, relaxed)`, return `nullptr`.
- **`cache_try_insert`** — `unique_lock`. Before emplace, if
  `state.capacity != 0 && state.map.size() >= state.capacity`, run the **clock sweep**:
  iterate entries; for each with `referenced == true`, clear it (second chance) and continue;
  evict the first entry with `referenced == false` (`state.evictions.fetch_add(1, relaxed)`,
  erase node). If a full pass clears every bit without finding a victim, evict the next entry
  (guarantees forward progress — bounded to one extra pass). Then `try_emplace`.
- **`cache_stats`** — `shared_lock`; returns `CacheStats{ hits.load, misses.load,
  evictions.load, map.size() }` (relaxed loads).
- **`cache_clear_all` / `cache_clear_table` / `cache_count`** — updated for the
  `CacheEntry` value type (`.stmt` access in `cache_clear_table`'s predicate; clear/size unchanged
  in shape). Counters are **not** reset by `clear_*` (they are lifetime totals).

### 5. Config surface

```cpp
struct Config {                            // per-backend, or shared in concept
    std::size_t statement_cache_capacity = 512;  // 0 = unbounded
};
[[nodiscard]] static auto open(std::string_view db_path, Config config = {})
    -> std::expected<Connection, Error>;
```

`PoolConfig` gains `std::size_t statement_cache_capacity = 512;`. `PoolCore` passes it:
`ConnType::open(conninfo_, {.statement_cache_capacity = config_.statement_cache_capacity})`.

The `Connection` private constructor stores `config.statement_cache_capacity` into
`cache_.capacity` and keeps the existing `map.reserve(STMT_CACHE_RESERVE)`.

### 6. Public accessor + concept

Each `Connection` gets `[[nodiscard]] auto cache_stats() const noexcept -> CacheStats;`
delegating to the shared helper. Add to the `CachedDatabaseConnection` concept:

```cpp
{ conn.cache_stats() } -> std::same_as<CacheStats>;
```

`open(path, Config)` must remain concept-compatible — add the `Config`-arg `open` to the concept
(or keep a defaulted second arg so the single-arg form still satisfies callers).

## Testing (TYPED_TEST over DatabaseTypes — SQLite + PostgreSQL)

1. **Eviction bounds size.** Open with `capacity = 4`. Prepare 10 distinct SQLs → `cache_stats().current_size == 4`, `evictions == 6`.
2. **Second-chance keeps hot entries.** With `capacity = 4`, repeatedly re-hit one SQL while inserting cold ones; assert the hot SQL still produces a cache **hit** (not a miss) after the cold churn.
3. **Stats correctness.** Known sequence of hits and misses → `hits` / `misses` / `evictions` / `current_size` all match expected counts.
4. **Memory-growth bound.** `capacity = 512`, 10 000 unique SQLs → `current_size == 512`, `evictions == 10000 - 512`.
5. **Unbounded mode.** `capacity = 0`, 10 000 unique SQLs → `current_size == 10000`, `evictions == 0`.
6. **clear preserves counters.** After `clear_statement_cache()`, `current_size == 0` but `hits`/`misses`/`evictions` retain their lifetime totals.

Error paths exercised via the existing mock-error pattern where applicable.

## Bench gate

Core-8 filter, Release, 20–30 reps, compared against `develop`:
- Within ±5% → keep default 512.
- Hot-path regression >5% → change the **default** to `0` (unbounded; opt-in eviction), document
  the measured numbers in the results doc, keep all the machinery. Eviction stays available, just
  off by default.

## Docs

`docs/architecture/STATEMENT_CACHING.md`: replace the unbounded-growth note with the CLOCK
eviction policy, the capacity config, and `cache_stats()`.
Agent files referencing the cache (if any) updated alongside.

## Out of scope

- `hit_rate` / `capacity` in `CacheStats` (derivable, YAGNI).
- Resetting counters on `clear_*` (lifetime totals are more useful; not requested).
- Strict (exact) LRU ordering — approximate CLOCK is sufficient for a statement cache.
