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
- **DISTINCT**: ~100% efficiency (parity with raw SQLite)
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
