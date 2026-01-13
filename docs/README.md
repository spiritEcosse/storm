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
- **[Database-Agnostic Modules](architecture/MODULE_SYSTEM.md)** - Template trick for cross-module inlining without LTO
- **[Compile-Time vs Runtime](architecture/COMPILE_TIME_VS_RUNTIME.md)** - WHERE expression design tradeoffs and performance analysis

## Development

### Guides
- **[Performance Tips](performance-tips.md)** - Transaction wrapping, batch inserts, and optimization techniques
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

### Completed:
- [x] ~~let run benchmarks parallel~~ - **REJECTED** - Parallel execution introduced high variance (90-108% vs sequential 95-96%). Added `-c` category filter instead. Made connections `thread_local` for future use.
- [x] ~~consider prepare_statement simplify~~ **REJECTED**: ~22% regression (102% → 80%)
- [x] ~~consider adding reserve - Dynamic path~~ **REJECTED**: ~2% regression
- [x] ~~Add benchmarks for select in table~~ - **DONE** - Added SELECT performance tables showing 95-103% efficiency
- [x] ~~string comparison vs pointer comparison~~ - **INVESTIGATED**: No measurable improvement (<0.1%) - SQLite execution dominates
- [x] ~~enable bugprone-exception-escape~~ - ✅ **DONE**
- [x] ~~enable bugprone-suspicious-stringview-data-usage~~ - ✅ **DONE**
- [x] ~~enable bugprone-branch-clone~~ - ✅ **DONE**
- [x] ~~enable bugprone-inc-dec-in-conditions~~ - ✅ **DONE**
- [x] ~~enable bugprone-unused-return-value~~ - ✅ **DONE**
- [x] ~~Consider adding clang-tidy in quick_commit.sh~~ - ✅ **DONE** - Added `scripts/run_clang_tidy.sh`
- [x] ~~Replace all std::vector with plf::hive~~ - ✅ **DONE**
- [x] ~~**Enable `modernize-avoid-c-arrays`**~~ - ✅ **DONE**
- [x] **Batch INSERT Performance Variance** - ✅ **SOLVED**
- [x] ~~Make select agnostic about which db is used~~ - ✅ **DONE** - Template trick for cross-module inlining. See [architecture/MODULE_SYSTEM.md](architecture/MODULE_SYSTEM.md)
- [x] ~~**Make remaining modules database-agnostic**~~ - ✅ **DONE** - Removed `#include <sqlite3.h>` using template trick:
  - [x] `insert.cppm` - Use Statement template methods for binding/execution
  - [x] `update.cppm` - Use Statement template methods for binding/execution
  - [x] `remove.cppm` - Use Statement template methods for binding/execution
  - [x] `distinct.cppm` - Use Statement template methods for extraction
  - [x] `aggregate.cppm` - Use Statement template methods for extraction
  - [x] `join.cppm` - Use Statement template methods for extraction (optimized NULL check for optional types only)
  - [x] `base.cppm` - Use Statement template methods for common utilities
  - [x] `queryset.cppm` - Remove sqlite3.h dependency
- [x] ~~Replace all hpp files with cppm modules or cpp files~~ — **Analyzed: Must remain as headers** (macros, `#embed`, dependency chain)

### Pending:
- [ ] **Fix DISTINCT on `std::optional<std::string>`** - DISTINCT queries on optional string fields return garbage values (memory corruption). See `DistinctOptionalStringFieldKnownIssue` test in `tests/test_distinct.cpp`. Note: `std::optional<int>` works correctly.
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
- [ ] **Enable `modernize-use-trailing-return-type`** - Convert all functions to trailing return type syntax
- [ ] **Enable `bugprone-easily-swappable-parameters`** - Add strong types to prevent parameter swapping bugs
- [ ] Simplify *.md files structure - move rules.md content to CLAUDE.md or docs/
