# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**📚 Full Documentation**: See [docs/README.md](docs/README.md) for comprehensive feature documentation, architecture guides, and development workflows.

## Project Overview

Storm is a modern C++26 ORM library for SQLite using cutting-edge C++26 reflection to automatically map C++ structs to database tables without macros.

**Performance Summary** (10,000 operations, Release builds, warm cache):
- **INSERT**: 23.67M/sec single, 3.04M/sec batch (24x faster than sqlite_orm)
- **SELECT**: 13.28M rows/sec (1.44x faster than sqlite_orm)
- **UPDATE**: 16.23M/sec single, 15.15M/sec batch (18x faster than sqlite_orm)
- **DELETE**: 31.51M/sec single, 32.26M/sec batch (33x faster than sqlite_orm)
- **JOIN**: 4-6M rows/sec (77% average efficiency vs raw SQLite)
- **DISTINCT**: 100-223% efficiency (JOIN: 223%, WHERE: 100-137%)
- **WHERE (detailed)**:
  - int comparison: 8.88M rows/sec (88.6% efficiency vs raw SQLite)
  - bool comparison: 9.04M rows/sec (92.8% efficiency)
  - string/LIKE pattern: 2.79M rows/sec (100% efficiency)
  - BETWEEN range: 4.91M rows/sec (99.6% efficiency)
  - IN (3 values): 2.14M rows/sec (~100% efficiency)
  - IN (10 values): 3.02M rows/sec (69.5% efficiency)
  - Simple (2 AND): 7.03M rows/sec (90.9% efficiency)
  - Medium (4 conditions): 1.32M rows/sec (95.6% efficiency)
  - Complex (8+ conditions): 0.73M rows/sec (102% efficiency)

**Note**: CRUD measurements use warm statement cache (production-realistic scenario). WHERE measurements may include cold cache overhead.

**Key Innovations**: Compile-time SQL generation, 3-level statement caching, thread-local SQL caching, optimized row extraction, fully inlined field binding, abstract base class pattern for type-erased JOIN operations, pure C++26 reflection for WHERE clauses.

## Critical Safety Rules

**⚠️ IMPORTANT: These rules must NEVER be violated:**

1. **NEVER Delete .git Repository**
   - Do not run `rm -rf .git` or any command that deletes the `.git` directory
   - The `.git` directory contains all project history and must be preserved

2. **NEVER Push Without User Approval**
   - Do not run `git push` unless explicitly requested by the user
   - Always ask for permission before pushing to remote repository
   - When committing changes, wait for user confirmation before pushing
   - Exception: If user explicitly says "commit and push", then both operations are approved

3. **MANDATORY: Benchmark After ANY Code Changes**
   - **After suggesting/implementing ANY improvement, IMMEDIATELY run benchmarks**
   - This applies to ALL changes, even "zero overhead" or "refactoring only" changes
   - **If benchmarks show ANY slowdown (even 1-2%), REVERT IMMEDIATELY**
   - Try alternative approach if available, or keep original code
   - **Never declare success without benchmark confirmation**
   - Remember: Performance > Code Cleanliness for ORMs

   **Why This Matters:**
   - Binary layout changes affect instruction cache unpredictably
   - Even removing dead code can change memory layout
   - Template/lambda changes can affect inlining decisions
   - "Unrelated" code can regress due to code placement
   - Benchmarks are the only source of truth

   **Mandatory Workflow:**
   ```bash
   # 1. Implement change
   # 2. Build release
   cmake --build --preset ninja-release

   # 3. RUN BENCHMARKS (for affected code paths)
   ./build/release/benchmarks/bench_join --size=10000 --all
   ./build/release/benchmarks/bench_where --benchmark_min_time=2s
   ./build/release/benchmarks/bench_storm --mode=select-only --test-size=10000

   # 4. Compare with baseline - if ANY regression:
   git stash  # or git checkout -- <files>

   # 5. Only after confirming zero regression:
   # Proceed with commit
   ```

## Quick Start

### Build & Test

```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release
```

### Quick Commit Workflow

```bash
# Usage: ./quick_commit.sh [commit message]

# With custom commit message
./quick_commit.sh "fix: resolve ODR violation in Message struct"

# Without message (uses auto-generated default: "chore: - run code formatting")
./quick_commit.sh
```

This script:
1. Runs clang-format on all source files
2. Runs unit tests (fails if any test fails)
3. Uses provided commit message or auto-generates one
4. Commits changes and pushes to remote

### Benchmarking

**⚠️ Always use Release builds for accurate performance measurements!**

```bash
# Python-based (recommended - auto-rebuild)
python3 bench.py --joins         # JOIN performance
python3 bench.py --compare       # All CRUD operations
python3 bench.py --all           # Complete suite

# See BENCHMARKS.md for detailed guide
```

### Prerequisites
- Custom Clang with C++26 reflection (`../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+, Ninja

See [Getting Started Guide](docs/development/getting-started.md) for detailed setup.

## Architecture Overview

### Module Structure

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

See [Architecture Documentation](docs/architecture/) for detailed design.

### Key Design Decisions

1. **C++26 Reflection-Based ORM** - Automatic field mapping using `std::meta`
2. **Concept-Based Abstraction** - PostgreSQL/MySQL support without ORM changes
3. **Compile-Time SQL Generation** - Zero runtime overhead with ConstexprString
4. **Statement-Level Caching** - 20x+ speedup for repeated operations
5. **Thread-Local SQL Caching** - 94% improvement for bulk operations
6. **Index Sequence Optimization** - Fold expressions replace recursive templates
7. **Batch Operations** - Smart thresholds (SQLite limit = 999 variables)
8. **JOIN Architecture** - Type-erased SQL builder without std::function
9. **Auto-Generated IDs** - Returns IDs from INSERT operations
10. **DISTINCT Support** - Single and multi-field with type safety

See [Design Decisions](docs/architecture/design-decisions.md) for detailed explanations.

## Performance Guidelines

**Performance testing is mandatory** for all new features. Target: ≥70% of raw SQLite efficiency.

### Workflow

```bash
# 1. Implement feature
# 2. Create benchmark: benchmarks/bench_<feature>.cpp
# 3. Run benchmarks
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_<feature> --size=10000

# 4. Compare with raw SQLite
# If Storm: 8.5M/sec, Raw: 10M/sec → 85% efficiency ✅ GOOD
# If Storm: 5M/sec, Raw: 10M/sec → 50% efficiency ❌ NEEDS WORK

# 5. Document results in docs/benchmarks/results.md
# 6. Commit with performance metrics
git commit -m "feat: add FEATURE (85% of raw SQLite)"
```

### Design Principles Balance

- **DRY/KISS** principles apply, but **performance takes precedence**
- If abstraction costs >10% performance → duplicate code
- Complex optimizations justified if >20% performance gain
- Always profile before optimizing

See [Performance Guidelines](docs/development/performance-guidelines.md) for complete rules.

## Common Development Tasks

### Adding a New Database Operation

1. Create statement class in `src/orm/statements/` (inherits `BaseStatement<T>`)
2. Implement single & batch operations
3. Choose return type: INSERT → `std::expected<int64_t/vector<int64_t>, Error>`
4. Consider statement caching pattern (see [Statement Caching](docs/reference/statement-caching.md))
5. Implement compile-time SQL generation
6. Add QuerySet method
7. Add tests in `tests/test_*.cpp`
8. Create performance benchmark

See [Common Tasks](docs/development/common-tasks.md) for detailed patterns.

## Supported Field Types

- **Integer**: `int`, `int64_t`, `long`, `unsigned` variants
- **Floating**: `double`, `float`
- **Boolean**: `bool` (stored as INTEGER 0/1)
- **String**: `std::string`, `const char*`, `std::string_view`
- **Optional**: `std::optional<T>` for any supported type (NULL support)
- **BLOB**: `std::vector<uint8_t>`, `std::vector<unsigned char>`

See [Field Types Reference](docs/reference/field-types.md) for complete mapping.

## Known Compiler Issues

**Module cache corruption**: Simply run build command twice - second attempt succeeds.

```bash
ninja storm_tests  # May fail
ninja storm_tests  # Will succeed
```

Other known issues:
- `std::mutex` segfaults in modules → Use per-thread connections
- `std::function` linker errors → Use abstract base classes
- C headers must be `#include`d, not `import`ed

See [Compiler Issues Reference](docs/reference/compiler-issues.md) for all workarounds.

## Known Issues and Findings

### DISTINCT Performance Analysis (2025-01) - CORRECTED

**Discovery**: The original "rows/sec" throughput metric was **misleading** because it measured output row count, not query latency. The corrected analysis using **latency (ms/query)** reveals the truth:

| Operation | Storm Latency | Raw Latency | Efficiency | Avg Results | True Performance |
|-----------|---------------|-------------|------------|-------------|------------------|
| DISTINCT + WHERE | **0.588ms** | 0.578ms | 98.3% | 78 rows | **FASTEST** - near-parity |
| DISTINCT + WHERE + JOIN | **0.603ms** | 0.811ms | **134.5%** | 2,620 rows | **Very fast** - 1.35x faster |
| DISTINCT + JOIN | 1.143ms | 2.590ms | **226.5%** | 10,000 rows | **Slower, but 2.27x faster than SQLite** |

**Key Insight**: The "rows/sec" metric was misleading because:
- **DISTINCT + JOIN** returns 10,000 rows → inflated throughput (8.75M rows/sec)
- **DISTINCT + WHERE** returns only 78 rows → deflated throughput (0.13M rows/sec)
- **Latency tells the truth**: WHERE is fastest at 0.588ms, JOIN is slowest at 1.143ms

**Why DISTINCT + JOIN shows 226.5% efficiency despite being slower:**
1. **Statement pointer caching** - Avoids connection cache hash lookup
2. **SQL string caching** - Avoids repeated string concatenation
3. **Zero parameter binding** - JOIN conditions are static (`ON sender_id = id`)
4. Raw SQLite's JOIN implementation is inefficient (2.590ms vs Storm's 1.143ms)

**Why DISTINCT + WHERE is fastest:**
- ✅ Statement pointer caching implemented
- ✅ SQL string caching implemented
- ✅ **Selective WHERE reduces result set** (78 rows vs 10,000 rows)
- ❌ Parameter binding overhead present but minimal

**Benchmark Methodology Note**: Always use **latency (ms/query)** for comparing query performance, not throughput. Throughput is only meaningful when comparing operations with similar result set sizes.

**Caching Architecture:**
```cpp
// Per-thread, per-field-combination caching
static thread_local std::unique_ptr<DistinctQuerySet> cached_dqs;

// Inside DistinctStatement:
mutable Statement* cached_where_stmt_;      // Avoids prepare_cached() hash lookup
mutable std::string cached_where_sql_;      // Avoids to_sql() overhead
mutable Statement* cached_where_join_stmt_; // Combined WHERE + JOIN optimization
```

**When caches are reused:**
- Same thread + same field combination (`distinct<^^Person::name>()`) → **cache shared**
- Different QuerySet instances → **cache shared** (keyed by template params, not instance)
- Same WHERE expression object → **cache preserved** across calls
- Different WHERE expressions → **cache cleared** (SQL changed)

**When new caches are created:**
- Different field combinations (`distinct<^^Person::name>()` vs `distinct<^^Person::age>()`) → **separate caches**
- Different threads → **separate caches** (`thread_local`)

**Parameter Binding Safety**: Multiple QuerySet instances can safely share the same cached statement because:
1. Each QuerySet has its own `where_expr_` object (NOT shared)
2. Parameter binding happens atomically with execution in the same method call
3. SQLite's binding is "last write wins" - each bind overwrites previous parameters
4. No window exists for another QuerySet to interfere between bind and execute

See [Parameter Binding Safety](docs/reference/statement-caching.md#parameter-binding-safety-with-shared-cached-statements) for detailed explanation.

### Thread Safety Issues (TODO: Fix)

**⚠️ CRITICAL**: The following patterns are **NOT thread-safe** and will cause data races:

#### 1. Default Connection Sharing (Documented, Not Fixed)

```cpp
// ❌ UNSAFE: Multiple threads using default connection
// Thread 1
QuerySet<Person> qs1;  // Uses static default connection
qs1.where(age > 30).select();

// Thread 2
QuerySet<Person> qs2;  // SAME static default connection - RACE CONDITION!
qs2.where(age > 50).select();
```

**Problem**: `get_default_connection_ptr()` returns `static` (not `thread_local`) connection.
**Race on**: SQLite connection internals, prepared statement maps, statement execution state.

#### 2. QuerySet Sharing Between Threads (Not Documented, Not Fixed)

```cpp
// ❌ UNSAFE: Sharing QuerySet between threads
QuerySet<Person> qs;

std::thread t1([&qs]() { qs.where(age > 30).select(); });
std::thread t2([&qs]() { qs.where(age > 50).select(); });  // RACE CONDITION!
```

**Problem**: QuerySet has mutable state (`where_expr_`, `join_stmt_`).
**Race on**: WHERE expression pointer, JOIN statement wrapper.

#### 3. Connection Sharing Between Threads (SQLite Limitation)

```cpp
// ❌ UNSAFE: Sharing connection between threads
auto conn = Connection::create("db.sqlite").value();

std::thread t1([&conn]() { QuerySet<Person>{conn}.select(); });
std::thread t2([&conn]() { QuerySet<Person>{conn}.select(); });  // RACE CONDITION!
```

**Problem**: SQLite connections are not thread-safe.
**Race on**: Statement preparation, execution, internal connection state.

#### Safe Pattern (Use This!)

```cpp
// ✅ SAFE: Per-thread connections and QuerySets
void worker_thread() {
    // Thread-local connection
    auto conn = db::sqlite::Connection::create("database.db").value();

    // Thread-local QuerySet
    QuerySet<Person> qs{conn};

    // Safe - all caching is thread-local
    for (int i = 0; i < 1000; i++) {
        qs.where(age > 30).distinct<^^Person::name>().select();
    }
}

std::thread t1(worker_thread);
std::thread t2(worker_thread);
```

**Why this is safe:**
- Each thread has its own `Connection` instance
- Each thread has its own `QuerySet` instance
- `static thread_local` caching provides isolated storage per thread
- No shared mutable state between threads

#### TODO: Improvements Needed

1. **Make default connection thread_local** (easy fix, breaking change)
2. **Document QuerySet thread safety** in public API docs
3. **Consider making QuerySet/Connection non-copyable** to prevent accidental sharing
4. **Add compile-time or runtime checks** for cross-thread usage (hard)

**Current Status**: Users must manually ensure per-thread instances. Violation = undefined behavior.

## Testing

```bash
# Run all tests (104 tests, ~0.5 seconds)
ctest --test-dir build/debug --output-on-failure

# Run specific suite
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"

# With sanitizers
cmake --preset ninja-debug -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug
ctest --test-dir build/debug
```

See [Testing Strategy](docs/development/testing.md) for comprehensive guide.

## Documentation Structure

- **[docs/architecture/](docs/architecture/)** - Module structure, design decisions, optimizations
- **[docs/development/](docs/development/)** - Getting started, common tasks, testing, performance guidelines
- **[docs/benchmarks/](docs/benchmarks/)** - Performance results, JOIN analysis, DISTINCT analysis
- **[docs/reference/](docs/reference/)** - Field types, statement caching, compiler issues
- **[BENCHMARKS.md](BENCHMARKS.md)** - Comprehensive benchmarking guide (user-facing)
- **[rules.md](rules.md)** - General C++23/26 coding standards

## Core Concepts of QuerySet

QuerySet system enables building and executing SQLite queries in a fluent, type-safe manner using C++. Key principles:

### Immutability and Chaining

- **Core Principle**: All non-terminal methods (e.g., `where()`, `join()`) return a **new query object** by copying or moving internal state
- **Fluent API**: Build complex queries in readable, chainable style:
  ```cpp
  auto results = QuerySet<Model>().where(...).order_by<...>().select();
  ```
- **Terminal Methods**: `select()` or standalone aggregates execute immediately and return results

### Clause Ordering

- **Flexible Chaining**: Chain methods in any order for convenience
- **Internal Enforcement**: SQL clauses reordered to match valid SQLite syntax:
  ```
  SELECT ... FROM ... JOIN ... WHERE ... GROUP BY ... HAVING ... ORDER BY ... LIMIT/OFFSET
  ```
- **Validation**: Invalid combinations trigger compile-time errors

### Projection and Result Types

- **Transformers**: `distinct<...>()` or `values<...>()`
- **Without Transformers**: `select()` returns `std::vector<Model>`
- **With Transformers**: `select()` returns `std::vector<std::tuple<...>>` or `std::vector<type>`
- **Standalone Aggregates**: `qs.min<...>()`, `qs.max<...>()` return scalar values

### Available Methods in All Modes

- `join<OtherModel>()`: Adds JOIN clause
- `where(Condition)`: Filters rows
- `order_by<Cols...>()`: Sorts results
- `limit(int)`: Restricts result count
- `offset(int)`: Skips results
- `group_by<Cols...>()`: Groups results
- `having(Condition)`: Filters groups

### Modes and Transformers

QuerySet operates in different **modes** via **transformers**:

**Default Mode (Object Mode)**:
- Begins with `QuerySet<Model>`
- `select()` returns model vectors
- Use transformers to enter Tuple or Aggregate Mode

**Tuple Mode**:
- Entry: `distinct<Cols...>()` or `values<Cols...>()`
- Returns specialized objects with projected columns
- `select()` yields tuples

**Aggregate Mode**:
- Entry: `min<Col>()`, `max<Col>()`, `sum<Col>()`, `avg<Col>()`, `count<Col|*>()`
- Accumulates aggregates
- `select()` yields tuples of aggregate values

**Mode Precedence**:
- Modes combine via chaining transformers
- Final mode determined by: Tuple if projection used, Aggregate if aggregates present
- Prior state transfers to new objects

---

**For detailed information, see [docs/](docs/) directory.**
