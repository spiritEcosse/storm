# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

**📚 Full Documentation**: [docs/README.md](docs/README.md)

## Project Overview

Storm is a C++26 ORM library for SQLite using compile-time reflection to automatically map C++ structs to database tables without macros.

**Performance**: 96-108% efficiency vs raw SQLite (Release builds). See [benchmarks/README.md](benchmarks/README.md).

**Key Features**: Compile-time SQL generation, 3-level statement caching, thread-local caching, type-erased JOINs, pure C++26 reflection for WHERE clauses.

## Critical Safety Rules

**⚠️ NEVER violate these rules:**

1. **NEVER delete `.git`** - Do not run `rm -rf .git`
2. **NEVER push without approval** - Ask before `git push` (exception: user says "commit and push")
3. **ALWAYS benchmark after code changes** - Use Release builds; revert if ANY slowdown
4. **ALWAYS update docs after changes** - Code + docs commit together
5. **Pre-commit hook enforces checks** - `commit.sh` runs automatically on `git commit` (format, tidy, test, coverage, sonar)
6. **ALWAYS show files before commit** - Run `git status --short`, get user approval, then commit
7. **ASK before creating new `.md` files**
8. **UPPERCASE doc filenames** - `GETTING_STARTED.md`, not `getting-started.md`
9. **ALWAYS write thorough unit tests** - Every feature needs comprehensive tests (see Testing Checklist)

## Quick Start

### CMake Presets

| Preset | Build type | Tests | Coverage | Bench | Use for |
|---|---|---|---|---|---|
| `ninja-debug` | Debug | ✓ | ✓ | — | Development, coverage |
| `ninja-release` | Release | ✓ | — | ✓ | CI, benchmarking |
| `ninja-prod` | Release | — | — | — | Production artifact |

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
└── format.cmake          # clang-format/cmake-format targets (see docs/development/FORMATTING.md)
```

### GitHub Issue Workflow
- **Before starting**: Read the issue body with `gh issue view <N>`. Check for a **"Definition of done"** section or checkbox subtasks (e.g., `- [ ] Each error path has a test`).
- **Verify context**: Before planning or making changes, cross-check the issue description (file paths, line numbers, API references, assumptions) against the actual codebase. If anything is outdated or wrong, **report discrepancies to the user** before proceeding.
- **Track subtasks**: Use those checkboxes as your acceptance criteria. After completing each one, update the issue to mark it done: `gh issue edit <N> --body "..."` with `- [x]` replacing `- [ ]`.
- **Close when done**: After all subtasks are checked off, close the issue with `gh issue close <N>`.

### Branching Rules
- **GitHub Issue work**: ALWAYS create a feature branch `feature/<issue-number>-<short-description>` from `develop` BEFORE starting any work. Never work directly on `develop` for issue-linked tasks.
- **Link branch to issue**: After creating the feature branch, link it to the issue: `gh issue develop <N> --branch feature/<issue-number>-<short-description>`
- **Create pull request**: After pushing a feature branch, ALWAYS create a PR with `gh pr create --base develop` including `Closes #<N>` in the body to auto-link and auto-close the issue on merge.
- **Close issue after merge**: After merging a feature branch into `develop`, ALWAYS close the issue with `gh issue close <N>`. Do NOT wait to be asked.
- **Ad-hoc fixes** (no GitHub Issue): Work directly on `develop`.

### SonarCloud Gate (MANDATORY before merge)

The `/sonarcloud-status` skill (and `./scripts/sonarcloud-check.sh`) is **branch-aware**:

| Context | Mode | What it checks |
|---|---|---|
| `develop` / `master` / `main` | **Branch mode** | Full project — ALL existing issues on the branch |
| Feature branch or explicit PR number | **PR mode** | New code only — waits for Sonar to finish, then checks changed lines |

**Before merging a PR (run from the feature branch):**
1. Run `/sonarcloud-status` — it auto-detects the PR and **waits** for SonarCloud analysis to finish.
2. **If gate passes** (no issues on new code): merge the PR into `develop`.
3. **If gate fails** (any issues — code duplication, bugs, code smells, security hotspots, even minor ones): fix ALL reported issues on the feature branch, push the fixes, then re-check SonarCloud until the gate passes.
4. **Only merge after a clean SonarCloud gate** — no exceptions, even for minor issues.

**Checking overall project health (on `develop`):**
Run `/sonarcloud-status` while on `develop` to see the full project picture — all existing issues, overall metrics, and quality gate status for the branch.

### Commit & Push Workflow
```bash
git status --short           # Show files
# Get user approval
git add -A && git commit -m "message"
# Pre-commit hook (commit.sh): format → tidy → tests → coverage
# Smart skips: no C++/cmake → skip all; cmake-only → tests+coverage+cmake-format; C++ only-bench → skip tests/coverage

git push
# Pre-push hook (.githooks/pre-push): build-wrapper → sonar-scanner upload
# → waits for SonarCloud analysis via CE task polling
# → checks quality gate → blocks push if FAILED, allows if OK
# Requires SONAR_TOKEN env var (skipped gracefully if unset)
```

### Benchmarking (Release only!)
```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench --quick     # Development (~3-5 min)
./build/release/benchmarks/storm_bench --thorough  # Pre-commit (~15-20 min)
./build/release/benchmarks/storm_bench -c SELECT   # Category filter
```

### Code Coverage
```bash
# ninja-debug has coverage enabled by default
cmake --preset ninja-debug && cmake --build --preset ninja-debug

# Console summary (quick) — ninja-debug-coverage injects STORM_PG_CONNSTR
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
    └── statements/             # INSERT, SELECT, UPDATE, DELETE, DISTINCT, JOIN
```

See [docs/architecture/](docs/architecture/) for design decisions.

### Key Design Decisions
1. C++26 reflection for automatic field mapping
2. Concept-based DB abstraction (PostgreSQL/MySQL ready)
3. Compile-time SQL generation (zero runtime overhead)
4. Statement + thread-local caching (20x+ speedup)
5. Batch operations with smart thresholds (SQLite limit = 999)
6. Type-erased JOINs via abstract base class

## Performance Guidelines

**Target**: ≥95% of raw SQLite efficiency. Performance > code cleanliness.

### Hot Path Optimizations

| Optimization | Improvement | When to Use |
|--------------|-------------|-------------|
| Flat code over nested lambdas | ~3-4% | Hot paths, inner loops |
| Statement pointer caching | ~23% | Single-row ops in loops |
| Raw pointer caching in loops | ~5-6% | Query extraction loops |
| Expression address caching | Skip SQL build | Repeated WHERE queries |
| Template methods for modules | ~1-3% | Cross-module hot paths |

```cpp
// Cache statement pointer (23% faster)
if (!cached_stmt_) cached_stmt_ = conn_->prepare_cached(sql);
cached_stmt_->reset();

// Cache raw pointer in loops (5-6% faster)
sqlite3_stmt* raw = stmt->handle();
while (sqlite3_step(raw) == SQLITE_ROW) { ... }
```

### Fair Benchmark Rules
- Setup outside loop, execute inside
- Same algorithm, containers, decision logic for both Storm and raw
- Use latency (ms/query) for different result sizes

See [docs/development/PERFORMANCE_GUIDELINES.md](docs/development/PERFORMANCE_GUIDELINES.md).

## Supported Field Types

`int`, `int64_t`, `double`, `float`, `bool`, `std::string`, `std::string_view`, `std::optional<T>`, `std::vector<uint8_t>` (BLOB)

See [docs/reference/FIELD_TYPES.md](docs/reference/FIELD_TYPES.md).

## Known Compiler Issues

- **Module cache corruption**: Run build twice
- **std::mutex segfaults**: Use per-thread connections
- **std::function errors**: Use abstract base classes
- **C headers**: Must `#include`, not `import`

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

```cpp
// Fluent chaining
auto results = QuerySet<Person>()
    .where(age > 30)
    .order_by<^^Person::name>()
    .limit(10)
    .select();

// Scalar aggregates (no GROUP BY) → .get()
qs.count().get();                          // int64_t
qs.sum<^^Person::age>().get();             // int64_t
qs.avg<^^Person::salary>().get();          // double

// GROUP BY with aggregates → .select()
qs.group_by<^^Person::department>().count().select();

// HAVING (only with GROUP BY) — filters groups after aggregation
qs.group_by<^^Person::age>().having(field<^^Person::age>() > 30).count().select();
qs.group_by<^^Person::dept>().count().having(field<^^Person::dept>() == "Eng").select();

// DISTINCT
qs.distinct<^^Person::name>().select();

// Column projection (SELECT specific columns, duplicates preserved)
qs.values<^^Person::name>().select();                      // plf::hive<std::string>
qs.values<^^Person::name, ^^Person::age>().select();       // plf::hive<std::tuple<std::string, int>>

// JOIN
qs.join<Message>().where(...).select();
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
- **Special expressions**: `IN` (multiple values), `BETWEEN` (range), `LIKE` (pattern)
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
- [benchmarks/README.md](benchmarks/README.md) - Benchmark system guide
