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
5. **Pre-commit hook enforces checks** - `commit.sh` runs automatically on `git commit` (format, tidy, test, coverage, sonar, bench)
6. **ALWAYS show files before commit** - Run `git status --short`, get user approval, then commit
7. **ASK before creating new `.md` files**
8. **UPPERCASE doc filenames** - `GETTING_STARTED.md`, not `getting-started.md`

## Quick Start

### Build & Test
```bash
# Debug
cmake --preset ninja-debug -DENABLE_TESTS=ON && cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release
cmake --preset ninja-release && cmake --build --preset ninja-release
```

### Commit Workflow
```bash
git status --short           # Show files
# Get user approval
git add -A && git commit -m "message"  # Pre-commit hook runs all checks automatically

# Skip optional checks if needed:
SKIP_COVERAGE=1 git commit -m "message"
SKIP_BENCH=1 git commit -m "message"
SKIP_SONAR=1 git commit -m "message"
```

### Benchmarking (Release only!)
```bash
cmake --preset ninja-release -DENABLE_BENCH=ON && cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench --quick     # Development (~3-5 min)
./build/release/benchmarks/storm_bench --thorough  # Pre-commit (~15-20 min)
./build/release/benchmarks/storm_bench -c SELECT   # Category filter
```

### Code Coverage
```bash
# Configure (one-time)
cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON

# Console summary (quick)
cmake --build build/coverage && cmake --build build/coverage --target coverage-filtered

# HTML report (detailed)
cmake --build build/coverage --target coverage-filtered-html
# Open build/coverage/coverage/html-filtered/index.html
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

// GROUP BY with aggregates
qs.group_by<^^Person::department>().count().select();

// DISTINCT
qs.distinct<^^Person::name>().select();

// JOIN
qs.join<Message>().where(...).select();
```

**Methods**: `where()`, `join()`, `order_by()`, `limit()`, `offset()`, `group_by()`, `having()`, `distinct()`, `values()`
**Aggregates**: `count()`, `sum()`, `avg()`, `min()`, `max()`

## Testing

```bash
# SQLite only
ctest --test-dir build/debug --output-on-failure

# With PostgreSQL (parallel — ~10x speedup)
STORM_PG_CONNSTR="host=host.containers.internal port=5432 dbname=storm_db user=storm_db password=storm_db" \
  ctest --test-dir build/debug -j$(nproc) --output-on-failure

# Filter specific tests
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"
```

See [docs/development/TESTING.md](docs/development/TESTING.md) for PostgreSQL test isolation details.

## Documentation

- [docs/architecture/](docs/architecture/) - Design decisions, module system
- [docs/development/](docs/development/) - Getting started, common tasks, performance
- [docs/benchmarks/](docs/benchmarks/) - Performance results
- [docs/reference/](docs/reference/) - Field types, compiler issues
- [benchmarks/README.md](benchmarks/README.md) - Benchmark system guide
