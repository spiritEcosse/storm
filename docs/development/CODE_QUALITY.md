# Code Quality — `LINT-EXCLUDE-FILE` dispositions

This document tracks the audit decisions taken under [#277](https://github.com/spiritEcosse/storm/issues/277) (Phase 3 — Audit & extract real duplicates across `src/*.cppm`).

The repo-level lint hook (`smell_checks.py`) supports a per-file opt-out via a `// LINT-EXCLUDE-FILE: <tag-list>` directive. The audit policy is:

1. **Statement-class files** (`base`, `insert`, `select`, `update`, `aggregate`, `where`) — *single cohesive class template, thresholds intentionally relaxed (see #264 finding). `file-size, complexity, length` excludes are accepted; `duplicate` must be removed by extracting all real duplicate blocks.*
2. **Multi-type files** (`sqlite`, `pool`, `schema`, `utilities`, `setop`, `erase`, `distinct`, `join`, `postgresql_statement`) — *`duplicate` is removed by extracting real duplicates or by documenting why a residual block is intentional below.*

When a file's `duplicate` tag is removed by extraction, its `LINT-EXCLUDE-FILE` line carries a one-line trail comment naming the helper introduced. When a block is intentionally accepted, the rationale lives in this file under the **Accepted duplicates** section so future audits can see the decision rather than re-litigating it.

## Per-file dispositions

| File | Status | Extraction / `accept →` rationale |
|---|---|---|
| `src/orm/statements/insert.cppm` | extracted (#278) | `build_insert_sql_array_impl<bool>`, `to_sql_impl`, single/bulk binder split |
| `src/orm/schema.cppm` | extracted (#279) | `append_index_sql` helper for the shared `CREATE INDEX` tail |
| `src/orm/statements/join.cppm` | extracted (#280) | `for_each_fk_field<F>` consteval helper |
| `src/orm/statements/select.cppm` | extracted (#281) | `build_sql()` reused by `to_sql`/`prepare_statement`/`rows_generator`; `QueryBase::forward()` collapses the 5-arg forwarder block in `Query`/`FirstQuery`/`GetQuery`; `make_first_or_get<Proxy>` consolidates the `query_first`/`query_get` bodies; coroutine step-loop in `rows_generator` no longer branches before the loop; `step_first_row()` helper de-duplicates the first-step triage in `execute_single_row` / `execute_exact_one` |
| `src/orm/statements/base.cppm` | extracted (#283) | `for_each_field_name<SkipPK>` consteval iterator drives both the size-calculator and the list-builder; `bind_bulk_objects_impl<SkipPK>` unifies the two bulk binders; `bind_expr_or_reset` shares the bind-or-reset tail of `bind_where_params` / `bind_having_params` |
| `src/orm/where.cppm` | extracted (#284) | `BindParamsVisitor` casts the type-erased statement pointer once and calls each Expr's small `bind_impl(StmtType*, int&)`. `make_null_check_expr(name, is_null)` consolidates the `is_null`/`is_not_null` bodies of `CollatedField` and `Field` |
| `src/orm/statements/update.cppm` | extracted (#285) | `ensure_cached_stmt()` and `reset_bind_execute()` member helpers replace the inline cache-prepare + reset/bind/execute blocks repeated across `execute(span)` / `execute_single_row` / `execute_single_optimized`; `QueryBase::sql()` consolidates the `static auto sql() -> std::string { return update_sql_string; }` body shared by `SingleQuery` and `BulkQuery` proxies |
| `src/orm/statements/aggregate.cppm` | extracted (#286) | `append_group_by_tail(sql)` folds the `if constexpr (HasGroupBy) { if (having_expr_) insert_having_clause(sql); append_modifiers(sql); }` block that used to repeat across `execute_where` / `execute_join` / `execute_where_join` |
| `src/db/pool.cppm` | extracted (#287) | `count_entries(pred)` helper takes the lock and counts entries matching a predicate. `available()` and `in_use()` collapse to one-line predicate calls |
| `src/orm/statements/erase.cppm` | extracted (#288) | `delete_prefix_size()` + `append_delete_prefix(buf)` consteval helpers share the `"DELETE FROM <table> WHERE <pk_name>"` prefix between the single-row and bulk DELETE SQL builders — only the tail differs (`" = ?"` vs `" IN ("`) |
| `src/orm/statements/distinct.cppm` | extracted (#289) | `build_field_list_with_prefix<Extra>(prefix, seq)` consteval helper shared by `build_field_list_constexpr` (no prefix) and `build_join_field_list_constexpr` (`"t1."` per field) |
| `src/orm/statements/setop.cppm` | extracted (#290) | `add_operand(op, type)` folds the `union_`/`union_all`/`except_`/`intersect_` bodies; `ready_statement()` shares the prepare/reset/bind dance between `execute()` and `to_sql()` |
| `src/db/sqlite.cppm` | tag dropped (#291) | No duplicate blocks today — the directive was carried over from earlier `LINT-EXCLUDE-FILE` rollout but had no current effect |
| `src/db/postgresql_statement.cppm` | extracted (this PR) | `bind_text_value(idx, std::string)` helper folds the `ensure_param_slot → assign → update_param_ptrs` dance shared by `bind_int` / `bind_int64` / `bind_text` |

## Accepted duplicates

(none yet — fill in as later PRs land)

## Related issues

- [#264](https://github.com/spiritEcosse/storm/issues/264) — original tech-debt umbrella.
- [#275](https://github.com/spiritEcosse/storm/pull/275) — Phase 2 (postgresql.cppm split).
- [#276](https://github.com/spiritEcosse/storm/pull/276) — rationale rewrite on 14 files.
- [#277](https://github.com/spiritEcosse/storm/issues/277) — Phase 3 (this document).
- [#293](https://github.com/spiritEcosse/storm/issues/293) — Phase 4 (hook tuning & stop-gap removal).

## Phase 4 audit (#293) — PR-level outcomes

Phase 3 (#277) closed 2026-05-20 with 14 merged PRs (#278–#292). The hook's thresholds drove ~117 real refactors across `src/*.cppm`. This table is the evidence trail.

| PR | File | Hook tags reported pre-PR | Outcome | Helper(s) introduced |
|---|---|---|---|---|
| #278 | `src/orm/statements/insert.cppm` | `duplicate` | extracted | `build_insert_sql_array_impl<bool>`, `to_sql_impl`, `bind_single` / `bind_bulk` |
| #279 | `src/orm/schema.cppm` | `duplicate` | extracted | `append_index_sql` for the shared `CREATE INDEX` tail |
| #280 | `src/orm/statements/join.cppm` | `duplicate` | extracted | `for_each_fk_field<F>` consteval helper |
| #281 | `src/orm/statements/select.cppm` | `duplicate` | extracted | `build_sql`, `QueryBase::forward`, `make_first_or_get<Proxy>`, coroutine step-loop flattening, `step_first_row` |
| #283 | `src/orm/statements/base.cppm` | `duplicate` | extracted | `for_each_field_name<SkipPK>`, `bind_bulk_objects_impl<SkipPK>`, `bind_expr_or_reset` |
| #284 | `src/orm/where.cppm` | `duplicate` | extracted | `BindParamsVisitor`, `make_null_check_expr` |
| #285 | `src/orm/statements/update.cppm` | `duplicate` | extracted | `ensure_cached_stmt`, `reset_bind_execute`, `QueryBase::sql` |
| #286 | `src/orm/statements/aggregate.cppm` | `duplicate` | extracted | `append_group_by_tail` |
| #287 | `src/db/pool.cppm` | `duplicate` | extracted | `count_entries(pred)` |
| #288 | `src/orm/statements/erase.cppm` | `duplicate` | extracted | `delete_prefix_size`, `append_delete_prefix` |
| #289 | `src/orm/statements/distinct.cppm` | `duplicate` | extracted | `build_field_list_with_prefix<Extra>` |
| #290 | `src/orm/statements/setop.cppm` | `duplicate` | extracted | `add_operand`, `ready_statement` |
| #291 | `src/db/sqlite.cppm` | `duplicate` | tag dropped | none — no real blocks under the tag |
| #292 | `src/db/postgresql_statement.cppm` | `duplicate` | extracted | `bind_text_value` |

**Bench / sanitizer verdict (all 14 PRs):** no regression on `develop` per per-PR CI runs gated by CLAUDE.md verification rules.

**Tally:** 13 extractions, 1 tag-drop, 0 "accept the duplicate" decisions. The hook's `duplicate` detection produced zero false-positive blocks on Storm — every reported duplicate represented a real, extractable pattern.

## Phase 4 threshold decisions

For each constant in `~/.claude/hooks/smell_types.py`, the question is: *did Phase 3 show the threshold was calibrated correctly?* The decisions below are based on the audit table above.

- **`DUPLICATE_MIN_LINES = 6` — keep.** The 6-line window flagged 14 file-clusters across `src/*.cppm`. All 14 led to either a real extraction (13) or a no-op tag drop (1, `sqlite.cppm` — no blocks survived). Zero blocks needed a "this is fine as-is" accept. Raising to 10 (as suggested in #264) would have missed real extractions like `count_entries` in `pool.cppm` (6-line predicate-count pattern). Keep.

- **`DUPLICATE_MIN_OCCURRENCES = 2` — keep.** Storm's duplicates are usually 2-occurrence pairs (single-row vs bulk variants, two binder overloads). Bumping to 3 would have missed every Phase 3 extraction.

- **`MAX_FILE_LINES = 600` — keep.** Six files (`insert`, `select`, `base`, `where`, `aggregate`, …) still exceed 600 lines and carry `file-size` exclusions in `.lint-skip`. The Phase 2 finding (#264 comment 4462154303) documented why splitting these hurts perf or breaks ADL. The threshold is correctly catching them; the project-local `.lint-skip` is correctly opting out per-file.

- **`MAX_COMPLEXITY = 10` — keep.** Seven files carry `complexity` exclusions in `.lint-skip`, mostly for consteval reflection helpers that walk `nonstatic_data_members_of`. The threshold flags real complexity; the exclusions are scoped to documented exceptions.

- **`MAX_FUNCTION_LINES = 60` — keep.** Five files carry `length` exclusions. Same pattern as complexity — consteval and large constructor lists in reflection-driven code legitimately exceed 60 lines. The threshold is right; the exemptions are right.

- **`MAX_NESTING_DEPTH = 4` — keep.** Phase 3 reported zero `nesting` violations on `src/*.cppm`. No exclusions exist for this tag. No tuning needed.

- **`MAX_PARAMETERS = 6` — keep.** Phase 3 reported zero `parameters` violations on `src/*.cppm`. No exclusions exist. No tuning needed.

**Conclusion:** all 7 constants stay at their current values. The stop-gap `LINT-EXCLUDE-FILE` mechanism in the hook is removed and replaced by `.lint-skip` at the repo root.

## Phase 5 audit (#295) — `.lint-skip` shrink

Phase 4 ended with 9 entries in `.lint-skip` across three tags (`file-size`, `complexity`, `length`). Phase 5's policy: for every `(file, tag)` pair, either remove the tag (refactor lands or threshold no longer fires) or document a `Kept →` reason here.

### Stale-entry sweep — first PR

Re-running the hook against the Phase 4 `.lint-skip` showed several entries no longer fire **at the current threshold values** (`MAX_COMPLEXITY = 10`, `MAX_FUNCTION_LINES = 60`, `MAX_FILE_LINES = 600`). These were dropped without source changes. The headroom column shows how close the file sits to today's threshold — if Phase 4's threshold decisions are ever revisited downward, these files may need their tags back or a real refactor.

| File | Tag | Disposition | Evidence (today's thresholds) | Headroom |
|---|---|---|---|---|
| `src/orm/statements/update.cppm` | `complexity` | **dropped (entire line removed)** | Lizard reports max CCN = 7. Phase 3 PR #285 already extracted helpers that reduced complexity; the tag was carried over from before that work landed | 3 CCN points |
| `src/orm/where.cppm` | `file-size` | **dropped (entire line removed)** | File is 574 lines. Phase 3 PR #284 trimmed it below 600 | 26 lines |
| `src/orm/where.cppm` | `complexity` | **dropped** | Lizard reports zero functions over CCN 10 | N/A (no offender today) |
| `src/orm/statements/insert.cppm` | `complexity` | **dropped** | Lizard reports max CCN = 7 (Phase 3 PR #278 already split the big consteval builder) | 3 CCN points |
| `src/orm/statements/insert.cppm` | `length` | **dropped** | Lizard reports max function = 33 lines | 27 lines |

After this sweep, `.lint-skip` shrank from 9 entries (across 9 files) to 7 entries (across 7 files), and the project's full hook pass is still clean against today's thresholds.

### Real-refactor sub-PRs

| PR | File | Tag(s) dropped | Helper(s) introduced | Before → after |
|---|---|---|---|---|
| TBD | `src/db/pool.cppm` | `complexity`, `length` (entire line removed) | `try_grow(lock)` + `wait_for_idle(lock, deadline)` extract grow- and wait-paths out of `checkout` | `checkout`: CCN 13 → 5, length 70 → 19 |

### Remaining entries (Phase 5 work plan)

Each row below is a separate sub-PR per the issue's "one PR per file per tag" rule.

| File | Tag | Hook says | Phase 5 sub-PR target |
|---|---|---|---|
| `src/orm/statements/select.cppm` | `complexity` | 1 function @ CCN 19 | refactor |
| `src/orm/statements/select.cppm` | `length` | 1 function @ 66 lines | refactor (likely same fn as complexity) |
| `src/orm/statements/select.cppm` | `file-size` | 612 lines | bench-gated `Kept →` candidate (Phase 2 finding) |
| `src/orm/statements/base.cppm` | `complexity` | 1 function @ CCN 40 | refactor |
| `src/orm/statements/base.cppm` | `length` | 1 function @ 130 lines | refactor |
| `src/orm/statements/base.cppm` | `file-size` | 832 lines | bench-gated `Kept →` candidate |
| `src/orm/statements/aggregate.cppm` | `file-size` | 693 lines | refactor candidate (no Phase 2 finding) |
| `src/orm/schema.cppm` | `complexity` | 1 function @ CCN 63 | refactor |
| `src/orm/schema.cppm` | `length` | 1 function @ 146 lines | refactor |
| `src/orm/utilities.cppm` | `complexity` | 1 function @ CCN 39 | refactor |
| `src/orm/utilities.cppm` | `length` | 1 function @ 114 lines | refactor |
