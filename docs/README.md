# Storm ORM Documentation

Welcome to Storm ORM - a modern C++26 ORM library using compile-time reflection to automatically map C++ structs to database tables without macros.

**Supported backends**: SQLite, PostgreSQL

## Quick Links

- **[Main Project Guide (CLAUDE.md)](../CLAUDE.md)** - Quick start and common tasks
- **[Benchmarks](../benchmarks/README.md)** - Performance results and benchmark system

## Features

### Core Operations
- **[CRUD Operations](features/CRUD_OPERATIONS.md)** - INSERT, UPDATE, DELETE with auto-generated IDs
- **[SELECT Queries](features/SELECT_QUERIES.md)** - Optimized row extraction and statement caching
- **[WHERE Clauses](features/WHERE_CLAUSES.md)** - Type-safe filtering with pure C++26 reflection
- **[JOIN Operations](features/JOIN_OPERATIONS.md)** - Single and multi-FK JOINs with type erasure
- **[Batch Operations](features/BATCH_OPERATIONS.md)** - Bulk INSERT/UPDATE/DELETE with smart thresholds

## Architecture

- **[Overview](architecture/OVERVIEW.md)** - High-level architecture overview
- **[Design Decisions](architecture/DESIGN_DECISIONS.md)** - Key architectural decisions and rationale
- **[C++26 Reflection](architecture/REFLECTION.md)** - How Storm uses std::meta for ORM mapping
- **[Statement Caching](architecture/STATEMENT_CACHING.md)** - 3-level caching achieving near-raw SQLite performance
- **[SQL Generation](architecture/SQL_GENERATION.md)** - Compile-time SQL generation with ConstexprString
- **[Module System](architecture/MODULE_SYSTEM.md)** - C++26 module structure, dependencies, and cross-module inlining
- **[Compile-Time vs Runtime](architecture/COMPILE_TIME_VS_RUNTIME.md)** - WHERE expression design tradeoffs and performance analysis

## Benchmarks

- **[Benchmark System](../benchmarks/README.md)** - Compile-time benchmark system with size profiles and category filtering
- **[JOIN Analysis](benchmarks/JOIN_ANALYSIS.md)** - JOIN performance deep dive
- **[DISTINCT Analysis](benchmarks/DISTINCT_ANALYSIS.md)** - DISTINCT performance analysis

## Reference

- **[Field Types](reference/FIELD_TYPES.md)** - Supported C++ to SQLite type mappings

## Development

- **[Common Tasks](development/COMMON_TASKS.md)** - Adding operations, common development workflows
- **[Testing](development/TESTING.md)** - Test framework, PostgreSQL test isolation, running tests
- **[Code Coverage](development/CODE_COVERAGE.md)** - How to generate and analyze test coverage
- **[Adding Features](development/ADDING_FEATURES.md)** - How to add new database operations
- **[C++26 Coding Standards](development/CPP26_CODING_STANDARDS.md)** - Modern C++ best practices and patterns
- **[Performance Guidelines](development/PERFORMANCE_GUIDELINES.md)** - Performance rules and best practices
- **[Performance Tips](development/PERFORMANCE_TIPS.md)** - Transaction wrapping, batch inserts, optimization techniques
- **[Performance Testing](development/PERFORMANCE_TESTING.md)** - Benchmarking guidelines and workflow
- **[Compiler Attributes](development/COMPILER_ATTRIBUTES.md)** - Guide for using hot, flatten, and always_inline attributes
- **[Compiler Issues](development/COMPILER_ISSUES.md)** - Known workarounds for clang-p2996
- **[Formatting](development/FORMATTING.md)** - clang-format and cmake-format targets, pre-commit integration, and `.clang-format` settings
- **[Sanitizers](development/SANITIZERS.md)** - ASAN+UBSAN and TSAN presets, what each sanitizer catches, MSAN status

## Open Issues

All open tasks are tracked as [GitHub Issues](https://github.com/spiritEcosse/storm/issues).
