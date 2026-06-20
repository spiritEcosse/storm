# Documentation Staleness Sweep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align every user/dev-facing doc with current `src/` code (Part A of the docs task; reorganization is a separate later plan).

**Architecture:** Pure markdown edits. No code, no build. Each task fixes one coherent staleness group from the spec, then re-runs the spec's audit grep as its "test" (expecting zero stale hits), then commits. Work happens on branch `feature/docs-staleness-sweep` (already created on `origin/develop`, with the spec commit on top).

**Tech Stack:** Markdown, `grep`. Ground-truth code reference: `src/orm/`, `shared/models.h`, `tests/query/`.

## Global Constraints

- Excluded from all edits: `docs/superpowers/` and `docs/archive/`.
- Current annotation spelling: `[[= storm::FieldAttr::primary]]`, `[[= storm::fk<>]]` (the `= ` is required; bare `[[storm::…]]` is invalid).
- Current FK field holds the **related object** (`Person sender;`), not an int; the emitted column is `<name>_id`.
- Current cache reality (#214): a single Connection-level prepared-statement cache via `prepare_cached`. No L1 (`select_stmt_` / `get_select_statement()`) or L2 (`cached_stmt_`). `architecture/STATEMENT_CACHING.md` is the correct reference doc — do not edit it.
- Public set-op methods: `union_`, `union_all`, `except_`, `intersect_`. There is no `union_with`.
- Spec reference: `docs/superpowers/specs/2026-06-19-docs-staleness-sweep-design.md`.
- Verify on the `feature/docs-staleness-sweep` branch; do not commit to `develop`.

---

### Task 1: Fix COOKBOOK.md (annotations, FK, join, set-op)

**Files:**
- Modify: `docs/COOKBOOK.md` (lines 13, 199, 201, 205, 215)
- Test (verification grep): the COOKBOOK-specific patterns below

**Interfaces:**
- Consumes: ground-truth from `shared/models.h` `Message` (`[[= storm::fk<>]] Person sender;`) and `tests/query/test_values.cpp` (`join<^^Message::sender>()`).
- Produces: a COOKBOOK with zero invalid annotation/method spellings (later tasks don't depend on this).

- [ ] **Step 1: Run the audit to confirm the stale hits exist (failing "test")**

Run:
```bash
grep -n "\[\[storm::primary_key\|storm::auto_increment\|\[\[storm::foreign_key\|join<Message>\|union_with" docs/COOKBOOK.md
```
Expected: 5 hits — lines 13, 199, 201 (annotations), 205 (`join<Message>`), 215 (`union_with`).

- [ ] **Step 2: Fix the two model-declaration annotations (lines 13 and 199)**

Both occurrences are identical:
- Old: `    [[storm::primary_key, storm::auto_increment]] int id;`
- New: `    [[= storm::FieldAttr::primary]] int id{};`

Use `replace_all` since the line is identical in both spots.

- [ ] **Step 3: Fix the Message struct FK field (line 201)**

- Old: `    [[storm::foreign_key<^^Person::id>]] int sender;`
- New: `    [[= storm::fk<>]] Person sender;`

- [ ] **Step 4: Fix the join call (line 205)**

- Old: `auto results = msg_qs.join<Message>().where(...).select();`
- New: `auto results = msg_qs.join<^^Message::sender>().where(...).select();`

- [ ] **Step 5: Fix the set-op method name (line 215)**

- Old: `    .union_with(qs2.where(f<^^Person::is_active>() == true))`
- New: `    .union_(qs2.where(f<^^Person::is_active>() == true))`

- [ ] **Step 6: Re-run the audit to verify zero hits**

Run:
```bash
grep -n "\[\[storm::primary_key\|storm::auto_increment\|\[\[storm::foreign_key\|join<Message>\|union_with" docs/COOKBOOK.md || echo "CLEAN"
```
Expected: `CLEAN`.

- [ ] **Step 7: Commit**

```bash
git add docs/COOKBOOK.md
git commit -m "docs(cookbook): fix stale annotation, FK, join, and set-op syntax

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Fix "3-level caching" one-line claims (README, OVERVIEW, SELECT summary)

**Files:**
- Modify: `docs/README.md:28`
- Modify: `docs/architecture/OVERVIEW.md:17,220`
- Modify: `docs/features/SELECT_QUERIES.md:452`

**Interfaces:**
- Consumes: the correct framing from `architecture/STATEMENT_CACHING.md` ("single Connection-level prepared-statement cache").
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Confirm the stale one-liners exist**

Run:
```bash
grep -rn "3-level" docs/README.md docs/architecture/OVERVIEW.md docs/features/SELECT_QUERIES.md
```
Expected: 4 hits (README:28, OVERVIEW:17, OVERVIEW:220, SELECT_QUERIES:452).

- [ ] **Step 2: Fix README.md:28**

- Old: `- **[Statement Caching](architecture/STATEMENT_CACHING.md)** - 3-level caching achieving near-raw SQLite performance`
- New: `- **[Statement Caching](architecture/STATEMENT_CACHING.md)** - Single Connection-level statement cache achieving near-raw SQLite performance`

- [ ] **Step 3: Fix OVERVIEW.md:17**

- Old: `3-level caching architecture achieving near-raw SQLite performance.`
- New: `A single Connection-level statement cache achieving near-raw SQLite performance.`

- [ ] **Step 4: Fix OVERVIEW.md:220**

- Old: `- [Statement Caching](STATEMENT_CACHING.md) - 3-level caching details`
- New: `- [Statement Caching](STATEMENT_CACHING.md) - Single Connection-level cache details`

- [ ] **Step 5: Fix SELECT_QUERIES.md:452**

- Old: `✅ **3-level caching** - Optimal statement reuse`
- New: `✅ **Connection-level statement cache** - Statement reuse`

- [ ] **Step 6: Re-run to verify zero hits in these four files**

Run:
```bash
grep -rn "3-level" docs/README.md docs/architecture/OVERVIEW.md docs/features/SELECT_QUERIES.md || echo "CLEAN"
```
Expected: `CLEAN`.

- [ ] **Step 7: Commit**

```bash
git add docs/README.md docs/architecture/OVERVIEW.md docs/features/SELECT_QUERIES.md
git commit -m "docs: correct stale 3-level caching one-liners to single Connection-level cache

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Rewrite the SELECT_QUERIES.md caching section (removed L1 example)

**Files:**
- Modify: `docs/features/SELECT_QUERIES.md` (the "Caching Architecture" section starting at line 79)

**Interfaces:**
- Consumes: `src/db/` `prepare_cached` behavior, `STATEMENT_CACHING.md` description.
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Confirm the removed-API example exists**

Run:
```bash
grep -n "3-level caching architecture\|get_select_statement\|select_stmt_" docs/features/SELECT_QUERIES.md
```
Expected: hits around lines 79–95 (`SelectStatement uses a 3-level caching architecture`, the `select_stmt_` member, `get_select_statement()`).

- [ ] **Step 2: Read the current section to get exact bounds**

Run:
```bash
sed -n '77,110p' docs/features/SELECT_QUERIES.md
```
Note the exact start (the `SelectStatement uses a 3-level caching architecture…` sentence and its `### Caching Architecture` heading) and the end of the embedded code block.

- [ ] **Step 3: Replace the section body**

Replace the "3-level" sentence and the `// Level 1: QuerySet caches Statement instance` code block (through the end of that fenced block) with this prose + pointer (no removed-member code):

```markdown
SelectStatement reuses prepared statements through a single Connection-level
cache. The Connection keeps a pool of prepared statements keyed by SQL text, so
repeated identical SELECTs reuse a compiled statement instead of re-parsing and
re-planning it. There is no per-QuerySet or per-Statement cache — those layers
(L1/L2) were removed in #214 after benchmarks showed no measurable benefit.

See [Statement Caching](../architecture/STATEMENT_CACHING.md) for the full
design and the #214 measurements.
```

- [ ] **Step 4: Verify the removed-API names are gone from the section**

Run:
```bash
grep -n "3-level caching architecture\|get_select_statement\|select_stmt_" docs/features/SELECT_QUERIES.md || echo "CLEAN"
```
Expected: `CLEAN`.

- [ ] **Step 5: Commit**

```bash
git add docs/features/SELECT_QUERIES.md
git commit -m "docs(select): rewrite caching section to single Connection-level cache (#214)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Rewrite the CRUD_OPERATIONS.md caching section (removed L2 example)

**Files:**
- Modify: `docs/features/CRUD_OPERATIONS.md` (the "Statement Caching" section starting at line 216)

**Interfaces:**
- Consumes: same cache reality as Task 3.
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Confirm the removed-API example exists**

Run:
```bash
grep -n "3-level caching pattern\|cached_stmt_" docs/features/CRUD_OPERATIONS.md
```
Expected: hits around lines 218+ (`UpdateStatement uses the 3-level caching pattern`, `cached_stmt_ = nullptr`).

- [ ] **Step 2: Read the current section to get exact bounds**

Run:
```bash
sed -n '214,250p' docs/features/CRUD_OPERATIONS.md
```
Note where the `### Statement Caching` heading starts and where the embedded `cached_stmt_` code block ends.

- [ ] **Step 3: Replace the section body**

Replace the `UpdateStatement uses the 3-level caching pattern:` sentence and the following `cached_stmt_` code block with:

```markdown
UpdateStatement reuses prepared statements through the single Connection-level
cache (`prepare_cached`): identical UPDATEs reuse a compiled statement keyed by
SQL text. There is no per-Statement handle cache — that layer was removed in
#214.

See [Statement Caching](../architecture/STATEMENT_CACHING.md) for details.
```

- [ ] **Step 4: Verify the removed-API names are gone from the section**

Run:
```bash
grep -n "3-level caching pattern\|cached_stmt_" docs/features/CRUD_OPERATIONS.md || echo "CLEAN"
```
Expected: `CLEAN`.

- [ ] **Step 5: Commit**

```bash
git add docs/features/CRUD_OPERATIONS.md
git commit -m "docs(crud): rewrite caching section to single Connection-level cache (#214)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Fix JOIN_OPERATIONS.md removed-API example and REFLECTION.md roadmap note

**Files:**
- Modify: `docs/features/JOIN_OPERATIONS.md` (lines 418–420)
- Modify: `docs/architecture/REFLECTION.md:21`

**Interfaces:**
- Consumes: current execution path (no `get_select_statement`).
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Confirm both stale spots**

Run:
```bash
grep -n "get_select_statement" docs/features/JOIN_OPERATIONS.md
grep -n "foreign_key, etc" docs/architecture/REFLECTION.md
```
Expected: JOIN_OPERATIONS hits at 418/420; REFLECTION hit at 21.

- [ ] **Step 2: Read the JOIN example context**

Run:
```bash
sed -n '408,424p' docs/features/JOIN_OPERATIONS.md
```
Determine what the example illustrates (statement execution with/without a join statement).

- [ ] **Step 3: Update the JOIN example**

Replace the `get_select_statement().execute_optimized(...)` calls with the current path. If the surrounding example is an illustration of a removed internal method, replace the code block with a short note that execution runs through the Connection-level cached statement (mirror the phrasing used in `STATEMENT_CACHING.md`). Keep the change minimal — only the lines referencing `get_select_statement`.

- [ ] **Step 4: Update the REFLECTION.md roadmap note (line 21)**

- Old: `- More attributes coming (unique, index, foreign_key, etc.)`
- New: `- Foreign keys (`fk<>`), many-to-many, and auto-timestamps are now supported; more attributes (unique, index) are planned`

- [ ] **Step 5: Verify**

Run:
```bash
grep -n "get_select_statement" docs/features/JOIN_OPERATIONS.md || echo "JOIN CLEAN"
grep -n "foreign_key, etc" docs/architecture/REFLECTION.md || echo "REFLECTION CLEAN"
```
Expected: `JOIN CLEAN` and `REFLECTION CLEAN`.

- [ ] **Step 6: Commit**

```bash
git add docs/features/JOIN_OPERATIONS.md docs/architecture/REFLECTION.md
git commit -m "docs: drop removed get_select_statement example; refresh attribute roadmap

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Final full-tree verification

**Files:** none (verification only)

- [ ] **Step 1: Run all spec audit greps**

Run:
```bash
grep -rn --include='*.md' "3-level\|three-level" docs/ | grep -v superpowers | grep -v archive || echo "A: CLEAN"
grep -rn --include='*.md' "\[\[storm::primary_key\|\[\[storm::foreign_key\|storm::auto_increment" docs/ | grep -v superpowers | grep -v archive || echo "B: CLEAN"
grep -rn --include='*.md' "union_with" docs/ | grep -v superpowers | grep -v archive || echo "C: CLEAN"
grep -rn --include='*.md' "get_select_statement" docs/ | grep -v superpowers | grep -v archive | grep -v STATEMENT_CACHING || echo "D: CLEAN"
```
Expected: `A: CLEAN`, `B: CLEAN`, `C: CLEAN`, `D: CLEAN`.

- [ ] **Step 2: Confirm the history note in STATEMENT_CACHING.md is untouched**

Run:
```bash
grep -n "three cache levels" docs/architecture/STATEMENT_CACHING.md
```
Expected: still present (line ~8) — this is the correct historical note and must NOT have been removed.

- [ ] **Step 3: Review the full branch diff**

Run:
```bash
git log --oneline origin/develop..HEAD
git diff origin/develop..HEAD --stat
```
Expected: spec commit + Tasks 1–5 commits; only `.md` files changed (plus the spec under `docs/superpowers/`).

---

## Post-plan: push, PR, merge

After all tasks pass verification:

- [ ] Push the branch: `git push -u origin feature/docs-staleness-sweep`
- [ ] Open a PR into `develop` with `gh pr create --base develop`. Docs-only diff (no src/tests/cmake) → per project convention, SonarCloud check is skipped; CI jobs are the real gate.
- [ ] Wait for CI; merge with `gh pr merge --squash --auto`.
- [ ] After merge: `git checkout develop && git pull`, then `git branch -d feature/docs-staleness-sweep`.
