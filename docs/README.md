# Storm ORM Documentation

Welcome to Storm ORM - a modern C++26 ORM library for SQLite using cutting-edge reflection to automatically map C++ structs to database tables.

## Quick Links

- **[Main Project Guide (CLAUDE.md)](../CLAUDE.md)** - Quick start and common tasks
- **[Benchmarks](../benchmarks/README.md)** - Unified benchmark system documentation

## Features

### Core Operations
- **[CRUD Operations](features/CRUD_OPERATIONS.md)** - INSERT, UPDATE, DELETE with auto-generated IDs
- **[SELECT Queries](features/SELECT_QUERIES.md)** - Optimized row extraction and statement caching
- **[WHERE Clauses](features/WHERE_CLAUSES.md)** - Type-safe filtering with pure C++26 reflection
- **[JOIN Operations](features/JOIN_OPERATIONS.md)** - Single and multi-FK JOINs with type erasure
- **[Batch Operations](features/BATCH_OPERATIONS.md)** - Bulk INSERT/UPDATE/DELETE with smart thresholds

## Architecture

### Core Systems
- **[C++26 Reflection](architecture/REFLECTION.md)** - How Storm uses std::meta for ORM mapping
- **[Statement Caching](architecture/STATEMENT_CACHING.md)** - 3-level caching achieving near-raw SQLite performance
- **[SQL Generation](architecture/SQL_GENERATION.md)** - Compile-time SQL generation with ConstexprString
- **[Module System](architecture/MODULE_SYSTEM.md)** - C++26 module structure and dependencies
- **[Compile-Time vs Runtime](architecture/COMPILE_TIME_VS_RUNTIME.md)** - WHERE expression design tradeoffs and performance analysis

## Development

### Guides
- **[Compiler Attributes](development/COMPILER_ATTRIBUTES.md)** - Guide for using hot, flatten, and always_inline attributes
- **[Compiler Issues](development/COMPILER_ISSUES.md)** - Known workarounds for clang-p2996
- **[Performance Testing](development/PERFORMANCE_TESTING.md)** - Benchmarking guidelines and workflow
- **[Adding Features](development/ADDING_FEATURES.md)** - How to add new database operations

## Performance Summary

Storm ORM achieves **1.5-6x performance advantage** over sqlite_orm:

| Operation | Storm ORM | Raw SQLite | Storm vs sqlite_orm |
|-----------|-----------|------------|---------------------|
| INSERT (single) | 992K/sec | 49M/sec | 2.0x faster |
| INSERT (batch) | 2.7M/sec | - | 6.4x faster |
| SELECT (all rows) | 13.07M rows/sec | 17.67M rows/sec | 1.51x faster |
| UPDATE (single) | 2M/sec (12M peak) | 1.09M/sec | 6x faster |
| DELETE (single) | 21.6M/sec | 29.4M/sec | 36.6x faster |
| DELETE (batch) | 3.9M/sec | - | - |
| JOIN | 4-6M rows/sec | 5-7.4M rows/sec | 77% avg efficiency |
| WHERE | 9.45-11.88M rows/sec | 10.57-13.82M rows/sec | 86-90% efficiency |

## Key Innovations

1. **Compile-Time SQL Generation** - Zero runtime SQL construction overhead
2. **3-Level Statement Caching** - QuerySet → Statement → Connection caching
3. **Thread-Local SQL Caching** - 8-entry cache for bulk operations
4. **Optimized Row Extraction** - resize() pre-allocation, direct string construction
5. **Type-Erased JOIN Pattern** - Abstract base class pattern without std::function
6. **Pure C++26 Reflection WHERE** - No macros, fully module-compatible

## Getting Help

- Check **[CLAUDE.md](../CLAUDE.md)** for quick start and common tasks
- Browse feature docs for detailed usage examples
- See architecture docs for implementation details
- Consult development docs for contributing guidelines


## TODO: 
- [ ] - let run benchmarks parallel, to quickly check many cases, so use count of cores 
and run 10, 100, 500, 1k, 5k, 10k, 50k, 100k
- [x] ~~consider prepare_statement simplify – adding the check inside the lambda~~ **REJECTED**: ~22% regression (102% → 80%) - check runs 10k times per iteration instead of once
- [x] ~~consider adding reserve - Dynamic path~~ **REJECTED**: ~2% regression - function call overhead exceeds reallocation savings
- [ ] - we have so many *.md files, lets simplify and struct them, so all rules.md must be inside CLAUDE.md if 
the file CLAUDE is too big – lets move rules.md to the docs. what actually is UPDATE_OPTIMIZATION_REPORT.md ?
if it is important, move to according folder in docs. What is docs/development folder is ?
Add a rule if needed to create a *.md file – use docs directory for all docs.
- [x] ~~Add benchmarks for select in table of benchmarks/README.md~~ - **DONE** - Added SELECT performance tables (simple, JOIN, WHERE+JOIN) showing 95-103% efficiency
- [ ] - Make select agnostic about which db is used (for now it's sqlite_orm and could be psql and others)
- [x] ~~string comparison vs pointer comparison~~ - **INVESTIGATED**: Micro-benchmark proves O(1) vs O(n) (20-80x faster for pure comparison), but real ORM benchmarks show no measurable improvement (<0.1%) because SQLite execution dominates. String comparison is adequate.
- [x] ~~enable bugprone-exception-escape~~ - ✅ **DONE** - Removed `noexcept` from functions that may allocate std::string
- [x] ~~enable bugprone-suspicious-stringview-data-usage~~ - ✅ **DONE** - Use `sql.size()` with `sqlite3_prepare_v2` or convert to `std::string`
- [x] ~~enable bugprone-branch-clone~~ - ✅ **DONE** - Combined identical int64/uint64 branches in type extraction
- [x] ~~enable bugprone-inc-dec-in-conditions~~ - ✅ **DONE** - Separated increment into local variable assignment in fold expressions
- [x] ~~enable bugprone-unused-return-value~~ - ✅ **DONE** - Added `std::ignore =` for intentionally ignored returns in tests
- [x] ~~Consider adding clang-tidy in quick_commit.sh, but exclude third_party~~ - ✅ **DONE** - Added `scripts/run_clang_tidy.sh` with parallel execution, modernize checks, third_party exclusion. Runs by default in `quick_commit.sh` (use `--no-tidy` to skip)
- [x] ~~Replace all std::vector with plf::hive~~ - ✅ **DONE** - All QuerySet `select()` methods now return `plf::hive<T>` for stable iterators and efficient insertion
- [ ] **Verify Performance Code Compliance** - Audit all statement implementations against CLAUDE.md performance rules:
  - [ ] Raw pointer caching in hot loops (SELECT, DISTINCT extraction loops)
  - [ ] Statement pointer caching for single-row operations (execute_one, remove_one)
  - [ ] Expression address caching for WHERE clause reuse
  - [ ] Flat code pattern in hot paths (no nested lambdas) Prove it with benchmarks
  - [ ] Cache invalidation on reset() to prevent ABA problem
- [ ] **Verify Benchmark Fairness** - Audit all benchmarks against CLAUDE.md fair benchmark rules:
  - [ ] Setup outside loop, execute inside (WHERE, bind, prepare)
  - [ ] Same algorithm for Storm and raw SQLite
  - [ ] Same container types (plf::hive for both)
  - [ ] Runtime decision logic for both (no compile-time advantages for raw)
  - [ ] Correct metric (latency for different result sizes)
- [ ] Lets think how to add true statistics in README files (like benchmarks)
- [ ] **Enable `modernize-use-trailing-return-type`** - Convert all functions to trailing return type syntax (`auto foo() -> int` instead of `int foo()`) for consistent modern style (~50+ functions)
- [x] ~~**Enable `modernize-avoid-c-arrays`**~~ - ✅ **DONE** - Enabled (0 warnings, prevents future C-array usage)
- [ ] **Enable `bugprone-easily-swappable-parameters`** - Add strong types to prevent parameter swapping bugs (e.g., `FieldCount`, `BatchSize` wrappers instead of raw `size_t`)
- [ ] **Fix DISTINCT on `std::optional<std::string>`** - DISTINCT queries on optional string fields return garbage values (memory corruption). See `DistinctOptionalStringFieldKnownIssue` test in `tests/test_distinct.cpp`. Note: `std::optional<int>` works correctly.
- [x] **Batch INSERT Performance Variance** - ✅ **SOLVED**

  **Problem**: Small batch operations (batch_10) showed high measurement variance (72-88% efficiency range across runs).

  **Root Causes Identified**:
  1. ❌ **Unfair benchmark comparison** - Raw SQLite wasn't using chunked bulk SQL strategy (fixed in commit f06d51b)
  2. ❌ **Suboptimal thresholds** - Hardcoded BATCH_THRESHOLD didn't utilize SQLite's 999 variable limit (fixed in commit 2c787cc)

  **Solutions Implemented**:
  1. ✅ **Fair apples-to-apples comparison** - Both Storm ORM and raw SQLite now use identical chunked bulk SQL strategy
  2. ✅ **Field-aware adaptive thresholds** - `calculate_adaptive_threshold()` now computes optimal batch sizes based on struct field count (999/field_count)

  **Current Results** (1000 iterations, Release build):
  ```
  insert_batch_10:    115.0% efficiency (1.66 M ops/sec vs 1.45 M ops/sec)
  insert_batch_100:   112.9% efficiency (2.82 M ops/sec vs 2.50 M ops/sec)
  insert_batch_1000:  113.7% efficiency (2.84 M ops/sec vs 2.50 M ops/sec)
  insert_batch_10000: 114.3% efficiency (2.85 M ops/sec vs 2.49 M ops/sec)
  ```

  **Conclusion**: Storm ORM now shows **consistent 113-115% efficiency** across ALL batch sizes, including previously problematic batch_10. The 72-88% low performance was a measurement artifact from unfair comparisons and suboptimal thresholds, not a real performance issue.
