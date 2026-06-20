# Documentation Restructure — Design (Part B)

**Date:** 2026-06-20
**Scope:** Part B of the docs task. Part A (staleness sweep) is complete on the
same branch (`feature/docs-staleness-sweep`). This part reorganizes the doc tree
into an audience-split structure and re-indexes every file. Continues on the
SAME branch (one PR delivers both true/stale fixes and the new structure).

## Goal

Replace the flat, partly-unindexed `docs/` layout (a 19-file `development/`
grab-bag, 3 overlapping performance docs, 5 unindexed files, a 1-file
`reference/`) with a clear **users vs. contributors** split, and make
`mkdocs.yml`'s `nav:` and every cross-link agree with the new paths.

## Out of scope

- `docs/superpowers/` and `docs/archive/` — left in place. (`archive/` keeps its
  current `mkdocs.yml` nav entry unchanged.)
- Hardcoded performance NUMBERS (`74%`, `13.07M rows/sec`, `% of raw`) — a
  separate deferred follow-up. Not touched here.
- Content rewrites beyond the one explicit merge (3 perf docs → 1) and the
  link/path updates that moves require.

## Constraints discovered

- **Live MkDocs site.** `mkdocs.yml` has a `nav:` block; `.github/workflows/docs.yml`
  runs `mkdocs gh-deploy --force` on push to `develop` when `docs/` or
  `mkdocs.yml` changes. CI does **not** use `--strict`, so broken links do NOT
  fail CI — they silently ship as broken pages. **Link integrity must be
  verified locally** (`mkdocs build --strict`, or a markdown link checker)
  before merge.
- **Inbound references that break on move:** ~102 relative `.md` links between
  docs, 15 `docs/...md` references in `CLAUDE.md`/`README.md`, and every path in
  `mkdocs.yml`'s `nav:`. (No `src/` code comments reference doc paths — verified
  0 hits.)
- The current `nav:` already OMITS several files (CONNECTION_TUNING,
  REFERENTIAL_INTEGRITY, MIGRATIONS, GETTING_STARTED, PRE_COMMIT, CODE_QUALITY,
  MSAN_LIBC_FIX) — the new `nav:` must list every kept file.

## Target structure

```
docs/
  README.md                          # rewritten index; every file linked
  guide/                             # USERS of Storm
    COOKBOOK.md
    features/
      CRUD_OPERATIONS.md  SELECT_QUERIES.md  WHERE_CLAUSES.md
      JOIN_OPERATIONS.md  BATCH_OPERATIONS.md
      REFERENTIAL_INTEGRITY.md  CONNECTION_TUNING.md
    reference/
      FIELD_TYPES.md  MIGRATIONS.md
  internals/                         # CONTRIBUTORS
    architecture/
      OVERVIEW.md  DESIGN_DECISIONS.md  REFLECTION.md  SQL_GENERATION.md
      MODULE_SYSTEM.md  STATEMENT_CACHING.md  COMPILE_TIME_VS_RUNTIME.md
    building/
      GETTING_STARTED.md  COMMON_TASKS.md  ADDING_FEATURES.md
      PRE_COMMIT.md  FORMATTING.md
    testing/
      TESTING.md  CODE_COVERAGE.md  SANITIZERS.md  FUZZING.md  MSAN_LIBC_FIX.md
    performance/
      PERFORMANCE.md                 # merged: GUIDELINES + TIPS + TESTING
      BENCHMARK_DASHBOARD.md  JOIN_ANALYSIS.md  DISTINCT_ANALYSIS.md
    compiler/
      COMPILER_ISSUES.md  COMPILER_ATTRIBUTES.md
      CPP26_CODING_STANDARDS.md  CODE_QUALITY.md
  superpowers/   archive/            # untouched
```

### Resolved placements
- `GETTING_STARTED.md` → `internals/building/` (it is clang-p2996/Docker
  toolchain setup, not an end-user query tutorial). `guide/` entry point is
  `COOKBOOK.md`.
- `MIGRATIONS.md` → `guide/reference/` (user-facing: run Atlas against your schema).
- `JOIN_ANALYSIS.md`, `DISTINCT_ANALYSIS.md` → `internals/performance/`.
- `COMMON_TASKS.md`, `ADDING_FEATURES.md` → `internals/building/`.

### The one content merge
`PERFORMANCE_GUIDELINES.md` + `PERFORMANCE_TIPS.md` + `PERFORMANCE_TESTING.md`
→ `internals/performance/PERFORMANCE.md`. Concatenate under clear `##` sections
(Guidelines / Tips / Testing), de-duplicate only obvious overlap, keep all
non-duplicate content. Do NOT alter perf numbers. (All three are already
caching-clean after Part A.)

## File moves (37 files → new paths)

Use `git mv` (preserves history). Source → destination, grouped:

- `COOKBOOK.md` → `guide/COOKBOOK.md`
- `features/*` (7) → `guide/features/*`
- `reference/FIELD_TYPES.md` → `guide/reference/FIELD_TYPES.md`
- `development/MIGRATIONS.md` → `guide/reference/MIGRATIONS.md`
- `architecture/*` (7) → `internals/architecture/*` (unchanged names)
- `development/{GETTING_STARTED,COMMON_TASKS,ADDING_FEATURES,PRE_COMMIT,FORMATTING}.md`
  → `internals/building/*`
- `development/{TESTING,CODE_COVERAGE,SANITIZERS,FUZZING,MSAN_LIBC_FIX}.md`
  → `internals/testing/*`
- `development/BENCHMARK_DASHBOARD.md` → `internals/performance/BENCHMARK_DASHBOARD.md`
- `benchmarks/{JOIN_ANALYSIS,DISTINCT_ANALYSIS}.md` → `internals/performance/*`
- `development/{COMPILER_ISSUES,COMPILER_ATTRIBUTES,CPP26_CODING_STANDARDS,CODE_QUALITY}.md`
  → `internals/compiler/*`
- `development/{PERFORMANCE_GUIDELINES,PERFORMANCE_TIPS,PERFORMANCE_TESTING}.md`
  → merged into `internals/performance/PERFORMANCE.md` (then `git rm` the three originals)

Empty dirs (`features/`, `reference/`, `benchmarks/`, `development/`) are removed
by the moves.

## Link & reference updates

1. **Internal cross-links (~102):** every relative `.md` link must resolve from
   its file's NEW location. Recompute relative paths (e.g. a `guide/features/`
   file linking to `internals/architecture/STATEMENT_CACHING.md` becomes
   `../../internals/architecture/STATEMENT_CACHING.md`).
2. **`mkdocs.yml` `nav:`** — rewrite to the new tree, with a section per top
   group (Guide → Cookbook/Features/Reference; Internals →
   Architecture/Building/Testing/Performance/Compiler). List EVERY kept file
   (fix the current omissions). Keep the existing Archive entry as-is. Merge the
   three perf entries into one "Performance" page.
3. **`docs/README.md`** — rewrite the index to mirror the new structure; link
   every file (including the previously-unindexed COOKBOOK, MIGRATIONS, FUZZING,
   CODE_QUALITY, MSAN_LIBC_FIX, GETTING_STARTED, PRE_COMMIT). Fix the stale
   "3-level caching" index line (still says it) → single Connection-level cache.
4. **`CLAUDE.md` (15 refs)** — update each `docs/...md` path to its new location.
5. **`benchmarks/README.md`** and any other root-level doc that links into
   `docs/` — grep and update.

## Verification

1. **No dangling links:** `mkdocs build --strict` (install `mkdocs-material`
   locally) must succeed with zero warnings. If mkdocs can't be installed, run a
   markdown link-checker over `docs/` and manually confirm the 15 CLAUDE.md refs
   resolve.
2. **No orphaned files:** every `.md` under `docs/guide` and `docs/internals`
   appears exactly once in `mkdocs.yml` `nav:` and once in `README.md`.
3. **History preserved:** `git log --follow` on a moved file shows pre-move
   history (confirms `git mv`, not delete+create).
4. **No stale paths remain:** `grep -rn 'docs/development/\|docs/benchmarks/\|docs/features/\|docs/reference/' CLAUDE.md README.md benchmarks/README.md docs/` returns nothing (all updated to new paths), except inside `docs/superpowers/` and `docs/archive/`.
5. **Part A staleness still clean:** re-run the Part A audit greps (no `3-level`,
   no stale annotations) — moves must not reintroduce anything.

## Risk notes

- CI does not gate on link integrity (`gh-deploy --force`, no `--strict`), so the
  local `mkdocs build --strict` is the ONLY safety net — do not skip it.
- The merge (3 perf docs → 1) is the only place content is rewritten; everything
  else is moves + link math. Keep them in separate commits so a bad merge is
  easy to isolate from the mechanical moves.
