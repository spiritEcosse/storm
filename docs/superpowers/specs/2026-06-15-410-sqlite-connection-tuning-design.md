# #410 — SQLite connection tuning (busy_timeout default + optional WAL)

## Problem

SQLite connections open with the default `busy_timeout = 0`, so two pooled
connections touching the same file return `SQLITE_BUSY` immediately on write
contention instead of waiting. There is no PRAGMA / tuning support anywhere in
`src/`, and no way to enable WAL.

## Decisions (confirmed with user)

1. **SQLite-specific `Config` struct** — not added to the shared
   `StatementCacheConfig`. PostgreSQL keeps the shared config untouched.
2. **`PoolConfig` carries the SQLite tuning knobs** — propagated to each pooled
   connection. PG pools ignore the SQLite-only fields.
3. **WAL opt-in included now** — `journal_mode` flag applied via
   `PRAGMA journal_mode=WAL` in `open()`.

## Design

### 1. SQLite `Config` (sqlite.cppm)

Replace `using Config = storm::db::StatementCacheConfig;` with:

```cpp
enum class JournalMode { Default, WAL };

struct Config {
    std::size_t statement_cache_capacity = 512;   // field name unchanged → designated-init call sites keep compiling
    int         busy_timeout_ms          = 5000;  // non-zero default; 0 = no wait (legacy behaviour)
    JournalMode journal_mode             = JournalMode::Default;
};
```

Keeping the field name `statement_cache_capacity` preserves existing
`Connection::open(":memory:", {.statement_cache_capacity = N})` call sites.

### 2. Apply tuning in `open()`

After a successful `sqlite3_open_v2`, before constructing `Connection`:
- `sqlite3_busy_timeout(raw_db, config.busy_timeout_ms)` when `> 0`.
- If `journal_mode == WAL`: `sqlite3_exec(raw_db, "PRAGMA journal_mode=WAL", …)`.
  WAL is silently ignored on `:memory:`/temp DBs (no error). On failure of
  either call, close and return `std::unexpected`.

### 3. PoolConfig plumbing (pool.cppm)

`PoolConfig` gains:
```cpp
int         busy_timeout_ms = 5000;
JournalMode journal_mode    = JournalMode::Default;   // reused from sqlite backend
```

`pool.cppm` calls `ConnType::open(conninfo_, {…})` generically for both SQLite
and PG. PG's `Config` has no `busy_timeout_ms`, so the braced-init can't name it
unconditionally. A backend-dispatched helper builds the right config:

```cpp
template <CachedDatabaseConnection ConnType>
auto make_conn_config(const PoolConfig& pc) -> typename ConnType::Config {
    typename ConnType::Config cfg{};
    cfg.statement_cache_capacity = pc.statement_cache_capacity;
    if constexpr (requires { cfg.busy_timeout_ms; }) {
        cfg.busy_timeout_ms = pc.busy_timeout_ms;
        cfg.journal_mode    = pc.journal_mode;
    }
    return cfg;
}
```

Both `try_grow` and `create_entry` call `make_conn_config<ConnType>(config_)`,
deduping the two identical inline braced-inits.

`JournalMode` lives in the sqlite backend; `pool.cppm` imports it. (PoolConfig is
generic but referencing a backend enum as a default is acceptable — PG ignores it.)

## Tests (TDD — written first, must fail before implementation)

- `Config` defaults: `busy_timeout_ms == 5000`, `journal_mode == Default`.
- `open()` applies `busy_timeout`: probe via `PRAGMA busy_timeout;` returning the
  configured value (use `prepare` + `step` + `extract_int`).
- `open()` with `JournalMode::WAL` on a file DB: `PRAGMA journal_mode;` returns
  `"wal"`. On `:memory:` it falls back (returns `"memory"`), no error.
- Concurrent-writer test on a shared temp file: with a non-zero `busy_timeout`,
  two connections writing do not fail immediately with `SQLITE_BUSY`; with
  `busy_timeout_ms = 0` the collision returns `SQLITE_BUSY` (proves the knob works).
- Pool propagation: a `ConnectionPool<sqlite::Connection>` created with
  `busy_timeout_ms` set hands out connections whose `PRAGMA busy_timeout` matches.

## DoD mapping

- [x] SQLite `Config` exposes `busy_timeout_ms` (sane non-zero default), applied in `open()`.
- [x] `PoolConfig` propagates tuning knobs (via `make_conn_config`).
- [x] WAL opt-in included.
- [x] Concurrent-writer test.
- [ ] Bench: confirm no single-connection hot-path regression (open() cost only; hot path unchanged).
- [ ] Docs: tuning knobs + pooled-SQLite concurrency story.
