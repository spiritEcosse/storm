# #363 — Converge runtime execute paths on a shared prepare→reset→bind skeleton

**Date:** 2026-06-03
**Issue:** #363 (code-quality / refactor / tech-debt, priority: low)

## Reframing (PR-documented finding)

The issue title says "converge on monadic `std::expected` (`and_then`/`transform`/`or_else`)".
But its own cited model — `setop::ready_statement` — does **not** use `and_then`. It hoists the
repeated `prepare_cached → reset → bind → return Statement*` skeleton into one shared helper and
keeps explicit `if (!x) return std::unexpected(...)` control flow.

Decision (confirmed with user): follow the **actual model**, not the title.

- **Goal:** skeleton dedup, explicit error checks.
- **Non-goal:** introducing `and_then`/`transform` lambdas into runtime row loops.

Rationale: Storm already gains 5–6 % from flat-code-over-lambdas in hot loops. A literal monadic
rewrite risks regressing even when it "should" inline (the issue says exactly this). The PR will
document that the title is misleading and that monadic lambdas were deliberately kept out of the
row loops.

## Scope

In scope (introduce a `ready_statement`-style private helper, matching `setop`):

- `aggregate.cppm` — has `prepare_bind_extract` + `prepare_bind_having_extract`; both repeat the
  `prepare_cached → check` prefix, and the loop-tail `reset → unexpected` appears 3×. Hoist the shared prefix.
- `erase.cppm` — 7 prepare sites, no helper.
- `update.cppm` — 2 prepare sites, no helper.
- `distinct.cppm` — 1 prepare site, no helper.

Leave alone (already clean — only touch a genuine stray dedup win):

- `select.cppm` — already has `prepare_and_bind` / `bind_where_or_propagate`.
- `insert.cppm` — already has `prepare_and_bind(sql, obj)`.
- `setop.cppm` — the model itself.

Deferred to a follow-up issue (confirmed with user):

- `postgresql_connection.cppm` / `postgresql_statement.cppm` PG prepare/exec paths. Not on the
  benchmarked hot path, so not covered by the bench gate; keep this PR smaller and bench-clean.

Out of scope (Shape B — documented in PR as intentionally untouched):

- `pool.cppm` checkout/try_grow/init (relock + emplace + `core_ref` capture between steps).
- `transaction.cppm::commit()` (side-effecting ROLLBACK-on-failure between check and return).
- `queryset.cppm::capture_operand` (plain string assembly, no `expected`).

## The shared shape (per-file private helper, matching `setop::ready_statement`)

```cpp
[[nodiscard]] auto ready_statement(const std::string& sql /*, bind inputs */)
        -> std::expected<Statement*, Error> {
    auto r = conn_->prepare_cached(sql);
    if (!r) [[unlikely]] return std::unexpected(r.error());
    Statement* stmt = *r;
    stmt->reset();
    if (auto b = bind_all(stmt); !b) [[unlikely]] {
        stmt->reset();
        return std::unexpected(b.error());
    }
    return stmt;
}
```

Each `execute()` / `get()` / `to_sql()` becomes: `ready_statement(...) → check → run_loop(*stmt)`.
**No `and_then` in the row loops.** Loops stay flat (`while (step_raw() == ROW_AVAILABLE)`), untouched.

## Verification gate (A/B per file, strict revert)

1. Confirm full suite green on `develop` first.
2. Build `develop` baseline: `ninja-release`, run `storm_bench --benchmark_repetitions=10` on
   `Storm/(SELECT|INSERT|.*[Aa]ggregate).*`. Record median + cv. (cv ≪ 5 % ⇒ gate resolvable; else jitter.)
3. One file per commit. After each: rebuild Release, re-run the relevant filter, compare median.
   **Revert that file on any median slowdown beyond cv noise.**
4. After all files: full `ctest`, then `ninja-asan-ubsan` + `ninja-tsan` green.

## Branching / process

- Branch: `feature/363-converge-execute-skeleton` (via `gh issue develop`).
- One commit per converted file so a regression is bisectable/revertable.
- Behavior-preserving: existing test suite + sanitizers are the regression net (no new tests needed;
  no error-message or status changes).
- Update docs/agent files only if the execute-path pattern is documented somewhere.

## Definition of done (from issue)

- [ ] Shape-A skeleton hoisted into a shared per-file helper (aggregate, erase, update, distinct).
- [ ] select/insert left clean; setop unchanged (the model).
- [ ] Shape-B paths (pool, commit, capture_operand) untouched — documented in PR.
- [ ] PG paths deferred to a follow-up issue.
- [ ] Release A/B bench attached; no slowdown vs develop (revert any file that regresses).
- [ ] Full suite + ASAN/UBSAN/TSAN green.
- [ ] Title-vs-model reframing documented in PR.
