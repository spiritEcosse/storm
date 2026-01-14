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

### Coverage Summary

| Category | Implementation | Unit Tests | Benchmarks |
|----------|---------------|------------|------------|
| CRUD Operations | ✅ 100% | ✅ 100% | ✅ 100% |
| WHERE Clauses | ✅ 100% | ✅ 100% | ⚠️ 57% (4/7 operators) |
| JOIN Operations | ✅ 100% | ✅ 100% | ⚠️ 25% (INNER only) |
| DISTINCT | ✅ 100% | ✅ 100% | ⚠️ 50% (single field only) |
| LIMIT/OFFSET | ✅ 100% | ✅ 100% | ✅ 100% |
| ORDER BY | ✅ 100% | ✅ 100% | ❌ 0% |
| GROUP BY | ✅ 100% | ✅ 100% | ❌ 0% |
| Aggregates | ✅ 100% | ✅ 100% | ✅ 100% |
| HAVING | ❌ 0% | ❌ 0% | ❌ 0% |

**Total: 290 unit tests, 96 benchmarks**

---

### Missing Benchmarks (Priority: High)

Features implemented and tested but not benchmarked for performance:

- [x] **LIMIT/OFFSET Benchmarks** - ✅ Implemented (17 tests, 89-105% efficiency)
  - [x] `select_limit_10/50/100/500/1000` - LIMIT on 10K rows
  - [x] `select_offset_100/500` - OFFSET only (with LIMIT -1)
  - [x] `select_limit_offset_page1/10/50/deep` - LIMIT + OFFSET pagination patterns
  - [x] `select_where_limit_100/500` - Combined LIMIT + WHERE
  - [x] `select_join_limit_100/500` - LIMIT with JOIN operations
  - [x] `select_join_limit_offset_page1/50` - JOIN + pagination

- [x] **ORDER BY Benchmarks** - ✅ Implemented (6 tests, 96-101% efficiency)
  - [x] `order_by_single_asc` - Single field ascending (98.7%)
  - [x] `order_by_single_desc` - Single field descending (99.1%)
  - [ ] `order_by_multi_field` - Multiple fields sorting (not yet implemented)
  - [x] `order_by_with_where` - Combined ORDER BY + WHERE (98.4%)
  - [x] `order_by_with_limit_10/100` - Top-N query pattern (96-101%)

- [ ] **GROUP BY Benchmarks** - No performance testing
  - [ ] `group_by_single` - Single field grouping
  - [ ] `group_by_multi` - Multi-field grouping
  - [ ] `group_by_with_count` - GROUP BY + COUNT aggregate
  - [ ] `group_by_with_sum` - GROUP BY + SUM aggregate
  - [ ] `group_by_with_where` - GROUP BY + WHERE filter

- [ ] **JOIN Type Benchmarks** - Only INNER JOIN benchmarked
  - [ ] `left_join_100` to `left_join_100000` - LEFT JOIN at various scales
  - [ ] `right_join_100` to `right_join_100000` - RIGHT JOIN at various scales
  - [ ] `multi_fk_join` - Multiple foreign key JOIN performance

- [ ] **WHERE Operator Benchmarks** - Only basic operators benchmarked
  - [ ] `where_like` - Pattern matching (LIKE '%pattern%')
  - [ ] `where_between` - Range queries (BETWEEN x AND y)
  - [ ] `where_in` - Set membership (IN (1, 2, 3))
  - [ ] `where_complex_and_or` - Complex AND/OR combinations

- [ ] **DISTINCT Multi-Field Benchmarks**
  - [ ] `distinct_multi_field_2` - DISTINCT on 2 fields
  - [ ] `distinct_multi_field_3` - DISTINCT on 3 fields
  - [ ] `distinct_with_order_by` - DISTINCT + ORDER BY

---

### Missing Unit Tests (Priority: Medium)

Features that could use additional test coverage:

- [ ] **ORDER BY Edge Cases**
  - [ ] Order by nullable fields (NULL ordering)
  - [ ] Order by BLOB fields (if applicable)
  - [ ] ORDER BY with empty result set

- [ ] **GROUP BY Edge Cases**
  - [ ] GROUP BY with all NULL values in group column
  - [ ] GROUP BY + multiple aggregates + WHERE + ORDER BY (full chain)

- [ ] **Combined Clause Tests**
  - [ ] Full chain: WHERE + JOIN + GROUP BY + ORDER BY + LIMIT + OFFSET
  - [ ] All aggregate types with GROUP BY in single query

---

### Feature Requests (Priority: Medium)

- [ ] **Column Projection (SELECT specific columns)** - Allow users to specify which columns to retrieve
  - Example: `qs.select<^^name, ^^age>()` returns `std::vector<std::tuple<std::string, int>>`
  - Example: `qs.select<^^name>()` returns `std::vector<std::string>` (single column)
  - Benefits: Reduced memory usage, faster queries when only subset of fields needed
  - Related to `values<>()` but for general SELECT operations
  - Should work with WHERE, JOIN, ORDER BY, LIMIT/OFFSET

---

### Not Implemented Features (Priority: Low)

Features not yet implemented:

- [ ] **HAVING Clause** - Filter on aggregated results
  - Example: `qs.group_by<^^age>().count().having(count > 5).select()`
  - Requires post-GROUP BY filtering support

- [ ] **UNION / UNION ALL** - Combine results from multiple queries
  - Would require query composition architecture

- [ ] **Subqueries** - Nested queries
  - IN (SELECT ...), EXISTS (SELECT ...)
  - FROM (SELECT ...) AS subquery

- [ ] **Window Functions** - Analytics
  - ROW_NUMBER(), RANK(), DENSE_RANK(), LAG(), LEAD()
  - Requires OVER clause support

- [ ] **Common Table Expressions (CTE)** - WITH clause
  - Recursive queries support

---

### Code Quality Tasks (Priority: Medium)

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

---

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
