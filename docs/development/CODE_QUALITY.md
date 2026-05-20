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
| `src/orm/statements/aggregate.cppm` | extracted (this PR) | `append_group_by_tail(sql)` folds the `if constexpr (HasGroupBy) { if (having_expr_) insert_having_clause(sql); append_modifiers(sql); }` block that used to repeat across `execute_where` / `execute_join` / `execute_where_join` |
| `src/db/pool.cppm` | pending | — |
| `src/orm/statements/erase.cppm` | pending | — |
| `src/orm/statements/distinct.cppm` | pending | — |
| `src/orm/statements/setop.cppm` | pending | — |
| `src/db/sqlite.cppm` | pending | — |
| `src/db/postgresql_statement.cppm` | pending | — |

## Accepted duplicates

(none yet — fill in as later PRs land)

## Related issues

- [#264](https://github.com/spiritEcosse/storm/issues/264) — original tech-debt umbrella.
- [#275](https://github.com/spiritEcosse/storm/pull/275) — Phase 2 (postgresql.cppm split).
- [#276](https://github.com/spiritEcosse/storm/pull/276) — rationale rewrite on 14 files.
- [#277](https://github.com/spiritEcosse/storm/issues/277) — Phase 3 (this document).
