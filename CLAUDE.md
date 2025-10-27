# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**📚 Full Documentation**: See [docs/README.md](docs/README.md) for comprehensive feature documentation, architecture guides, and development workflows.

## Project Overview

Storm is a modern C++26 ORM library for SQLite using cutting-edge C++26 reflection to automatically map C++ structs to database tables without macros.

**Performance**: Storm achieves **1.5-6x performance advantage** over sqlite_orm, maintaining **70-90% efficiency** compared to raw SQLite.

| Operation | Storm ORM | Raw SQLite | Storm vs sqlite_orm |
|-----------|-----------|------------|---------------------|
| INSERT (single) | 992K/sec | 49M/sec | 2.0x faster |
| INSERT (batch) | 2.7M/sec | - | 6.4x faster |
| SELECT | 13.07M rows/sec | 17.67M rows/sec | 1.51x faster |
| UPDATE | 2M/sec (12M peak) | 1.09M/sec | 6x faster |
| DELETE | 21.6M/sec | 29.4M/sec | 36.6x faster |
| JOIN | 4-6M rows/sec | 5-7.4M rows/sec | 77% avg efficiency |
| WHERE | 9.45-11.88M rows/sec | 10.57-13.82M rows/sec | 86-90% efficiency |

**Key Innovations**: Compile-time SQL generation, 3-level statement caching, thread-local SQL caching, optimized row extraction, abstract base class pattern for type-erased JOINs, pure C++26 reflection for WHERE clauses.

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

## Quick Start

### Prerequisites
- Custom Clang with C++26 reflection support (located at `../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+, Ninja build system

### Build Commands

```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release

# Code formatting
cmake --build --preset ninja-debug --target format
```

### Sanitizer Builds

```bash
# Address + Leak sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug

# Thread sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread"
cmake --build --preset ninja-debug
```

## Benchmarking

**⚠️ IMPORTANT: Always use Release builds for accurate performance measurements!**

```bash
# Python benchmark suite (RECOMMENDED - with auto-rebuild)
python3 bench.py --all                   # All microbenchmarks
python3 bench.py --joins                 # JOIN performance
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --compare               # Full Storm vs sqlite_orm vs Raw SQLite

# Direct C++ benchmark execution (manual rebuild required)
cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Individual benchmarks
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_join --size=10000 --storm-join-1
./build/release/benchmarks/bench_sqlite_orm
./build/release/benchmarks/bench_sqlite
```

**Python Benchmark Features**: Auto-rebuild, formatted output, flexible dataset control, cache-aware.

## Basic Usage Examples

### Defining Structs

```cpp
struct Person {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};
```

### CRUD Operations

```cpp
storm::orm::QuerySet<Person> queryset(conn);

// INSERT - returns auto-generated ID
auto result = queryset.insert(Person{0, "Alice", 25});
int64_t id = result.value();

// SELECT
auto result = queryset.select();
for (const auto& person : result.value()) { /* ... */ }

// UPDATE
queryset.update(Person{1, "Alice", 26});

// DELETE
queryset.remove(Person{1, "Alice", 26});
```

See [docs/features/CRUD_OPERATIONS.md](docs/features/CRUD_OPERATIONS.md) for complete CRUD documentation.

### WHERE Clauses

```cpp
using namespace storm::orm::where;

// Single condition (pure C++26 reflection - no macro!)
auto result = queryset.where(field<^^Person::age>() > 30).select();

// Multiple conditions with natural operators
auto result = queryset.where(field<^^Person::age>() > 25 and
                              field<^^Person::age>() < 50).select();

// Special methods
auto result = queryset.where(field<^^Person::name>().like("A%")).select();
auto result = queryset.where(field<^^Person::age>().between(28, 35)).select();
auto result = queryset.where(field<^^Person::id>().in(1, 2, 3)).select();
```

See [docs/features/WHERE_CLAUSES.md](docs/features/WHERE_CLAUSES.md) for complete WHERE documentation.

### JOIN Operations

```cpp
// Single FK JOIN
auto result = message_qs.join<&Message::sender>().select();

// Multi-FK JOIN
auto result = message_qs.join<&Message::sender, &Message::receiver>().select();

// JOIN with WHERE
auto result = message_qs.join<&Message::sender>()
                        .where(field<^^User::level>() > 5)
                        .select();
```

See [docs/features/JOIN_OPERATIONS.md](docs/features/JOIN_OPERATIONS.md) for complete JOIN documentation.

### Batch Operations

```cpp
// Batch INSERT - returns all generated IDs
std::vector<Person> people = {{0, "Alice", 25}, {0, "Bob", 30}};
auto result = queryset.insert(std::span<const Person>(people));
const auto& ids = result.value();

// Batch UPDATE/DELETE
queryset.update(std::span<const Person>(people));
queryset.remove(std::span<const Person>(people));
```

See [docs/features/BATCH_OPERATIONS.md](docs/features/BATCH_OPERATIONS.md) for complete batch operation documentation.

## Documentation Index

### Features
- **[CRUD Operations](docs/features/CRUD_OPERATIONS.md)** - INSERT, UPDATE, DELETE with auto-generated IDs
- **[SELECT Queries](docs/features/SELECT_QUERIES.md)** - Optimized row extraction and statement caching
- **[WHERE Clauses](docs/features/WHERE_CLAUSES.md)** - Type-safe filtering with pure C++26 reflection
- **[JOIN Operations](docs/features/JOIN_OPERATIONS.md)** - Single and multi-FK JOINs with type erasure
- **[Batch Operations](docs/features/BATCH_OPERATIONS.md)** - Bulk INSERT/UPDATE/DELETE with smart thresholds

### Architecture
- **[Overview](docs/architecture/OVERVIEW.md)** - High-level architecture and design principles
- **[C++26 Reflection](docs/architecture/REFLECTION.md)** - How Storm uses std::meta for ORM mapping
- **[Statement Caching](docs/architecture/STATEMENT_CACHING.md)** - 3-level caching achieving near-raw SQLite performance
- **[SQL Generation](docs/architecture/SQL_GENERATION.md)** - Compile-time SQL generation with ConstexprString
- **[Module System](docs/architecture/MODULE_SYSTEM.md)** - C++26 module structure and dependencies

### Development
- **[Compiler Issues](docs/development/COMPILER_ISSUES.md)** - Known workarounds for clang-p2996
- **[Performance Testing](docs/development/PERFORMANCE_TESTING.md)** - Benchmarking guidelines and workflow
- **[Adding Features](docs/development/ADDING_FEATURES.md)** - How to add new database operations

## Common Development Tasks

### Running Tests

```bash
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure
```

### Performance Testing

```bash
# Recommended workflow
python3 bench.py --compare  # Compare all implementations

# Check for regressions before commit
python3 bench.py --all
```

See [docs/development/PERFORMANCE_TESTING.md](docs/development/PERFORMANCE_TESTING.md) for detailed benchmarking guidelines.

### Adding New Features

1. Implement in `src/orm/statements/`
2. Add tests in `tests/test_*.cpp`
3. Create benchmark in `benchmarks/bench_*.cpp`
4. Run benchmark and measure efficiency (target: ≥70% of raw SQLite)
5. Update documentation
6. Commit with performance metrics

See [docs/development/ADDING_FEATURES.md](docs/development/ADDING_FEATURES.md) for detailed implementation guide.

### Git Workflow

```bash
# Update feature branch
git fetch origin && git merge origin/develop

# Test before pushing
cmake --build --preset ninja-debug && ctest --test-dir build/debug
git push
```

**Best practices**: Clean working directory, short-lived branches, thorough testing, descriptive commit messages.

## Module Structure

```
src/
├── storm.cppm                      # Main module
├── db/
│   ├── concept.cppm                # Database concepts
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet ORM interface
    ├── utilities.cppm              # ConstexprString, SQLCache templates
    └── statements/
        ├── base.cppm               # BaseStatement utilities
        ├── insert.cppm, update.cppm, remove.cppm
        ├── select.cppm             # SelectStatement with JOIN support
        └── join.cppm               # JoinStatement (SQL builder)
```

See [docs/architecture/MODULE_SYSTEM.md](docs/architecture/MODULE_SYSTEM.md) for module dependencies and circular dependency avoidance.

## Compiler Requirements

Experimental Clang fork with C++26 reflection:
- Located at `../clang-p2996/`
- Custom libcxx with reflection support
- Module scanning with `clang-scan-deps`
- Reflection flags: `-freflection -fannotation-attributes`

### Known Compiler Issues

1. **Module Cache Corruption** - Simply run build command again if it fails
2. **std::mutex Segfaults** - Avoid mutex in module code
3. **std::inplace_vector Not Available** - Use `std::array` instead
4. **C Headers Cannot Be Imported** - Include in module preamble with `#include`
5. **Most Vexing Parse** - Use braced initialization
6. **Missing Statement Methods** - Use `stmt->handle()` for raw SQLite API

See [docs/development/COMPILER_ISSUES.md](docs/development/COMPILER_ISSUES.md) for detailed workarounds.

## Performance-Driven Development

Storm ORM prioritizes performance over code elegance when there's a conflict:

### Key Principles

1. **Benchmark Before Accepting** - Every new feature requires performance testing
2. **Target**: ≥70% of raw SQLite performance
3. **DRY/KISS Principle** - Follow unless it costs >10% performance
4. **Document Tradeoffs** - Explain when breaking DRY/KISS for performance

### Performance Testing Workflow

```bash
# 1. Implement feature
# 2. Create benchmark
touch benchmarks/bench_<feature>.cpp

# 3. Run benchmark
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_<feature> --size=10000

# 4. Calculate efficiency
# Efficiency = (Storm_ORM_perf / Raw_SQLite_perf) * 100%

# 5. Document results in relevant docs
# 6. Commit with performance metrics
git commit -m "feat: add FEATURE (XX% of raw SQLite)"
```

See [docs/development/PERFORMANCE_TESTING.md](docs/development/PERFORMANCE_TESTING.md) for comprehensive guidelines.

## Important Implementation Notes

- **No REFL-CPP**: Project uses native C++26 reflection
- **Index Sequence Optimization**: Uses `std::index_sequence` and fold expressions
- **Pre-computed Metadata**: Field information cached in static constexpr variables
- **Module Naming**: Uses underscores (`storm_db_sqlite`) due to compiler limitations
- **Circular Dependencies**: Avoided by duplicating `FieldAttr` enum
- **std::function Avoided**: Use abstract base classes instead (see JOIN architecture)
- **Primary Key Access**: Uses reflection splice operator `obj.[:primary_key_:]`
- **WHERE Implementation**: Pure C++26 reflection using `field<^^T::member>()` - no macro needed

## Thread Safety

1. **SQLite Level**: Thread-safe with `SQLITE_OPEN_FULLMUTEX`
2. **Connection Management**: NOT thread-safe - requires external synchronization
3. **SQL Caching**: Thread-local storage eliminates synchronization overhead
4. **Recommended**: Per-thread connections or external mutex

## Testing Strategy

- **GoogleTest** with C++26 module support
- Tests in `tests/` directory, in-memory database (`:memory:`)
- Comprehensive sanitizer support
- ID validation for INSERT operations
- SELECT caching verification
- JOIN object population verification
- FK field testing (INSERT/UPDATE/DELETE with FKs)

## Help and Support

For questions or issues:
- Check [docs/README.md](docs/README.md) for comprehensive documentation
- See [docs/development/COMPILER_ISSUES.md](docs/development/COMPILER_ISSUES.md) for known workarounds
- Browse [docs/features/](docs/features/) for feature-specific usage
- Consult [docs/architecture/](docs/architecture/) for implementation details
