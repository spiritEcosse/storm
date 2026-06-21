# Documentation Restructure Implementation Plan (Part B)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize `docs/` into an audience-split tree (`guide/` for users, `internals/` for contributors), rewrite `mkdocs.yml` `nav:` and `README.md`, fix all cross-links and external refs, and merge the three performance docs into one — verified by `mkdocs build --strict`.

**Architecture:** Pure file moves (`git mv`, history-preserving) + link math + one content merge. No code. Continues on branch `feature/docs-staleness-sweep` (same PR as Part A). The verification gate is a local mkdocs strict build (CI uses `gh-deploy --force`, NOT `--strict`, so CI will NOT catch broken links — local strict build is the only safety net).

**Tech Stack:** Markdown, `git mv`, MkDocs Material. Local mkdocs already installed at `/tmp/mkdocs-venv/bin/mkdocs` (v1.6.1).

## Global Constraints

- Work on branch `feature/docs-staleness-sweep` in worktree `/home/ihor/projects/storm/storm_develop/.claude/worktrees/feature-docs-staleness-sweep`. Do NOT switch branches.
- `docs/superpowers/` and `docs/archive/` are NOT moved or renamed. `archive/`'s existing `mkdocs.yml` nav entry stays.
- Do NOT alter hardcoded performance NUMBERS (`74%`, `13.07M rows/sec`, `% of raw`, efficiency) — deferred follow-up.
- Use `git mv` for every move (preserve history). Stage only the files each task names; never `git add .superpowers/`.
- Pre-commit hook skips checks for docs-only commits — expected. NEVER `--no-verify`.
- Verification command (run from worktree root):
  `/tmp/mkdocs-venv/bin/mkdocs build --strict --site-dir /tmp/mkdocs-site 2>&1 | grep -E 'WARNING|ERROR'`
  A clean run prints NOTHING and exits 0. (INFO "not in nav" lines for `superpowers/`/`archive/` are noise, not failures — only WARNING/ERROR abort `--strict`.)
- Baseline (current tree) has 2 PRE-EXISTING `--strict` warnings that Tasks fix in passing:
  1. `development/PRE_COMMIT.md:122` link `../../CLAUDE.md` (unresolvable; CLAUDE.md is outside docs/).
  2. `COOKBOOK.md:122` bad anchor `reference/FIELD_TYPES.md#automatic-timestamps-auto_create--auto_update`.

## Target structure (reference for all tasks)

```
docs/
  README.md
  guide/
    COOKBOOK.md
    features/  {CRUD_OPERATIONS, SELECT_QUERIES, WHERE_CLAUSES, JOIN_OPERATIONS,
                BATCH_OPERATIONS, REFERENTIAL_INTEGRITY, CONNECTION_TUNING}.md
    reference/ {FIELD_TYPES, MIGRATIONS}.md
  internals/
    architecture/ {OVERVIEW, DESIGN_DECISIONS, REFLECTION, SQL_GENERATION,
                   MODULE_SYSTEM, STATEMENT_CACHING, COMPILE_TIME_VS_RUNTIME}.md
    building/     {GETTING_STARTED, COMMON_TASKS, ADDING_FEATURES, PRE_COMMIT, FORMATTING}.md
    testing/      {TESTING, CODE_COVERAGE, SANITIZERS, FUZZING, MSAN_LIBC_FIX}.md
    performance/  {PERFORMANCE (merged), BENCHMARK_DASHBOARD, JOIN_ANALYSIS, DISTINCT_ANALYSIS}.md
    compiler/     {COMPILER_ISSUES, COMPILER_ATTRIBUTES, CPP26_CODING_STANDARDS, CODE_QUALITY}.md
  superpowers/  archive/   (untouched)
```

---

### Task 1: Move all files with `git mv` (no link edits yet)

**Files:** 34 moves under `docs/` (the 3 perf docs are handled in Task 2, not moved here).

**Interfaces:**
- Produces: the final file paths every later task links to. After this task the tree matches the target structure EXCEPT `internals/performance/PERFORMANCE.md` (Task 2) and all links are temporarily broken (fixed Tasks 3-6).

- [ ] **Step 1: Create the new directories**

```bash
cd /home/ihor/projects/storm/storm_develop/.claude/worktrees/feature-docs-staleness-sweep
mkdir -p docs/guide/features docs/guide/reference
mkdir -p docs/internals/architecture docs/internals/building docs/internals/testing docs/internals/performance docs/internals/compiler
```

- [ ] **Step 2: git mv the guide files**

```bash
git mv docs/COOKBOOK.md docs/guide/COOKBOOK.md
git mv docs/features/CRUD_OPERATIONS.md docs/features/SELECT_QUERIES.md docs/features/WHERE_CLAUSES.md docs/features/JOIN_OPERATIONS.md docs/features/BATCH_OPERATIONS.md docs/features/REFERENTIAL_INTEGRITY.md docs/features/CONNECTION_TUNING.md docs/guide/features/
git mv docs/reference/FIELD_TYPES.md docs/guide/reference/FIELD_TYPES.md
git mv docs/development/MIGRATIONS.md docs/guide/reference/MIGRATIONS.md
```

- [ ] **Step 3: git mv the internals files**

```bash
git mv docs/architecture/OVERVIEW.md docs/architecture/DESIGN_DECISIONS.md docs/architecture/REFLECTION.md docs/architecture/SQL_GENERATION.md docs/architecture/MODULE_SYSTEM.md docs/architecture/STATEMENT_CACHING.md docs/architecture/COMPILE_TIME_VS_RUNTIME.md docs/internals/architecture/
git mv docs/development/GETTING_STARTED.md docs/development/COMMON_TASKS.md docs/development/ADDING_FEATURES.md docs/development/PRE_COMMIT.md docs/development/FORMATTING.md docs/internals/building/
git mv docs/development/TESTING.md docs/development/CODE_COVERAGE.md docs/development/SANITIZERS.md docs/development/FUZZING.md docs/development/MSAN_LIBC_FIX.md docs/internals/testing/
git mv docs/development/BENCHMARK_DASHBOARD.md docs/internals/performance/
git mv docs/benchmarks/JOIN_ANALYSIS.md docs/benchmarks/DISTINCT_ANALYSIS.md docs/internals/performance/
git mv docs/development/COMPILER_ISSUES.md docs/development/COMPILER_ATTRIBUTES.md docs/development/CPP26_CODING_STANDARDS.md docs/development/CODE_QUALITY.md docs/internals/compiler/
```

- [ ] **Step 4: Also move the MSAN patch file if present (non-.md asset referenced by MSAN_LIBC_FIX)**

```bash
# fix_msan_libc_string.patch lives in development/ next to MSAN_LIBC_FIX.md
git mv docs/development/fix_msan_libc_string.patch docs/internals/testing/ 2>/dev/null || echo "no patch file to move"
```

- [ ] **Step 5: Verify the old dirs are now empty of .md (only the 3 perf docs remain in development/)**

```bash
ls docs/features docs/reference docs/benchmarks 2>/dev/null    # expect: empty or 'No such file'
ls docs/development/                                            # expect: ONLY PERFORMANCE_GUIDELINES.md, PERFORMANCE_TIPS.md, PERFORMANCE_TESTING.md
```
Expected: `development/` holds only the 3 perf docs; `features/`, `reference/`, `benchmarks/` are empty (rmdir them):
```bash
rmdir docs/features docs/reference docs/benchmarks 2>/dev/null || true
```

- [ ] **Step 6: Commit the moves (links intentionally still broken; mkdocs NOT run yet)**

```bash
git add -A docs/
git commit -m "docs(restructure): git mv files into guide/ and internals/ tree (#Part B)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Merge the 3 performance docs into one PERFORMANCE.md

**Files:**
- Create: `docs/internals/performance/PERFORMANCE.md`
- Remove: `docs/development/PERFORMANCE_GUIDELINES.md`, `docs/development/PERFORMANCE_TIPS.md`, `docs/development/PERFORMANCE_TESTING.md`

**Interfaces:**
- Consumes: the three source files' content (all already caching-clean from Part A).
- Produces: `internals/performance/PERFORMANCE.md` — the single perf page the nav (Task 5) and README (Task 4) link to.

- [ ] **Step 1: Read all three source files fully**

```bash
wc -l docs/development/PERFORMANCE_GUIDELINES.md docs/development/PERFORMANCE_TIPS.md docs/development/PERFORMANCE_TESTING.md
```
Read each in full to understand their sections.

- [ ] **Step 2: Compose the merged file**

Create `docs/internals/performance/PERFORMANCE.md` with this top-level structure:

```markdown
# Performance

Storm targets ≥95% of raw SQLite efficiency. This page collects the
performance guidelines, hot-path tips, and the benchmarking/testing workflow.

## Guidelines
<full content of PERFORMANCE_GUIDELINES.md, minus its H1 title>

## Tips
<full content of PERFORMANCE_TIPS.md, minus its H1 title>

## Testing & Benchmarking
<full content of PERFORMANCE_TESTING.md, minus its H1 title>
```

Rules: demote each source file's `#` H1 to fit under the new `##` sections (its existing `##` become `###`, `###` become `####`). Keep ALL non-duplicate content verbatim. Do NOT alter any performance number. If two source files state the exact same fact (e.g. the "≥95% target"), keep it once in the intro and drop the duplicate.

- [ ] **Step 3: Remove the three originals**

```bash
git rm docs/development/PERFORMANCE_GUIDELINES.md docs/development/PERFORMANCE_TIPS.md docs/development/PERFORMANCE_TESTING.md
```

- [ ] **Step 4: Confirm development/ is now empty and remove it**

```bash
ls docs/development/ 2>/dev/null && rmdir docs/development 2>/dev/null || echo "development/ gone"
```
Expected: `development/` no longer exists.

- [ ] **Step 5: Verify the merged file has all three sections and balanced fences**

```bash
grep -n '^## Guidelines\|^## Tips\|^## Testing' docs/internals/performance/PERFORMANCE.md   # 3 hits
test $(($(grep -c '```' docs/internals/performance/PERFORMANCE.md) % 2)) -eq 0 && echo "fences balanced" || echo "FENCE IMBALANCE"
```

- [ ] **Step 6: Commit**

```bash
git add docs/internals/performance/PERFORMANCE.md
git add -A docs/development 2>/dev/null || true
git commit -m "docs(perf): merge GUIDELINES + TIPS + TESTING into one PERFORMANCE.md

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Fix all internal cross-links inside docs/ (relative path recompute)

**Files:** every `.md` under `docs/guide/` and `docs/internals/` that contains a relative `.md` link (~102 links total across the tree).

**Interfaces:**
- Consumes: the final paths from Tasks 1-2.
- Produces: a tree where every intra-docs link resolves. (mkdocs `--strict` in Task 7 is the proof.)

- [ ] **Step 1: List every relative .md link and its source file**

```bash
grep -rnoE '\]\([^)]*\.md[^)#]*(#[^)]*)?\)' docs/guide docs/internals --include='*.md'
```
This is the worklist. Each link must be recomputed from the SOURCE file's NEW directory to the TARGET file's NEW path.

- [ ] **Step 2: Recompute and fix each link**

For each link, determine the target's new location (per the target structure) and write the correct relative path from the source file's directory. Common cases:
- `guide/features/X.md` → another `guide/features/Y.md`: `Y.md` (same dir, unchanged).
- `guide/features/X.md` → `internals/architecture/STATEMENT_CACHING.md`: `../../internals/architecture/STATEMENT_CACHING.md`.
- `guide/COOKBOOK.md` → `guide/reference/FIELD_TYPES.md`: `reference/FIELD_TYPES.md`.
- `internals/<sub>/X.md` → `internals/<othersub>/Y.md`: `../<othersub>/Y.md`.
- Any link to one of the 3 merged perf docs (`PERFORMANCE_GUIDELINES.md` etc.) → `../performance/PERFORMANCE.md` (adjust `../` depth per source location); if it linked to a specific section, point at `PERFORMANCE.md#guidelines` / `#tips` / `#testing-benchmarking`.
- Any link to a moved file (e.g. `development/TESTING.md`, `benchmarks/JOIN_ANALYSIS.md`, `reference/FIELD_TYPES.md`) → its new path.

- [ ] **Step 3: Fix the pre-existing COOKBOOK bad anchor (now in guide/COOKBOOK.md)**

The link `reference/FIELD_TYPES.md#automatic-timestamps-auto_create--auto_update` has a wrong anchor. The real heading is `## Automatic Timestamps (\`auto_create\` / \`auto_update\`)`. Replace the whole link target with the section link WITHOUT a fragile anchor: `reference/FIELD_TYPES.md` (drop the `#...` fragment), OR use the correct mkdocs slug if verified. Simplest safe fix: drop the fragment.

- [ ] **Step 4: Fix the pre-existing PRE_COMMIT CLAUDE.md link (now in internals/building/PRE_COMMIT.md)**

Replace `[CLAUDE.md](../../CLAUDE.md)` with the absolute GitHub URL used elsewhere: `[CLAUDE.md](https://github.com/spiritEcosse/storm/blob/develop/CLAUDE.md)`.

- [ ] **Step 5: Grep for any surviving old-path link fragments**

```bash
grep -rnE '\]\([^)]*(development/|features/|benchmarks/|architecture/[A-Z]|reference/[A-Z]|PERFORMANCE_GUIDELINES|PERFORMANCE_TIPS|PERFORMANCE_TESTING)' docs/guide docs/internals --include='*.md'
```
Expected: NOTHING (every old path rewritten). Investigate and fix any hit.

- [ ] **Step 6: Commit**

```bash
git add docs/guide docs/internals
git commit -m "docs(restructure): repoint all internal cross-links to new paths

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Rewrite docs/README.md as the new index

**Files:** Modify `docs/README.md`.

**Interfaces:**
- Consumes: final tree.
- Produces: an index linking EVERY kept file under its new path.

- [ ] **Step 1: Rewrite README.md**

Structure it to mirror the tree, two top sections (Guide, Internals), linking every file:
- **Guide:** Cookbook (`guide/COOKBOOK.md`); Features (the 7 `guide/features/*`); Reference (`guide/reference/FIELD_TYPES.md`, `guide/reference/MIGRATIONS.md`).
- **Internals:** Architecture (7); Building (`GETTING_STARTED`, `COMMON_TASKS`, `ADDING_FEATURES`, `PRE_COMMIT`, `FORMATTING`); Testing (`TESTING`, `CODE_COVERAGE`, `SANITIZERS`, `FUZZING`, `MSAN_LIBC_FIX`); Performance (`PERFORMANCE.md`, `BENCHMARK_DASHBOARD`, `JOIN_ANALYSIS`, `DISTINCT_ANALYSIS`); Compiler (`COMPILER_ISSUES`, `COMPILER_ATTRIBUTES`, `CPP26_CODING_STANDARDS`, `CODE_QUALITY`).
- Keep the existing top links (Main Project Guide → the GitHub CLAUDE.md URL; Benchmarks → benchmarks/README.md URL).
- Ensure the Statement Caching line says "Single Connection-level statement cache" (NOT "3-level") — carry the Part A fix forward.
- All links are relative to `docs/README.md` (e.g. `guide/features/CRUD_OPERATIONS.md`, `internals/architecture/OVERVIEW.md`).

- [ ] **Step 2: Verify every kept .md is linked exactly once**

```bash
for f in $(find docs/guide docs/internals -name '*.md'); do rel=${f#docs/}; grep -q "$rel" docs/README.md || echo "MISSING FROM README: $rel"; done
```
Expected: no "MISSING" lines.

- [ ] **Step 3: Commit**

```bash
git add docs/README.md
git commit -m "docs(restructure): rewrite README index for guide/ + internals/ tree

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Rewrite mkdocs.yml nav

**Files:** Modify `mkdocs.yml` (the `nav:` block, ~lines 41-80).

**Interfaces:**
- Consumes: final tree.
- Produces: a nav listing every kept file at its new path. This is what `--strict` validates.

- [ ] **Step 1: Replace the nav block**

Rewrite `nav:` to:

```yaml
nav:
  - Home: README.md
  - Guide:
      - Cookbook: guide/COOKBOOK.md
      - Features:
          - CRUD Operations: guide/features/CRUD_OPERATIONS.md
          - SELECT Queries: guide/features/SELECT_QUERIES.md
          - WHERE Clauses: guide/features/WHERE_CLAUSES.md
          - JOIN Operations: guide/features/JOIN_OPERATIONS.md
          - Batch Operations: guide/features/BATCH_OPERATIONS.md
          - Referential Integrity: guide/features/REFERENTIAL_INTEGRITY.md
          - Connection Tuning: guide/features/CONNECTION_TUNING.md
      - Reference:
          - Field Types: guide/reference/FIELD_TYPES.md
          - Migrations: guide/reference/MIGRATIONS.md
  - Internals:
      - Architecture:
          - Overview: internals/architecture/OVERVIEW.md
          - Design Decisions: internals/architecture/DESIGN_DECISIONS.md
          - Reflection: internals/architecture/REFLECTION.md
          - SQL Generation: internals/architecture/SQL_GENERATION.md
          - Module System: internals/architecture/MODULE_SYSTEM.md
          - Statement Caching: internals/architecture/STATEMENT_CACHING.md
          - Compile-Time vs Runtime: internals/architecture/COMPILE_TIME_VS_RUNTIME.md
      - Building:
          - Getting Started: internals/building/GETTING_STARTED.md
          - Common Tasks: internals/building/COMMON_TASKS.md
          - Adding Features: internals/building/ADDING_FEATURES.md
          - Pre-Commit: internals/building/PRE_COMMIT.md
          - Formatting: internals/building/FORMATTING.md
      - Testing:
          - Testing: internals/testing/TESTING.md
          - Code Coverage: internals/testing/CODE_COVERAGE.md
          - Sanitizers: internals/testing/SANITIZERS.md
          - Fuzzing: internals/testing/FUZZING.md
          - MSAN libc Fix: internals/testing/MSAN_LIBC_FIX.md
      - Performance:
          - Performance: internals/performance/PERFORMANCE.md
          - Benchmark Dashboard: internals/performance/BENCHMARK_DASHBOARD.md
          - JOIN Analysis: internals/performance/JOIN_ANALYSIS.md
          - DISTINCT Analysis: internals/performance/DISTINCT_ANALYSIS.md
      - Compiler:
          - Compiler Issues: internals/compiler/COMPILER_ISSUES.md
          - Compiler Attributes: internals/compiler/COMPILER_ATTRIBUTES.md
          - C++26 Coding Standards: internals/compiler/CPP26_CODING_STANDARDS.md
          - Code Quality: internals/compiler/CODE_QUALITY.md
  - Archive:
      - SELECT Optimization Report: archive/SELECT_OPTIMIZATION_REPORT.md
      - SELECT Performance Analysis: archive/SELECT_PERFORMANCE_ANALYSIS.md
      - UPDATE Optimization Report: archive/UPDATE_OPTIMIZATION_REPORT.md
```

(Keep everything ABOVE `nav:` — `site_name`, theme, plugins, markdown_extensions — unchanged.)

- [ ] **Step 2: Commit**

```bash
git add mkdocs.yml
git commit -m "docs(restructure): rewrite mkdocs nav for guide/ + internals/ tree

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Update external references (CLAUDE.md, benchmarks/README.md)

**Files:** Modify `CLAUDE.md` (15 `docs/...md` refs), `benchmarks/README.md` (if it links into docs/).

**Interfaces:**
- Consumes: final tree.
- Produces: every out-of-docs reference points to a real new path.

- [ ] **Step 1: Find all docs/ path references in CLAUDE.md**

```bash
grep -noE 'docs/[a-z]+/[A-Za-z0-9_]+\.md|docs/[A-Z_]+\.md' CLAUDE.md | sort -u
```

- [ ] **Step 2: Update each to its new path**

Map each old path to new (examples): `docs/development/FORMATTING.md`→`docs/internals/building/FORMATTING.md`; `docs/development/CODE_COVERAGE.md`→`docs/internals/testing/CODE_COVERAGE.md`; `docs/architecture/STATEMENT_CACHING.md`→`docs/internals/architecture/STATEMENT_CACHING.md`; `docs/development/PERFORMANCE_GUIDELINES.md`→`docs/internals/performance/PERFORMANCE.md`; `docs/features/*`→`docs/guide/features/*`; `docs/reference/FIELD_TYPES.md`→`docs/guide/reference/FIELD_TYPES.md`; `docs/development/COMPILER_ISSUES.md`→`docs/internals/compiler/COMPILER_ISSUES.md`; `docs/features/CONNECTION_TUNING.md`→`docs/guide/features/CONNECTION_TUNING.md`; `docs/development/TESTING.md`→`docs/internals/testing/TESTING.md`; `docs/development/MIGRATIONS.md`→`docs/guide/reference/MIGRATIONS.md`. Apply the full target structure for any not listed.

- [ ] **Step 3: Check benchmarks/README.md and any other root docs**

```bash
grep -rnoE 'docs/[a-z]+/[A-Za-z0-9_]+\.md|docs/[A-Z_]+\.md' benchmarks/README.md README.md 2>/dev/null
```
Update any hits to new paths.

- [ ] **Step 4: Verify no stale docs paths remain anywhere outside docs/superpowers + docs/archive**

```bash
grep -rnE 'docs/(development|features|benchmarks|reference|architecture)/' CLAUDE.md README.md benchmarks/README.md | grep -vE 'docs/internals/architecture|superpowers|archive'
```
Expected: NOTHING. (Old top-level group paths fully replaced.)

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md benchmarks/README.md README.md 2>/dev/null
git commit -m "docs(restructure): update CLAUDE.md and root refs to new doc paths

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Final verification — mkdocs strict build + Part A re-audit

**Files:** none (verification only).

- [ ] **Step 1: mkdocs strict build must be clean**

```bash
cd /home/ihor/projects/storm/storm_develop/.claude/worktrees/feature-docs-staleness-sweep
/tmp/mkdocs-venv/bin/mkdocs build --strict --site-dir /tmp/mkdocs-site 2>&1 | grep -E 'WARNING|ERROR' || echo "STRICT CLEAN"
```
Expected: `STRICT CLEAN` (zero WARNING/ERROR). If any warning, fix the named link and re-run until clean.

- [ ] **Step 2: every kept .md is in the nav**

```bash
for f in $(find docs/guide docs/internals -name '*.md'); do rel=${f#docs/}; grep -q "$rel" mkdocs.yml || echo "NOT IN NAV: $rel"; done
```
Expected: no "NOT IN NAV" lines.

- [ ] **Step 3: history preserved on a sampled move**

```bash
git log --follow --oneline docs/guide/features/CRUD_OPERATIONS.md | head -3
```
Expected: shows commits from BEFORE the move (proves `git mv`).

- [ ] **Step 4: Part A staleness still clean (moves didn't reintroduce anything)**

```bash
grep -rn --include='*.md' '3-level\|three-level' docs/ | grep -v superpowers | grep -v archive || echo "3-level CLEAN"
grep -rn --include='*.md' '\[\[storm::primary_key\|\[\[storm::foreign_key\|storm::auto_increment\|union_with' docs/ | grep -v superpowers | grep -v archive || echo "syntax CLEAN"
```
Expected: `3-level CLEAN` and `syntax CLEAN`.

- [ ] **Step 5: branch diff sanity**

```bash
git diff origin/develop..HEAD --stat | tail -5
git log --oneline origin/develop..HEAD | head
```
Expected: only docs/, mkdocs.yml, CLAUDE.md, (benchmarks/README.md) changed; no code/cmake/test files.

---

## Post-plan: push, PR, merge

- [ ] Push: `git push -u origin feature/docs-staleness-sweep`
- [ ] Open PR into `develop` (`gh pr create --base develop`). Docs+mkdocs+CLAUDE.md diff (no src/tests/cmake) → SonarCloud skipped per convention; CI jobs are the gate. NOTE: the docs deploy workflow (`mkdocs gh-deploy --force`) does NOT use `--strict`, so the LOCAL strict build in Task 7 is the real link-integrity gate — it must have passed before pushing.
- [ ] Per repo convention, arm auto-merge: `gh pr merge <N> --squash --auto`. Merge after CI green.
- [ ] After merge: `git checkout develop && git pull`; `git worktree remove .claude/worktrees/feature-docs-staleness-sweep`; `git worktree prune`.
