# Documentation Staleness Sweep — Design

**Date:** 2026-06-19
**Scope:** Part A of a two-part documentation task. Part A (this spec) aligns
user/dev-facing docs with current code. Part B (documentation reorganization)
is a separate later pass and is **not** covered here.

## Goal

Bring every user/dev-facing doc into agreement with current `src/` code. Every
fix below was verified against the codebase, not inferred from memory.

## Out of scope

- `docs/superpowers/` and `docs/archive/` — left untouched (process artifacts /
  history).
- Structural reorganization (unindexed files, the `development/` grab-bag,
  overlapping performance docs, directory layout) — deferred to Part B.
- `reference/FIELD_TYPES.md:8` — the `storm::meta::` mention is **correct**: it
  documents that the longer spelling still works as an alias after #442. No change.

## Verified staleness inventory

### Category 1 — Stale "3-level caching" claims

L1 (per-`QuerySet`) and L2 (per-`Statement`) caches were removed in #214. Only
the Connection-level prepared-statement cache (`prepare_cached`) remains.
`architecture/STATEMENT_CACHING.md` already documents this correctly and is the
reference these files should agree with.

| File:line | Current text | Fix |
|---|---|---|
| `README.md:28` | "3-level caching achieving near-raw SQLite performance" | "single Connection-level statement cache …" |
| `architecture/OVERVIEW.md:17` | "3-level caching architecture achieving near-raw SQLite performance." | single Connection-level cache |
| `architecture/OVERVIEW.md:220` | "[Statement Caching] - 3-level caching details" | "- single Connection-level cache details" |
| `features/CRUD_OPERATIONS.md:218` + following code block | "UpdateStatement uses the 3-level caching pattern" with a `cached_stmt_ = nullptr` example | Rewrite to describe the Connection-level `prepare_cached`; remove the removed-member example |
| `features/SELECT_QUERIES.md:79–95` | "SelectStatement uses a 3-level caching architecture" with a `select_stmt_` / `get_select_statement()` example | Rewrite to the Connection-level cache; remove the removed-method example |
| `features/SELECT_QUERIES.md:452` | "✅ 3-level caching - Optimal statement reuse" | "✅ Connection-level statement cache - statement reuse" |

### Category 2 — COOKBOOK.md (mostly current, three broken spots)

Verified against `shared/models.h` (`Message` model) and `tests/query/`.

| Line | Current (wrong) | Correct (per code) |
|---|---|---|
| 13, 199 | `[[storm::primary_key, storm::auto_increment]] int id;` | `[[= storm::FieldAttr::primary]] int id{};` |
| 201 | `[[storm::foreign_key<^^Person::id>]] int sender;` | `[[= storm::fk<>]] Person sender;` |
| 205 | `msg_qs.join<Message>().where(...).select();` | `msg_qs.join<^^Message::sender>().where(...).select();` |
| 215 | `.union_with(qs2.where(...))` | `.union_(qs2.where(...))` (`union_with` does not exist; public methods are `union_`, `union_all`, `except_`, `intersect_`) |

### Category 3 — Smaller verified fixes

| File:line | Issue | Fix |
|---|---|---|
| `features/JOIN_OPERATIONS.md:418–420` | example calls `get_select_statement()` (removed in #214) | Update the example to the current execution path |
| `architecture/REFLECTION.md:21` | "More attributes coming (unique, index, foreign_key, etc.)" — `fk` (and m2m, timestamps) now exist | Update the roadmap note to reflect shipped annotations |

## Verification

Docs-only change; no build required. After edits, re-run the audit greps and
expect:

- zero hits for `3-level` / `three-level` outside `STATEMENT_CACHING.md`'s
  history note
- zero `primary_key` / `auto_increment` annotation spellings (`[[storm::…]]`)
- zero `union_with`
- zero `get_select_statement` outside `STATEMENT_CACHING.md`

Commands:

```bash
grep -rn --include='*.md' "3-level\|three-level" docs/ | grep -v superpowers | grep -v archive
grep -rn --include='*.md' "\[\[storm::primary_key\|\[\[storm::foreign_key\|storm::auto_increment" docs/ | grep -v superpowers | grep -v archive
grep -rn --include='*.md' "union_with\|get_select_statement" docs/ | grep -v superpowers | grep -v archive | grep -v STATEMENT_CACHING
```

## Noted gaps (for Part B, not fixed here)

- Set operations (`union_`, `union_all`, `except_`, `intersect_`) are documented
  only in `COOKBOOK.md`; there is no `features/` page for them.
- 8 unindexed files, the `development/` grab-bag, and three overlapping
  performance docs are organizational problems addressed in Part B.
