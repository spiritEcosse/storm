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
7. **ALWAYS run sanitizer builds after code changes** - `ninja-asan-ubsan` (memory + UB) and `ninja-tsan` (data races); revert if new violations appear
8. **ALWAYS update docs AND agent files after changes** - Code + docs + `.claude/agents/*.md` commit together. If you change a feature, preset, command, or pattern described in any agent file, update that agent file too.
9. **ALWAYS write thorough unit tests BEFORE implementing** - Every feature or fix needs comprehensive tests first (see [Testing Checklist](#thorough-testing-checklist)). Workflow: (1) write tests → (2) run — new tests MUST fail (proves they test real behavior) → (3) implement → (4) run again — ALL tests must pass
10. **SonarCloud gate MUST pass before merging** - Zero issues on new code; no exceptions, even for minor issues (see [SonarCloud Gate](#sonarcloud-gate-mandatory-before-merge))
11. **NEVER use `throw` for compile-time errors in `consteval` functions** - Use `requires` constraints instead. Define a concept that checks the condition and constrain the template. The `throw "string literal"` trick works but fires late with a poor error message. `requires` fires at the call site with a clear constraint violation. Use `std::unreachable()` after the loop body if needed to satisfy the return type.
12. **NEVER close an issue without verifying all subtasks** - Before closing, read the issue body and confirm every "Definition of done" checkbox was genuinely completed. Only check off items that were delivered. If some are intentionally skipped, ask the user first.

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
| `ninja-asan-ubsan` | Debug | ✓ | — | — | ASAN+UBSAN | — | Memory safety + undefined behavior |
| `ninja-tsan` | Debug | ✓ | — | — | TSAN | — | Data race detection |
| `ninja-msan` | Debug | ✓ | — | — | MSAN | — | Uninitialized memory reads |

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
├── format.cmake          # clang-format/cmake-format targets (see docs/development/FORMATTING.md)
└── tools.cmake           # ENABLE_TOOLS option + add_subdirectory(tools) (storm-schema CLI)
```

### GitHub Issue Workflow
- **Before starting**: Read the issue body with `gh issue view <N>`. Check for a **"Definition of done"** section or checkbox subtasks (e.g., `- [ ] Each error path has a test`).
- **Verify context**: Before planning or making changes, cross-check the issue description (file paths, line numbers, API references, assumptions) against the actual codebase. If anything is outdated or wrong, **report discrepancies to the user** before proceeding.
- **Track subtasks**: Use those checkboxes as your acceptance criteria. **Before committing**, check off all verified subtasks: `gh issue edit <N> --body "..."` with `- [x]` replacing `- [ ]`. Do not defer this to after the commit or merge.
- **Close when done**: After all subtasks are checked off, close the issue with `gh issue close <N>`.

### Branching Rules
- **GitHub Issue work**: Create and link a feature branch using `gh issue develop <N> --name feature/<N>-<short-description> --base develop --checkout` — this creates the branch, links it to the issue in GitHub, and checks it out in one step.
- **Create pull request**: After pushing a feature branch, ALWAYS create a PR with `gh pr create --base develop` including `Closes #<N>` in the body to auto-link and auto-close the issue on merge.
- **After creating a PR**: Wait 30 seconds, then run `/sonarcloud-status`. If there are **zero issues** on new code, check CI jobs with `gh pr checks <PR#> --watch`. Only merge after **both** SonarCloud gate AND all CI jobs (ninja-debug, ninja-asan-ubsan, ninja-tsan) pass (`gh pr merge --squash`). If ANY SonarCloud issues or CI failures, fix them all, push, and re-check until clean.
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

See [docs/development/CODE_COVERAGE.md](docs/development/CODE_COVERAGE.md) for details.

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
    ├── utilities.cppm          # ConstexprString, SQLCache
    ├── indexes.cppm            # Index, UniqueIndex, Indexes<T> trait (namespace storm)
    └── statements/             # INSERT, SELECT, UPDATE, DELETE, DISTINCT, JOIN
```

See [docs/architecture/](docs/architecture/) for design decisions.

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
> see [STATEMENT_CACHING.md](docs/architecture/STATEMENT_CACHING.md)). The former
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

See [docs/development/PERFORMANCE_GUIDELINES.md](docs/development/PERFORMANCE_GUIDELINES.md).

## Supported Field Types

`int`, `int64_t`, `double`, `float`, `bool`, `std::string`, `std::string_view`, `std::optional<T>`, `std::vector<uint8_t>` (BLOB)

**Auto-timestamps (#209)**: `[[= FieldAttr::auto_create]]` / `[[= FieldAttr::auto_update]]` on a
`std::chrono::system_clock::time_point` field auto-stamp `now()` — `auto_create` on INSERT only,
`auto_update` on INSERT and UPDATE. **Bind-time only, no write-back** (the caller's object is never
mutated; re-SELECT to read the value). UPDATE preserves `created_at` by binding the object's stored
value, so pass the original `created_at` when updating. Zero cost on models without timestamp fields.

See [docs/reference/FIELD_TYPES.md](docs/reference/FIELD_TYPES.md).

## Known Compiler Issues

- **Module cache corruption**: Run build twice
- **`import std;` not header units**: the tree uses a single `import std;` (issue #326), NOT per-header `import <header>;`. Reflection code still needs textual `#include <meta>` (`import std;` doesn't export `std::meta::`), placed BEFORE the imports in non-module TUs. See [COMPILER_ISSUES.md §9](docs/development/COMPILER_ISSUES.md) Findings A–D.
- **std::mutex in modules**: works via `import std;` (validated under TSAN). Per-thread connections are still the recommended concurrency model for QuerySet/Connection.
- **std::function errors**: Use abstract base classes
- **C headers / macros**: `<cassert>` (the `assert` macro) and POSIX headers (`<csignal>`, `<sys/*.h>`) must stay textual `#include` — `import std;` cannot deliver macros or POSIX extensions
- **Template alias can't be specialized**: `template<T> using X = Y<T>;` doesn't allow `template<> struct X<Foo>`. Use a real class template in a dedicated module to avoid circular deps.
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

See [docs/development/COMPILER_ISSUES.md](docs/development/COMPILER_ISSUES.md).

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

## QuerySet API

**Immutable `where()`**: Returns a new QuerySet — the original is never modified (Django-style).

```cpp
// Fluent chaining
auto results = QuerySet<Person>()
    .where(age > 30)
    .order_by<^^Person::name>()
    .limit(10)
    .select();

// where() returns a copy — safe to reuse base QuerySet
auto base = QuerySet<Person>();
auto young = base.where(age < 30);    // base unchanged
auto old   = base.where(age > 50);    // base still unchanged

// Scalar aggregates (no GROUP BY) → .get()
qs.count().execute();                          // int64_t
qs.sum<^^Person::age>().execute();             // int64_t
qs.avg<^^Person::salary>().execute();          // double

// GROUP BY with aggregates → .select()
qs.group_by<^^Person::department>().count().execute();

// HAVING (only with GROUP BY) — filters groups after aggregation
qs.group_by<^^Person::age>().having(field<^^Person::age>() > 30).count().execute();
qs.group_by<^^Person::dept>().count().having(field<^^Person::dept>() == "Eng").execute();

// DISTINCT
qs.distinct<^^Person::name>().execute();

// Column projection (SELECT specific columns, duplicates preserved)
qs.values<^^Person::name>().execute();                      // plf::hive<std::string>
qs.values<^^Person::name, ^^Person::age>().execute();       // plf::hive<std::tuple<std::string, int>>

// JOIN — FK field selectors use reflection NTTPs like every other field selector
message_qs.join<^^Message::sender>().where(...).select();
message_qs.left_join<^^Message::sender, ^^Message::receiver>().select();
```

**Methods**: `where()`, `join()`, `order_by()`, `limit()`, `offset()`, `group_by()`, `having()`, `distinct()`, `values()`
**Aggregates**: `count()`, `sum()`, `avg()`, `min()`, `max()`

## Testing

```bash
# SQLite + PostgreSQL (STORM_PG_CONNSTR injected by testPreset; PG skips gracefully if not running)
ctest --preset ninja-debug

# SQLite only
ctest --preset ninja-debug-sqlite

# Filter specific tests
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"
```

See [docs/development/TESTING.md](docs/development/TESTING.md) for PostgreSQL test isolation details.

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

- [docs/architecture/](docs/architecture/) - Design decisions, module system
- [docs/development/](docs/development/) - Getting started, common tasks, performance
- [docs/benchmarks/](docs/benchmarks/) - Performance results
- [docs/reference/](docs/reference/) - Field types, compiler issues
- [docs/development/MIGRATIONS.md](docs/development/MIGRATIONS.md) - Atlas schema migrations
- [benchmarks/README.md](benchmarks/README.md) - Benchmark system guide
- [.claude/agents/rule-standards.md](.claude/agents/rule-standards.md) - C++ Core Guidelines (RAII, type safety, Rule of Zero/Five, concepts)
