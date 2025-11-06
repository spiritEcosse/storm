# Storm ORM Documentation

Welcome to Storm ORM - a modern C++26 ORM library for SQLite using cutting-edge reflection to automatically map C++ structs to database tables.

## Quick Links

- **[Main Project Guide (CLAUDE.md)](../CLAUDE.md)** - Quick start and common tasks
- **[Benchmarks](../BENCHMARKS.md)** - Performance comparison results

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
