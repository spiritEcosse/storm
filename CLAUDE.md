# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

**📚 Full Documentation**: [docs/README.md](docs/README.md)

## Project Overview

Storm is a C++26 ORM library for SQLite using compile-time reflection to automatically map C++ structs to database tables without macros.

**Performance**: 96-108% efficiency vs raw SQLite (Release builds). See [benchmarks/README.md](benchmarks/README.md).

**Key Features**: Compile-time SQL generation, single-level (Connection-level) statement caching, thread-local caching, type-erased JOINs, pure C++26 reflection for WHERE clauses.

## Behavioral Guidelines

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**C++ coding standards**: When writing, reviewing, or refactoring C++ code, follow the rules in [`.claude/agents/rule-standards.md`](.claude/agents/rule-standards.md) (C++ Core Guidelines — RAII, immutability, type safety, concepts, Rule of Zero/Five, etc.).

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

### 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Critical Safety Rules

**⚠️ NEVER violate these rules:**

1. **NEVER delete `.git`** - Do not run `rm -rf .git`
2. **NEVER push without approval** - Ask before `git push` (exception: user says "commit and push")
3. **NEVER skip the pre-commit hook** - Do not use `--no-verify`; `commit.sh` enforces format, tidy, test, coverage, sonar automatically
4. **NEVER work directly on `develop` for issue-linked tasks** - Always create `feature/<N>-<description>` branch first (see [Branching Rules](#branching-rules))
5. **ALWAYS show files before commit** - Run `git status --short`, get user approval, then commit
6. **ALWAYS benchmark after code changes** - Use Release builds; revert if ANY slowdown
7. **NEVER run sanitizer builds locally — CI runs them** - `ninja-asan-ubsan` (memory + UB) and `ninja-tsan` (data races) run on every PR in CI; check results with `gh pr checks <PR#>`. Fix or revert if CI reports new violations. Do NOT build or run sanitizer presets locally.
8. **ALWAYS update docs AND agent files after changes** - Code + docs + `.claude/agents/*.md` commit together. If you change a feature, preset, command, or pattern described in any agent file, update that agent file too.
   - **Agent `description:` MUST stay on ONE physical line** — write newlines as literal `\n` escapes (see `reviewer.md`). A description spanning real lines breaks the YAML frontmatter parse and Claude Code drops the agent **silently**: it never appears in the available-agents list and cannot be dispatched, with no warning. This shipped twice (#543 — `storm-sql-reviewer`, `storm-buildsystem-reviewer`, both undispatchable from merge). Enforced by `scripts/check-agent-frontmatter.sh`, run by `commit.sh` on any staged agent file and by the `agent-frontmatter` CI job.
9. **ALWAYS write thorough unit tests BEFORE implementing** - Every feature or fix needs comprehensive tests first (see [Testing Checklist](#thorough-testing-checklist)). Workflow: (1) write tests → (2) run — new tests MUST fail (proves they test real behavior) → (3) implement → (4) run again — ALL tests must pass
10. **SonarCloud gate MUST pass before merging** - Zero issues on new code; no exceptions, even for minor issues (see [SonarCloud Gate](#sonarcloud-gate-mandatory-before-merge))
11. **NEVER use `throw` for compile-time errors in `consteval` functions** - Use `requires` constraints instead. Define a concept that checks the condition and constrain the template. The `throw "string literal"` trick works but fires late with a poor error message. `requires` fires at the call site with a clear constraint violation. Use `std::unreachable()` after the loop body if needed to satisfy the return type.
12. **NEVER close an issue without verifying all subtasks** - Before closing, read the issue body and confirm every "Definition of done" checkbox was genuinely completed. Only check off items that were delivered. If some are intentionally skipped, ask the user first.
13. **ALWAYS run the relevant reviewer agent(s) before every commit that touches code** - After staging (`git add`) and BEFORE `git commit`, dispatch the reviewer(s) matching the staged diff and address their findings. The pre-commit hook checks mechanics (format, tidy, tests, coverage); the reviewer agents check design and correctness. Applies to ALL code commits, including inline fixes and SonarCloud-fix commits; docs-only commits are exempt.
    - `storm-code-reviewer` - any `src/`, `tests/`, or other C++ change. Dispatch on every code commit. Reviews the **C++** — RAII, concepts, module structure, hot-path patterns.
    - `storm-sql-reviewer` - any `src/orm/statements/**`, `src/orm/schema.cppm`, or `src/db/*_statement.cppm` change. Dispatch alongside `storm-code-reviewer`. Reviews the **generated SQL text and bind sequence** (text↔bind agreement, column-name derivation, set semantics, sizer↔writer agreement, dialect matrix) — a blind spot `storm-code-reviewer` structurally cannot see since that code compiles cleanly and looks idiomatic even when the SQL it emits is wrong (#501, #506, #500).
    - `storm-buildsystem-reviewer` - any `cmake/**`, `CMakePresets.json`, `.github/workflows/**`, `commit.sh`, `.githooks/**`, or `scripts/**` change. Reviews CMake + presets + CI + hooks as one coupled system (cross-file consistency, cold-path correctness) — runs **alongside**, not instead of, `storm-code-reviewer` when a commit touches both.

**Doc conventions:**
- ASK before creating new `.md` files
- UPPERCASE doc filenames — `GETTING_STARTED.md`, not `getting-started.md`

## Quick Start

### CMake Presets

| Preset | Build type | Tests | Coverage | Bench | Sanitizer | Tools | Use for |
|---|---|---|---|---|---|---|---|
| `ninja-debug` | Debug | ✓ | ✓ | — | — | ✓ | Development, coverage |
| `ninja-release` | Release | ✓ | — | ✓ | — | ✓ | CI, benchmarking |
| `ninja-prod` | Release | — | — | — | — | — | Production artifact |
| `ninja-asan-ubsan` | Debug | ✓ | — | — | ASAN+UBSAN | — | Memory safety + undefined behavior (**CI only** — rule 7) |
| `ninja-tsan` | Debug | ✓ | — | — | TSAN | — | Data race detection (**CI only** — rule 7) |
| `ninja-msan` | Debug | ✓ | — | — | MSAN | — | Uninitialized memory reads (**CI only** — rule 7) |

### Build & Test
```bash
# Debug (tests + coverage on by default)
cmake --preset ninja-debug && cmake --build --preset ninja-debug
ctest --preset ninja-debug

# Release dev (tests + bench on by default)
cmake --preset ninja-release && cmake --build --preset ninja-release

# Production (library only, no extras)
cmake --preset ninja-prod && cmake --build --preset ninja-prod
```

### CMake Module Structure
```
cmake/
├── libcxx.cmake          # LIBCXX_ROOT validation, global -nostdinc++ flags, apply_clang_flags()
├── db.cmake              # find_package SQLite3 + PostgreSQL, link_sqlite/link_postgresql helpers
├── cpm.cmake             # CPM.cmake bootstrap (auto-downloaded on first configure)
├── cmake-scripts.cmake   # Global CPM fetch of StableCoder/cmake-scripts (used by format + sanitizers)
├── coverage.cmake        # Coverage compile/link flags (must include before tests)
├── coverage-targets.cmake# Coverage cmake targets: coverage, coverage-html, coverage-clean
├── tests.cmake           # GoogleTest via CPM + add_subdirectory(tests)
├── bench.cmake           # add_subdirectory(benchmarks)
├── sanitizers.cmake      # USE_SANITIZER option + cmake-scripts integration
├── format.cmake          # clang-format/cmake-format targets (see docs/internals/building/FORMATTING.md)
└── tools.cmake           # ENABLE_TOOLS option + add_subdirectory(tools) (storm-schema CLI)
```

### GitHub Issue Workflow
- **Before starting**: Read the issue body with `gh issue view <N>`. Check for a **"Definition of done"** section or checkbox subtasks (e.g., `- [ ] Each error path has a test`).
- **Verify context**: Before planning or making changes, cross-check the issue description (file paths, line numbers, API references, assumptions) against the actual codebase. If anything is outdated or wrong, **report discrepancies to the user** before proceeding.
- **Track subtasks**: Use those checkboxes as your acceptance criteria. **Before committing**, check off all verified subtasks: `gh issue edit <N> --body "..."` with `- [x]` replacing `- [ ]`. Do not defer this to after the commit or merge.
- **Close when done**: After all subtasks are checked off, close the issue with `gh issue close <N>`.
- **Superpowers skills (`brainstorming`, `writing-plans`, `executing-plans`)**: for issue-linked work, use the GitHub issue body as the spec/plan/results record instead of writing new files under `docs/superpowers/specs/`, `docs/superpowers/plans/`, or `docs/superpowers/results/` — the issue description and its checkboxes already serve that role (see rule #12 and "Track subtasks" above). Only fall back to `docs/superpowers/` for ad-hoc work with no linked issue. Note: `docs/superpowers/` is gitignored (not committed) — it's a local scratch area only.

### Branching Rules
- **ALWAYS start a feature branch in a git worktree, NOT in the default project folder** — create the branch in its own `git worktree` under `../worktrees/<branch-name>/` (relative to the main repo dir `storm_develop`, i.e. a sibling of the repo, NOT inside it) so the main working directory stays clean and on `develop`. Do all work for the branch inside that worktree. Run `git worktree remove` after the branch is merged.
- **GitHub Issue work**: Create and link a feature branch using `gh issue develop <N> --name feature/<N>-<short-description> --base develop --checkout` — this creates the branch, links it to the issue in GitHub. Then add it as a worktree (`git worktree add ../worktrees/<branch-name> feature/<N>-<short-description>`) and work from there instead of checking it out in place.
- **Create pull request**: After pushing a feature branch, ALWAYS create a PR with `gh pr create --base develop` including `Closes #<N>` in the body to auto-link and auto-close the issue on merge.
- **After creating a PR**: Wait 30 seconds, then run `/sonarcloud-status`. If there are **zero issues** on new code, check CI jobs with `gh pr checks <PR#> --watch`. Only merge after **both** SonarCloud gate AND all CI jobs (ninja-debug, ninja-release, ninja-asan-ubsan, ninja-tsan) pass (`gh pr merge --squash`). If ANY SonarCloud issues or CI failures, fix them all, push, and re-check until clean.
- **Close issue after merge**: After merging a feature branch into `develop`, ALWAYS close the issue with `gh issue close <N>`. Do NOT wait to be asked.
- **Switch to develop after merge**: After merging and closing the issue, ALWAYS run `git checkout develop && git pull` to return to the main branch.
- **Ad-hoc fixes** (no GitHub Issue): Work directly on `develop`.

### SonarCloud Gate (MANDATORY before merge)

The `/sonarcloud-status` skill (uses `sonar` CLI) is **branch-aware**:

| Context | Mode | What it checks |
|---|---|---|
| `develop` / `master` / `main` | **Branch mode** | Full project — ALL existing issues on the branch |
| Feature branch or explicit PR number | **PR mode** | New code only — waits for Sonar to finish, then checks changed lines |

**Quality gate: "Storm Strict"** (custom gate assigned to this project):
- `new_violations > 0` → FAIL (zero new issues of ANY severity: bugs, smells, vulnerabilities, even minor)
- `new_duplicated_lines_density > 0` → FAIL (zero code duplication on new lines)
- `new_security_hotspots_reviewed < 100%` → FAIL

**GitHub branch protection**: `develop` requires `SonarCloud Code Analysis` to pass before any merge — enforced at the repository level, cannot be bypassed.

**Before merging a PR (run from the feature branch):**
1. Run `/sonarcloud-status` — it auto-detects the PR and **waits** for SonarCloud analysis to finish.
2. **If gate passes** (zero violations, zero duplications): merge the PR into `develop`.
3. **If gate fails** (ANY issue or ANY duplication — no matter how minor): fix ALL reported issues on the feature branch, push, then re-check until the gate passes.
4. **Only merge after a clean SonarCloud gate** — GitHub will block the merge otherwise.

**Checking overall project health (on `develop`):**
Run `/sonarcloud-status` while on `develop` to see the full project picture — all existing issues, overall metrics, and quality gate status for the branch.

### SonarCloud Coding Rules (follow proactively when writing code)

These rules are enforced by SonarCloud analysis. Follow them when writing new code to avoid issues:

| Rule | What to do | Why |
|---|---|---|
| **S125** | Never leave commented-out code. Remove migration comments, old code, data comments like `// (Alice,30), (Bob,25)` | SonarCloud flags any comment that looks like code |
| **S3656** | Use `public:` (not `protected:`) for GTest fixture member variables | SonarCloud forbids protected members in classes |
| **S6185** | Use `std::format("Person{}", i)` instead of `"Person" + std::to_string(i)` | Prefer std::format over string concatenation |
| **S3659** | Use `\|\|` and `&&` instead of `or` and `and` | Alternative operators forbidden |
| **S6164** | Use `std::numbers::pi` instead of `3.14159` | Use standard math constants |
| **S6197** | Use `std::ranges::sort(vec)` instead of `std::ranges::sort(vec.begin(), vec.end())` | Prefer range overloads |
| **S6177** | Use `using enum EnumType;` to avoid verbose `EnumType::Value` repetition | Reduce enum verbosity |
| **S7034** | Use `str.contains(substr)` instead of `str.find(substr) != npos` | Prefer C++23 contains() |
| **S6009** | Use `std::string_view` for read-only string parameters | Avoid const std::string& for read-only |
| **S6003** | Use `emplace_back` instead of `push_back` when constructing in-place | Avoid unnecessary copies |
| **S1659** | Avoid `auto x = Type{};` — use `Type x;` directly | SonarCloud may flag brace-init as multi-identifier |
| **S912** | No side effects in `&&`/`\|\|` right operands (e.g., `--depth` in `&& --depth == 0`) | Separate side effects from conditions |
| **S6045** | Use `std::set<T, std::less<>>` for string containers | Transparent comparator for heterogeneous lookup |

**When NOSONAR is acceptable** (add `// NOSONAR` on the exact flagged line):
- `S5025`: GTest `RegisterTest` requires raw `new` — can't use smart pointers
- `S6188`: consteval functions use `ptr+sz` pattern — `std::span` not reliable in consteval
- `S3776`: consteval JSON parsers and `if constexpr` dispatch have inherent complexity
- `S1820`: Flat structs for consteval parsing intentionally exceed 20 fields
- `S6024`: GTest fixture static helpers are idiomatic — no need to extract as free functions
- `S954`: `#include "test_models.h"` MUST come after `import storm;` — can't move to top

### Commit & Push Workflow
```bash
git status --short           # Show files
# Get user approval
git add -A && git commit -m "message"
# Pre-commit hook (commit.sh): clang-format (C++) + cmake-format → clang-tidy → tests → coverage
# Smart skips: no C++/cmake → skip all; cmake-only → tests+coverage+cmake-format; C++ only-bench → skip tests/coverage
# Self-heal (#489): commit.sh configures + fully builds build/release before clang-tidy
#   (BMIs + mock binaries, so no missing compile_commands.json / std.pcm) and
#   build/debug before ctest (so no storm_mock_tests_NOT_BUILT). No-op on the warm
#   path; covers both the git-hook path and a manual ./commit.sh on a fresh worktree.

git push
# Pre-push hook (.githooks/pre-push): SonarCloud gate disabled (C++26 not yet supported)
# See: https://github.com/spiritEcosse/storm/issues/113
```

### Benchmarking (Release only!)
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench                                          # Full Google Benchmark run
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/SELECT/.*'     # Category filter
./build/release/benchmarks/storm_bench --benchmark_repetitions=10               # Stats: median/mean/stddev
./build/release/benchmarks/storm_anchors                                        # Raw SQLite anchors (release-time spot check)
```

### Code Coverage
```bash
# ninja-debug has coverage enabled by default
cmake --preset ninja-debug && cmake --build --preset ninja-debug

# Console summary (quick) — ninja-debug-coverage unsets STORM_PG_CONNSTR (SQLite + mock PG only)
cmake --build --preset ninja-debug-coverage --target coverage

# HTML report (detailed)
cmake --build --preset ninja-debug-coverage --target coverage-html
# Open build/debug/coverage/html-filtered/index.html
```

**Enforced in CI, not just locally (#528)**: the `coverage` job in `.github/workflows/ci.yml`
runs `ninja-debug-coverage` on every PR and fails below **100% line coverage** — the same gate
`commit.sh` step 5 applies, so the threshold is independently reproduced rather than self-reported
from one machine. It parses the **line** row specifically (functions ~81%, branches ~92% are not
gated) and uploads the HTML report as a `coverage-html` artifact on pass and failure alike.
The 100% is of the **filtered** set — 24 files / 8613 lines, `LCOV_EXCL` markers excluded from the
denominator — not of every line in the tree.

**⚠️ Coverage requires a running PostgreSQL** — the PG `Connection`'s `constexpr` transaction-nesting
methods and part of `pool.cppm` are instantiated only by a live connection (the libpq mock does not
reach them). Without a server the tree measures **~99.8-99.9%** and the gate fails; function coverage
dropping from 80.6% to **48.7%** is the tell, and the run prints a `WARN: PostgreSQL unreachable`
line. `scripts/coverage-run-batched.sh` defaults `STORM_PG_CONNSTR` to `host=/var/run/postgresql`;
an exported value wins, which is how CI targets its `postgres` service container. Note the
`ninja-debug-coverage` **build preset** nulls the variable, so the documented local command always
uses the script's default — invoke the script directly to point elsewhere. If coverage fails just
under 100%, check PG is up before suspecting your code.

See [docs/internals/testing/CODE_COVERAGE.md](docs/internals/testing/CODE_COVERAGE.md) for details.

### Prerequisites
- Custom Clang with C++26 reflection (`../clang-p2996/`)
- SQLite3, CMake 3.30+, Ninja

## Architecture

```
src/
├── storm.cppm                  # Main module
├── db/
│   ├── concept.cppm            # Database concepts
│   └── sqlite.cppm             # SQLite implementation
└── orm/
    ├── queryset.cppm           # QuerySet ORM interface
    ├── field_attr.cppm         # Free-standing flag annotation objects + is_primary_member (leaf module)
    ├── utilities.cppm          # ConstexprString, SQLCache
    ├── indexes.cppm            # Index, UniqueIndex, Indexes<T> trait (namespace storm)
    └── statements/             # INSERT, SELECT, UPDATE, DELETE, DISTINCT, JOIN
```

See [docs/internals/architecture/](docs/internals/architecture/) for design decisions.

### Key Design Decisions
1. C++26 reflection for automatic field mapping
2. Concept-based DB abstraction (PostgreSQL/MySQL ready)
3. Compile-time SQL generation (zero runtime overhead)
4. Single Connection-level statement cache + thread-local SQL caching (20x+ speedup). Statements are per-call temporaries owned by the result proxy; the L1/L2 caches were removed in #214 as they gave no measurable benefit.
5. Batch operations with smart thresholds (SQLite limit = 999)
6. Type-erased JOINs via abstract base class

## Performance Guidelines

**Target**: ≥95% of raw SQLite efficiency. Performance > code cleanliness.

### Hot Path Optimizations

| Optimization | Improvement | When to Use |
|--------------|-------------|-------------|
| Flat code over nested lambdas | ~3-4% | Hot paths, inner loops |
| Raw pointer caching in loops | ~5-6% | Query extraction loops |
| Template methods for modules | ~1-3% | Cross-module hot paths |

> Statement preparation is cached once, at the Connection level (`prepare_cached`,
> see [STATEMENT_CACHING.md](docs/internals/architecture/STATEMENT_CACHING.md)). The former
> per-QuerySet (L1) and per-Statement (L2) pointer caches were removed in #214
> after benchmarks showed no measurable benefit — do not reintroduce them.

```cpp
// Cache raw pointer in loops (5-6% faster)
sqlite3_stmt* raw = stmt->handle();
while (sqlite3_step(raw) == SQLITE_ROW) { ... }
```

### Fair Benchmark Rules
- Setup outside loop, execute inside
- Same algorithm, containers, decision logic for both Storm and raw
- **Same SCHEMA** — the raw anchor's `CREATE TABLE` must mirror what Storm's schema generator emits (e.g. `id INTEGER PRIMARY KEY AUTOINCREMENT`, NOT plain `INTEGER PRIMARY KEY`). AUTOINCREMENT alone adds ~358 ns/insert of `sqlite_sequence` bookkeeping inside `sqlite3_step`; a mismatch silently halved the INSERT `% of raw`.
- Use latency (ms/query) for different result sizes

See [docs/internals/performance/PERFORMANCE.md](docs/internals/performance/PERFORMANCE.md).

## Supported Field Types

`int`, `int64_t`, `double`, `float`, `bool`, `std::string`, `std::string_view`, `std::optional<T>`, `std::vector<uint8_t>` (BLOB)

**Annotation spelling (#442)**: annotation names are re-exported into the top-level `storm`
namespace — write `storm::primary`, `storm::fk<>`, `storm::many_to_many<>`,
`storm::reverse_fk<...>` instead of the longer `storm::meta::...`. The `storm::meta::` spelling
still works (re-exports are additive). Internal reflection helpers (`is_fk_field`,
`find_primary_key`, …) stay in `storm::meta`.

**Free-standing flag annotations (#492)**: the column flags are free-standing tag-object
annotations — `storm::primary`, `storm::primary_autoincrement`, `storm::indexed`,
`storm::unique`, `storm::auto_create`, `storm::auto_update`, `storm::signed_storage`,
`storm::full_unsigned` (plus `storm::primary_part`, #500) — replacing the former `enum class FieldAttr` (breaking, no
dual-spelling). Each is an empty tag struct + `inline constexpr` object in the
`storm_orm_field_attr` leaf (`storm::meta`), mirroring `fk<>`; detection scans
`annotations_of(m)` for the tag type (`has_annotation_type<Tag>`), the same pattern as
`is_fk_field`. The per-flag predicates (`is_unique`, `is_indexed`, `is_auto_create`,
`is_auto_update`, `has_full_unsigned_attr`, `has_signed_storage_attr`) keep their
names/signatures; PK detection routes through `is_primary_member(info)` (matches `primary`,
`primary_autoincrement`, OR `primary_part`). The exclusivity the enum gave for free is restored by the
`ModelAnnotationsValid<T>` concept (in `base.cppm`, ANDed into the `BaseStatement<T>`
constraint list), which rejects per member: `primary` + `primary_autoincrement`, or
`signed_storage` + `full_unsigned`. DDL is byte-identical — pure spelling change.

**Composite primary keys (#500)**: `[[= storm::primary_part]]` on two or more members declares a
multi-column PK — a free-standing tag object like `primary` (#492), re-exported to top-level
`storm::`. Deliberately a SEPARATE tag rather than "two `primary` members means composite": reusing
`primary` would silently turn a double-`primary` typo into a legal key. `is_primary_part_member`
detects it; `is_primary_member` now matches it too, so `ModelWithPrimaryKey<T>` accepts composite
models. `BaseStatement` gains `primary_key_members_` (the full PK list in DECLARATION order — the
order the DDL clause emits) and `has_composite_pk_`, alongside the unchanged `primary_key_`/`pk_name_`
(= the first element, so the ~13 files reading them are untouched). `schema.cppm` routes composite
models past the single-PK branch and emits every part as a regular column plus one table-level
`PRIMARY KEY (a, b)`, reusing the junction-table pattern; single-PK DDL is byte-identical.
`needs_index` now excludes every PK member (the table-level key already indexes each part).
New concept `ModelPrimaryKeyValid<T>` (ANDed into the `BaseStatement` constraint list next to
`ModelAnnotationsValid`) rejects: `primary` + `primary_part`, `primary_autoincrement` +
`primary_part`, exactly one `primary_part`, **two or more `primary`/`primary_autoincrement`
members** (the double-`primary` typo — without this the widened machinery would silently accept it
as composite), and a PK annotation on an m2m/reverse-FK container, on a `std::optional<T>`, or
`primary_part` + `unique`. The `std::optional` and relation-container rejections are gated on
`is_primary_member`, so they cover a **single** `primary` too — a deliberate widening (a nullable PK
was accepted before #500 and is never correct: SQLite's legacy NULL quirk admits duplicate keys while
PG rejects them). No in-tree model used that shape. The PK clause writes parts via the canonical column-name writer
(`append_column_name`, #422), so an FK part emits `warehouse_id` — a composite key over FKs is the
canonical association-table case, and naming the bare member would emit a PK over a nonexistent
column. `calculate_column_defs_size` keeps FK columns on the FK-suffix budget for composite models
(the regular suffix under-counts and `ConstexprString` truncates silently); `build_sql_impl` gained a
whole-SQL `std::unreachable()` backstop, since the per-column one can't catch a sizing shortfall. Autoincrement on a composite key is **unrepresentable**,
not just useless — SQLite rejects both spellings at parse time and PG's identity is single-column —
hence the compile-time rejection. **Scope**: annotation + DDL only; UPDATE/DELETE by composite key
land in #501 (below), INSERT/JOIN and composite FKs in #502–#504.
See [docs/guide/reference/FIELD_TYPES.md](docs/guide/reference/FIELD_TYPES.md).

**Composite PK — UPDATE and DELETE (#501)**: the by-key `WHERE pk = ?` widens to
`WHERE a = ? AND b = ?`, AND-joined in declaration order. Three things changed, each with its own
failure mode. (1) **SQL text**: the clause writer/sizer pair `append_pk_where_clause` /
`pk_where_clause_size` are free functions in `base.cppm` (NOT `BaseStatement` methods — that class is
inherited by every statement type, and `SchemaStatement` already sits at the S1448 ceiling), taking
the PK-member array as a parameter so `erase.cppm` and `update_grammar.cppm` share them without
depending on each other. Parts go through `append_column_name` (#422), so an FK part emits
`warehouse_id`. (2) **Bind arithmetic**: `bind_pk_values` binds all N parts via an index sequence
(each part may be a DIFFERENT type — `int` + `std::string` is the canonical shape, so the dispatch is
per-part at compile time) and threads `param_index` by REFERENCE, which is what removes the old
`++param_index`-per-row stride assumption. In UPDATE the key follows the SET values, so it starts at
N_set+1. (3) **SET-target gate**: `is_settable_member` in BOTH `update_grammar.cppm` and
`upsert_grammar.cppm` now tests `Base::is_pk_member(M)` instead of `M != primary_key_` — the old test
excluded only the FIRST part, leaving `SET b=? WHERE a=? AND b=?` (rewriting the key it matches on)
legal. `bind_field_at_index` gained a **separate** `SkipAllPK` flag rather than widening `SkipPK`:
INSERT passes `SkipPK` too, and composite INSERT is #502 — widening it would silently change INSERT's
bind order as a side effect. Bulk DELETE uses a **row-value IN list**, `(a, b) IN ((?,?),(?,?))`
(PG always, SQLite ≥ 3.15 vs the project's 3.35 floor); the per-column form `a IN (…) AND b IN (…)`
is WRONG — it matches the parts' cross product and deletes unlisted keys. Chunking is now
`MAX_CHUNK_ROWS = 799/N` since each row costs N parameters. The consteval DELETE grammar moved to a
new `erase_grammar.cppm` leaf (mirroring `update_grammar.cppm`, #434) to keep `erase.cppm` under the
file-size limit; its `append_row_placeholder_list` is shared by the consteval max-chunk builder AND
the runtime per-count builder, so the two cannot drift into disagreeing placeholder counts.
Single-PK SQL is byte-identical (regression-asserted), and single-PK UPDATE/DELETE benchmarks are
within noise (≤1.4% deltas at cv 1.1–2.9%).

**Composite PK — INSERT (#502)**: a composite key is never DB-generated — `AUTOINCREMENT` (SQLite)
and `GENERATED ... AS IDENTITY` (PG) are single-integer-column features, so every key part is
caller data. That inverts the single-PK rule twice. (1) **The key columns join the INSERT**: the
`SkipPrimaryKey` skip in `FieldNameGrammar::for_each_field_name` and the plain-`SkipPK` branch of
`skips_pk_column` are both gated on `!has_composite_pk_`, so a composite model emits and binds ALL
fields in declaration order (one shared consteval iterator feeds the column list, its sizer, and
the placeholders — they cannot drift). (2) **There is nothing to RETURN**: `insert().execute()` on
a composite model returns `std::expected<void, Error>` with no `RETURNING` emitted, riding the
pre-existing `ReturnId::No` fast path. The plain call resolves there via `default_return_id<T>()`
(consteval: `No` for composite, `Yes` for single-PK), so the call site is IDENTICAL across model
shapes; an explicit `insert<ReturnId::Yes>` on a composite model is a **compile-time error** via
the `ReturnIdSupported<T, R>` concept (unconstrained it would emit `RETURNING <first part>` —
silently lossy), constraining BOTH `QuerySet::insert` and `InsertStatement::query` so the
diagnostic fires at the user's call site. `ReturnId::No` stays valid on every model shape (generic
code that spells it out keeps compiling). The #413 auto-`DEFAULT` caveat was checked and does not
fire: that `DEFAULT` is recovered from a C++ default-member-initializer — a compile-time constant
already in the caller's object, not a DB-generated value. `placeholders_count()` widened to
`field_count_` for composite models (feeds the upsert `auto_update` tail offset — #503's path);
chunking needed NO change (the auto batch size already divides by ALL fields, which becomes exactly
right once the key columns are bound). Single-PK INSERT SQL is byte-identical
(regression-asserted) and the `RETURNING id` path is unchanged. Upsert/`ON CONFLICT` is #503,
JOIN #504.

**Composite PK — FKs and JOINs (#504)**: an FK may reference a composite-PK model, and every JOIN
path (regular FK, m2m, reverse-FK) works over multi-column keys. An FK to an N-part target occupies
**N columns**, named `<member>_<part>` (`line_order_id`, `line_product_id`) — the single-column
`<member>_id` spelling is unchanged for single-PK targets, so all pre-#504 SQL is byte-identical
(regression-asserted at every layer). Nullable composite FKs work: `std::optional<Target>` makes all
N columns nullable together.

Four things had to widen in lockstep, each with its own failure mode. (1) **ON clauses** AND-join
one equality per part (`t2.a = t1.fk_a AND t2.b = t1.fk_b`) — matching only the first part would
silently join rows sharing just that part. (2) **The m2m/reverse-FK stitch key** gained `StitchKey`,
a fixed 32-byte inline buffer, for composite owners — a composite key is not one integer and may mix
`int`/`std::string`/`int64_t`; the statement-side extractor and the object-side builder dispatch per
part on the declared type and must stay in agreement. A **single-column PK still keys the map on a
bare `std::uint64_t`**, selected by `if constexpr` on `primary_key_column_count_`: `std::hash<long>`
is the identity function and `operator==` on a `long` is one inlined compare, so routing single-PK
models through `StitchKey` cost ~4% on the m2m eager-load path (`M2MRelation` carries a second
fn-pointer for the narrow extractor; exactly one is populated). (3) **The Q2 `IN`
subquery** uses the row-value form `(a, b) IN (SELECT a, b FROM …)` — the outer comparison side is
parenthesized but the inner SELECT list is **left unwrapped**, because parenthesizing it makes
SQLite parse it as one scalar expression rather than N columns. (4) **Junction DDL** emits one
column per PK part per side, `<Side>_<part>`, with the PRIMARY KEY listing all N+M and each
`FOREIGN KEY` naming the target's real key columns. That last part also fixed a latent bug: the
old `REFERENCES <Side>(id)` named a nonexistent column for a composite side — SQLite silently
accepts an unvalidated FK target, but PG rejects it outright, so composite-PK junction tables could
not be created on PG at all. Composite PK is supported on **either or both** m2m sides.

An FK member used as a `primary_part` stores the *referenced* key's type, not the whole struct —
`pk_part_storage_type` routes through the same `is_fk_field` check as `bind_one_pk_part` (#501).
Without it, a composite key over FKs (the canonical association-table shape) hard-errors inside a
sizer with a message naming neither model nor field. Column names always go through
`append_column_name` (#422), so an FK part emits `warehouse_id`. Sizing is exact: the junction
sizer runs the **writer itself** into a counting sink, so the two cannot drift — and note
`ConstexprString<N>` holds only `N-1` bytes (its `append` stops at `len < N - 1`), so a buffer
sized to exactly the rendered length silently drops the final character.

**Foreign keys (#431)**: `[[= storm::fk<>]]` marks an FK field (bare = `RESTRICT`,
the SQL default — no `ON DELETE` clause emitted). The `ON DELETE` policy is the template
arg: `fk<RefAction::Cascade>` / `fk<RefAction::SetNull>` / `fk<RefAction::Restrict>` /
`fk<RefAction::NoAction>`. `SetNull` REQUIRES a nullable FK (`std::optional<Related>`) —
enforced at compile time by `ModelFkPoliciesValid<T>`. A class-template annotation (the flag
tag objects can't carry a parameter); FK detection runs through `meta::is_fk_field`. An FK target
must have a primary key: the `ValidForeignKey<FieldType>` concept (#474) constrains
`find_fk_primary_key` and the `FKFieldOf` gate on `join<>`/`left_join<>`, so a `join<>` on an
FK whose target lacks a PK fails at the call site (single-level — never recurses into the
target's own FKs). `ON UPDATE` is not emitted (identity PKs never change). See
[docs/guide/features/REFERENTIAL_INTEGRITY.md](docs/guide/features/REFERENTIAL_INTEGRITY.md).

**PK/FK column naming uses the real identifier, not a literal `"id"` (#506)**: the single-PK
`CREATE TABLE` branch (`append_single_pk_column_def`) emits `Base::pk_name_` (the PK member's
actual identifier) ahead of the type/constraint suffix, and the per-field FK `REFERENCES
<Related>(<pk>)` clause (`append_fk_column_def`) emits `find_fk_primary_key<FieldType>()`'s
identifier — the same helper `bind_value_by_type` already used for the FK bind splice, so DDL and
DML are guaranteed to name the same column. Previously both hardcoded the literal `"id"`, so a
model whose PK member was not named `id` got DDL and queries that disagreed at runtime ("no such
column"). Composite-PK models are unaffected (routed around this branch, #500). The consteval size
budgets (`column_size_budget`'s `pk_size`, `fk_references_len`) were widened to measure the real
identifier length instead of assuming the fixed 2-char `"id"`; `id`-named models (the ~50 existing
ones) stay byte-identical. The m2m auto-junction `FOREIGN KEY ... REFERENCES <Model>(id)` clause
still hardcodes `"id"` for both sides — same bug class, out of #506's scope, tracked separately as
#519.

**Many-to-many (#203)**: `[[= storm::many_to_many<>]]` (auto junction `<Owner>_<Related>`,
one junction table per field) or `[[= storm::many_to_many_through<Model>]]` on a container
member (`std::vector<T>`, `plf::hive<T>`, `vector<shared_ptr<T>>`). Not a column — invisible to
CRUD; eager-loaded via `join<fields::T.field>()`, several relations per call via
`join<fields::T.a, fields::T.b>()` (#392). The auto-junction `ON DELETE` defaults to CASCADE on both
sides, overridable via `many_to_many<RefAction::...>` (#431). See
[docs/guide/features/JOIN_OPERATIONS.md](docs/guide/features/JOIN_OPERATIONS.md).

**Reverse-FK (#398)**: `[[= storm::reverse_fk<^^Owner>]]` on a container member declares the
eager-load destination for "all `<Base>`, each with the `<Owner>`s that point at them". The argument
is the OWNER TYPE (resolved to its unique FK back at the base — the Base⟷Owner reference cycle forbids
a member splice in the annotation, so the owner must have exactly one FK to the base). Not a column.
`select()` runs the m2m two-query load (Q2 hits the owner table directly, no junction). Aggregate/filter
chains also accept a cross-model FK selector `join<fields::Owner.fk>()`, which disambiguates multiple owner
FKs (e.g. `fields::Bug.author` vs `fields::Bug.reviewer`). See
[docs/guide/features/JOIN_OPERATIONS.md](docs/guide/features/JOIN_OPERATIONS.md).

**Auto-timestamps (#209)**: `[[= storm::auto_create]]` / `[[= storm::auto_update]]` on a
`std::chrono::system_clock::time_point` field auto-stamp `now()` — `auto_create` on INSERT only,
`auto_update` on INSERT and UPDATE. **Bind-time only, no write-back** (the caller's object is never
mutated; re-SELECT to read the value). UPDATE preserves `created_at` by binding the object's stored
value, so pass the original `created_at` when updating. Zero cost on models without timestamp fields.

**No auto-timestamp on a primary key (#511)**: `auto_create`/`auto_update` on a PK member is a
**compile-time error** (`ModelTimestampPkValid<T>`, ANDed into the `BaseStatement` constraint list).
An auto-stamped key is rewritten by the statement matching on it — the #501 bug class, for the
implicit timestamp tail rather than the explicit SET targets. Detection routes through
`is_primary_member`, so it covers composite `primary_part` members too, which is the path that was
genuinely unprotected: `PrimaryKeyType` (#505) exempts `primary_part` entirely and only blocked a
single `time_point` PK *incidentally*, by that type falling off the end of its integral/UUID
whitelist. DECIDED 2026-07-29 in favour of rejection over silent exclusion, matching how #500 handles
nullable and relation-container PKs — breaking for out-of-tree models with that shape, of which there
are none in-tree. The `is_unlisted_auto_update` predicates in `update_grammar.cppm` AND
`upsert_grammar.cppm` also gained the `!is_pk_member` gate their four `is_settable_member` siblings
already had; the concept makes that gate unreachable (a model that would exercise it no longer
instantiates `BaseStatement`), so it is defence in depth only, retained against a future loosening.

**64-bit unsigned storage (#436)**: a bare `uint64_t` / `unsigned long` / `unsigned long long` field
is a **compile-time error** (`ModelStorageAnnotated<T>` constraint on `BaseStatement`). Annotate with
exactly one: `[[= storm::signed_storage]]` keeps today's signed `INTEGER`/`BIGINT` (byte-identical,
same `bind_int64`/`extract_int64` hot path, zero perf change) for values ≤ INT64_MAX; or
`[[= storm::full_unsigned]]` for order-preserving full-range `0..2⁶⁴−1` storage — SQLite zero-padded
20-char `TEXT` (lexicographic == numeric order), PG `NUMERIC(20,0)`, slower string bind/extract. Both
the `full_unsigned` bind/extract branches and the concept gate are compile-time-dispatched, so unrelated
types and signed-64/smaller integers are unaffected. Signed-64 types stay correct as `BIGINT`/`INTEGER`.

**Bounded text length (`max_length<N>`) (#493)**: `[[= storm::max_length<50>]] std::string name` bounds a
text column's length, **DB-enforced on every write path** — PG emits `VARCHAR(N)`, SQLite emits
`TEXT ... CHECK(length(col) <= N)` (both genuinely enforce; unlike Django/SQLAlchemy, whose `varchar(N)`
SQLite silently ignores). Class-template annotation `MaxLength<N>` carrying `N` as an NTTP (an enum member
can't be templated, same as `fk<Action>`), in the `field_attr.cppm` leaf module, re-exported to top-level
`storm::`. Only a text field (`std::string`/`std::string_view`/`std::optional<those>`) accepts it — a
non-text field is a **compile-time error** via `ModelMaxLengthValid<T>` (a `BaseStatement` constraint).
Nullable+bounded works: the SQLite CHECK passes on NULL. Combines with `unique`/auto-`DEFAULT` (#413)/
`indexed`; SQLite order: `<name> TEXT [NOT NULL] [DEFAULT v] [CHECK(...)] [UNIQUE]`. Detection helper
`max_length_of(member) -> optional<size_t>` mirrors `fk_on_delete_action_of`; the buffer grows via the
`ClauseSizer` sizer (`max_max_length_clause_len` folded into `regular_suffix`). No `min_length`/`check<>`
(separate follow-ups) and no client-side validation — the DB enforces.

**Field selectors — `fields::Model.field` (#518)**: the ONLY selector spelling. Both
predecessors are removed (BREAKING): `f<^^Person::age>()` is deleted, and a raw
`^^Model::member` in a selector position is a compile error. Declared per model in two
mechanical lines that name no fields, so they cannot drift:
`struct PersonT; consteval { std::meta::define_aggregate(^^PersonT, storm::field_specs_for(^^Person)); }
inline constexpr PersonT Person{};` — placed AFTER the struct, at namespace scope (a
`namespace fields` nested in an anonymous namespace makes every unqualified `fields::` in
that TU ambiguous), in a header that has `#include <meta>` and comes after `import storm;`.
`field_specs_for` emits ONE OF TWO proxies per member: a column (incl. FK) gets
`FieldRef<M>`, which derives from the stateless `where::Field<M>` and so inherits the whole
comparison surface — that inheritance is what makes the bare `fields::Person.age == 30`
work with no wrapper. An m2m/reverse-FK container gets `RelationRef<M>`, which has NO
`Field<M>` base and `= delete("...")`'d comparison operators: joinable
(`join<fields::Article.tags>()`) but never filterable, preserving #408 with an actionable
message instead of "invalid operands". Filtering THROUGH a relation is #553, not this.
`selector_info<S>()` recovers the `std::meta::info` at the API boundary; everything below
(statement classes, grammars, sizers) is unchanged `std::meta::info`. Generic code that
resolves a member by NAME (the YAML query builder) uses `selector_for<M>()`, the inverse,
since it cannot spell `fields::Model.member` for a template-parameter `Model`. `^^` remains
for DECLARATIONS — `storm_indexes`, `reverse_fk<^^Owner>` (names a type), `Indexes<>`:
**queries use `fields::`, model declarations use `^^`**. Destructuring a proxy works and is
arity-checked, but is POSITIONAL — reordering model fields silently rebinds with no
diagnostic. See [docs/guide/reference/FIELD_SELECTORS.md](docs/guide/reference/FIELD_SELECTORS.md).

**Entity concept (#472)**: `storm::meta::Entity<T>` is the compile-time structural gate for model
types — true iff `T` is a reflectable class (shipped as `std::meta::is_class_type(^^T) && requires
{ nonstatic_data_members_of(^^T, …); identifier_of(^^T); }`). `QuerySet<T>` and `BaseStatement<T>`
`require` it, so a non-model `T` (`int`, a pointer, a function type) fails at that named boundary
instead of deep inside reflection code. It sits ABOVE, and stays SEPARATE from, the semantic model
concepts (`ModelWithPrimaryKey`, `ModelStorageAnnotated`, `ModelFkPoliciesValid`) — those check model
*policy*, `Entity` checks *reflectability*. Structural only: a `union` also satisfies `Entity`, so
full model-ness is guaranteed by the semantic concepts together with it, not by `Entity` alone.

**Dialect-support concepts (#477)**: named backend-capability gates in `src/db/concept.cppm`
(`namespace storm::db`), replacing ad-hoc `if constexpr (requires { ConnType::trait; })` probes.
`SupportsPgDialect<ConnType>` (present iff the backend declares `uses_pg_dialect`) IS the
SQLite/PG dialect switch — true for PG, false for SQLite — used at `schema.cppm` (the `Dialect`
enum) and `base.cppm` (`append_order_by` NULLS FIRST/LAST). `SupportsLimitAll<ConnType>` is an
EXISTENCE probe true for BOTH backends; `append_limit_offset` still reads the bool VALUE to pick
`LIMIT ALL` (PG) vs `LIMIT -1` (SQLite). `TransactionCapable<ConnType>` names the
`in_transaction()`/`enter_transaction()`/`leave_transaction()`/`execute()`/`Error` surface that
`TransactionGuard` (#415) calls, and now constrains `TransactionGuard` + `storm::begin`/`storm::transaction`,
so a mis-typed connection fails at the `begin()` call site. NOT added: `SupportsRightJoin`
(`right_join()` was removed in #397 — no call site) and `SupportsReturning`/`SupportsStrictTables`
(declared on the connections but never read). Each backend static-asserts these next to its
`Connection` definition.

**ValidFieldInfo concept (#478)**: `storm::meta::ValidFieldInfo<MemberInfo>` is the compile-time gate
that a `std::meta::info` NTTP names a real field — `std::meta::is_nonstatic_data_member(MemberInfo) &&
std::meta::has_identifier(MemberInfo)`. It names the precondition the field selectors (and their
`Field`/`CollatedField` proxies) assumed inline, so a bad selector (a static member, a member function,
a whole-type reflection like `^^Person`, or `^^int`) fails at the named constraint instead of deep inside
`identifier_of`/`type_of`. Both requirements are load-bearing here (unlike `Entity`):
`is_nonstatic_data_member(^^int)` and `is_nonstatic_data_member(^^Type::static_member)` are both `false`,
so the concept genuinely rejects. Orthogonal to `Entity` (whole model type) and to `is_relation_field`
(the extra check excluding m2m/reverse_fk members from a column proxy — still ANDed at the call site).

See [docs/guide/reference/FIELD_TYPES.md](docs/guide/reference/FIELD_TYPES.md).

## Known Compiler Issues

- **Module cache corruption**: Run build twice
- **`import std;` not header units**: the tree uses a single `import std;` (issue #326), NOT per-header `import <header>;`. Reflection code still needs textual `#include <meta>` (`import std;` doesn't export `std::meta::`), placed BEFORE the imports in non-module TUs. See [COMPILER_ISSUES.md §9](docs/internals/compiler/COMPILER_ISSUES.md) Findings A–D.
- **std::mutex in modules**: works via `import std;` (validated under TSAN). Per-thread connections are still the recommended concurrency model for QuerySet/Connection.
- **std::function errors**: Use abstract base classes
- **C headers / macros**: `<cassert>` (the `assert` macro) and POSIX headers (`<csignal>`, `<sys/*.h>`) must stay textual `#include` — `import std;` cannot deliver macros or POSIX extensions
- **Template alias can't be specialized**: `template<T> using X = Y<T>;` doesn't allow `template<> struct X<Foo>`. Use a real class template in a dedicated module to avoid circular deps.
- **No explicit specializations in multi-GMF headers**: an explicit specialization of a module-owned template (e.g. `Indexes<Person>`) in a header included by several module TUs' GMFs breaks with "reference to 'type' is ambiguous" once the import graph offers a second BMI path (#464). Use the nested-typedef opt-in (`using storm_indexes = std::tuple<...>;` inside the model) instead. See [COMPILER_ISSUES.md §11](docs/internals/compiler/COMPILER_ISSUES.md).
- **`if constexpr` in consteval loops**: `if constexpr(f(arr[i]))` fails even in `consteval` — loop variable `i` isn't a core constant expression. Use plain `if` (both branches must compile, but that's fine in consteval).
- **Compile-time errors: use `requires`, not `throw`**: `throw "msg"` in `consteval` produces a poor error message. Instead define a concept and constrain the template — the error fires at the call site with a clear constraint violation:
  ```cpp
  // ❌ Bad — throw fires late, poor message
  static consteval auto find_pk() -> std::meta::info {
      for (auto m : members) { if (is_pk(m)) return m; }
      throw "No primary key"; // NOSONAR needed, ugly error
  }
  // ✅ Good — requires fires at call site
  template<typename T>
  concept ModelWithPrimaryKey = []() consteval -> bool {
      for (auto m : std::meta::nonstatic_data_members_of(^^T, ...))
          if (is_pk(m)) return true;
      return false;
  }();
  template<typename T> requires ModelWithPrimaryKey<T>
  class BaseStatement { ... };  // constraint violation = clear error
  ```

See [docs/internals/compiler/COMPILER_ISSUES.md](docs/internals/compiler/COMPILER_ISSUES.md).

## Thread Safety

**✅ Safe**: Per-thread connections via `thread_local`
**❌ Unsafe**: Sharing QuerySet or Connection between threads

```cpp
// ✅ Safe pattern
void worker() {
    QuerySet<Person>::set_default_connection(":memory:");
    QuerySet<Person> qs;
    qs.where(age > 30).select();
}
```

**SQLite connection tuning (#410)**: `sqlite::Config` carries `busy_timeout_ms`
(default `5000`; `0` = legacy fail-immediately) and `journal_mode`
(`JournalMode::Default`/`WAL`), applied once in `Connection::open()` (cold path,
zero query-hot-path cost). `PoolConfig` propagates both to every pooled
connection via `detail::make_conn_config<ConnType>()` (an `if constexpr (requires
{ cfg.busy_timeout_ms; })` guard keeps it compilable for PG, whose `Config` is
just `StatementCacheConfig`). WAL is silently ignored on `:memory:`/temp DBs. See
[docs/guide/features/CONNECTION_TUNING.md](docs/guide/features/CONNECTION_TUNING.md).

## QuerySet API

**Immutable `where()`**: Returns a new QuerySet — the original is never modified (Django-style).

```cpp
// Fluent chaining
auto results = QuerySet<Person>()
    .where(age > 30)
    .order_by<fields::Person.name>()
    .limit(10)
    .select();

// where() returns a copy — safe to reuse base QuerySet
auto base = QuerySet<Person>();
auto young = base.where(age < 30);    // base unchanged
auto old   = base.where(age > 50);    // base still unchanged

// Scalar aggregates (no GROUP BY) → .get()
qs.count().execute();                          // int64_t
qs.sum<fields::Person.age>().execute();             // int64_t (0 over an empty set)
qs.avg<fields::Person.salary>().execute();          // std::optional<double> — nullopt over empty set (#416)
qs.min<fields::Person.age>().execute();             // std::optional<double> — nullopt over empty set (#416)
qs.max<fields::Person.age>().execute();             // std::optional<double> — nullopt over empty set (#416)
// MIN/MAX/AVG of no rows have NO value → std::nullopt, distinguishable from a real 0.
// GROUP BY MIN/MAX/AVG tuple columns are std::optional<double> too (NULL within a group).
// sum/avg/min/max require a NumericAggregateable target field (#475): arithmetic and
// not bool, one level of std::optional<> unwrapped (nullable numeric columns are fine).
// A string/BLOB/enum/UUID/temporal/bool field is a COMPILE ERROR, not a silent coercion.
// count()/count_distinct() are unconstrained — COUNT is type-agnostic.

// GROUP BY with aggregates → .select()
qs.group_by<fields::Person.department>().count().execute();

// HAVING (only with GROUP BY) — filters groups after aggregation
qs.group_by<fields::Person.age>().having(fields::Person.age > 30).count().execute();
qs.group_by<fields::Person.dept>().count().having(fields::Person.dept == "Eng").execute();

// DISTINCT
qs.distinct<fields::Person.name>().execute();

// Column projection (SELECT specific columns, duplicates preserved)
qs.values<fields::Person.name>().execute();                      // plf::hive<std::string>
qs.values<fields::Person.name, fields::Person.age>().execute();       // plf::hive<std::tuple<std::string, int>>

// JOIN — FK field selectors use reflection NTTPs like every other field selector
message_qs.join<fields::Message.sender>().where(...).select();
message_qs.left_join<fields::Message.sender, fields::Message.receiver>().select();

// Many-to-many (#203 model/schema; #391 two-query execution; #392 multi-relation)
// — container field annotated [[= storm::many_to_many]] (auto junction) or
// many_to_many_through<Enrollment> (explicit junction model). Eager load runs as
// Q1 (base entities) + one Q2 PER relation (owner_pk, related.*) WHERE owner_id
// IN (base subquery), stitched by one pk→entity hash map, all in one transaction.
// WHERE/ORDER BY/LIMIT apply to BASE entities. Cost per extra relation is
// additive (no cartesian product). 33-46% faster than the old 1-query 3-table
// join at fan-out >= 10. See docs/guide/features/JOIN_OPERATIONS.md#execution-strategy-391.
student_qs.join<fields::Student.courses>().select();      // students with courses aggregated
student_qs.left_join<fields::Student.courses>().select(); // + students with no courses
member_qs.join<fields::Member.courses, fields::Member.clubs>().select(); // several m2m in one call (#392);
// INNER drops members empty in ANY relation, LEFT fills each independently

// Conditional bulk DELETE (#198) — deletes rows matching the current where().
qs.where(fields::Person.age > 30).erase().execute();   // → std::expected<void, Error>
// Empty where() is refused (no full-table wipe); erase_all() is the explicit wipe.

// Conditional bulk UPDATE (#403) — SET columns are compile-time member NTTPs,
// values come from a prototype; FK cols emit <name>_id; auto_update auto-stamped now().
qs.where(fields::Person.salary < 50000)
  .update<fields::Person.salary, fields::Person.is_active>(Person{.salary=60000, .is_active=true})
  .execute();                                          // → std::expected<void, Error>
// Empty where() is refused (no full-table write); update_all<>() is the explicit wipe (#409).
qs.update_all<fields::Person.department>(Person{.department="Global"}).execute(); // UPDATE … SET, no WHERE

// Upsert (#205) — single-row INSERT ... ON CONFLICT (target) DO UPDATE / DO NOTHING.
// Conflict target must be a unique field / UniqueIndex (compile-time checked).
qs.insert(p).on_conflict<fields::Person.name>().update<fields::Person.age>().execute(); // std::expected<int64_t, Error>
qs.insert(p).on_conflict<fields::Person.name>().nothing().execute();                // std::expected<std::optional<int64_t>, Error>

// Composite-PK INSERT (#502) — every key part is caller data (a composite key is
// never DB-generated), so there is nothing to return: no RETURNING is emitted.
qs.insert(order_line).execute();               // → std::expected<void, Error>
// insert<ReturnId::Yes> on a composite model is a compile-time error (ReturnIdSupported).

// Public transaction API (#415) — storm::begin(conn) returns an RAII
// storm::TransactionGuard<ConnType> (both re-exported from `storm`). BEGIN on
// begin(), explicit txn->commit(), auto-ROLLBACK on early return / throw / scope
// exit without commit. NEVER use raw conn->execute("BEGIN TRANSACTION"): chunked
// batch ops issue their own inner transaction and a raw outer BEGIN collides.
// The guard cooperates — begin() on an already-open connection returns a PASSIVE
// guard (no nested BEGIN; the outer guard owns the single commit/rollback),
// resolving the nested-BEGIN bug (#9). Nesting is tracked by a per-Connection
// depth counter (in_transaction()/enter_transaction()/leave_transaction()).
auto conn = QuerySet<Person, ConnType>::get_default_connection();
auto txn  = storm::begin(conn);
if (!txn) return std::unexpected(txn.error());
if (auto r = qs.insert(a).execute(); !r) return std::unexpected(r.error());
if (auto r = qs.update(b).execute(); !r) return std::unexpected(r.error());
return txn->commit();                                  // → std::expected<void, Error>

// Scope helper storm::transaction(conn, body) — convenience layer over begin().
// body returns std::expected<T, Error>: a value COMMITs and is forwarded, a
// std::unexpected (or throw) ROLLBACKs and propagates. Same cooperative nesting.
auto r = storm::transaction(conn, [&](auto& txn) -> std::expected<int, Error> {
    if (auto x = qs.insert(a).execute(); !x) return std::unexpected(x.error());
    if (auto x = qs.update(b).execute(); !x) return std::unexpected(x.error());
    return 42;                                          // forwarded out on commit
});                                                     // → std::expected<int, Error>
```

**Methods**: `where()`, `join()`, `order_by()`, `limit()`, `offset()`, `group_by()`, `having()`, `distinct()`, `values()`
**Aggregates**: `count()`, `sum()`, `avg()`, `min()`, `max()`
**Transactions**: `storm::begin(conn)` → `storm::TransactionGuard` (RAII), or `storm::transaction(conn, body)` scope helper; both cooperative with batch ops (#415/#9)

## Testing

```bash
# SQLite + PostgreSQL (STORM_PG_CONNSTR injected by testPreset; PG skips gracefully if not running)
ctest --preset ninja-debug

# SQLite only
ctest --preset ninja-debug-sqlite

# Filter specific tests
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"
```

See [docs/internals/testing/TESTING.md](docs/internals/testing/TESTING.md) for PostgreSQL test isolation details.

### Thorough Testing Checklist

Every new feature or modification MUST include thorough tests covering these categories:

#### Expression/Filter Features (WHERE, HAVING, future clauses)
- **All 6 comparison operators**: `==`, `!=`, `>`, `>=`, `<`, `<=`
- **Special expressions**: `IN` (multiple values), `BETWEEN` (range), `LIKE` (pattern), `IS NULL` / `IS NOT NULL` (null checks)
- **Logical combinations**: `AND`, `OR`, complex nested `(A && B) || C`
- **Type coverage**: Test with int, string, double at minimum

#### CRUD Operations (INSERT, UPDATE, DELETE)
- Single item operation
- Batch operation (multiple items)
- Batch at SQLite limit boundary (999 params)
- Operation on empty dataset
- Operation with all supported field types (int, string, double, bool, optional, blob)

#### Query Modifiers (ORDER BY, LIMIT, OFFSET, GROUP BY, DISTINCT)
- Modifier in isolation
- Modifier + WHERE
- Modifier + JOIN
- Modifier + WHERE + JOIN (all combined)
- Multiple modifiers together (e.g., ORDER BY + LIMIT + OFFSET)

#### Query Results
- Non-empty result set (happy path)
- Empty result set (filters exclude all)
- Single-row result
- Large result set (100+ rows)

#### Chaining & Caching
- Both chaining positions where applicable (e.g., `group_by().having().count()` AND `group_by().count().having()`)
- Repeated identical queries (statement caching correctness)
- Different queries on same QuerySet (cache invalidation)

#### Error Handling
- Invalid inputs where applicable
- Error paths tested via mock (test_orm_mock_errors.cpp pattern)

#### Cross-Backend
- Tests use TYPED_TEST with DatabaseTypes to run on both SQLite and PostgreSQL

## Documentation

- [docs/internals/architecture/](docs/internals/architecture/) - Design decisions, module system
- [docs/internals/building/](docs/internals/building/) - Getting started, common tasks
- [docs/internals/performance/](docs/internals/performance/) - Performance results
- [docs/guide/reference/](docs/guide/reference/) - Field types
- [docs/internals/compiler/](docs/internals/compiler/) - Compiler issues
- [docs/guide/reference/MIGRATIONS.md](docs/guide/reference/MIGRATIONS.md) - Atlas schema migrations
- [benchmarks/README.md](benchmarks/README.md) - Benchmark system guide
- [.claude/agents/rule-standards.md](.claude/agents/rule-standards.md) - C++ Core Guidelines (RAII, type safety, Rule of Zero/Five, concepts)
