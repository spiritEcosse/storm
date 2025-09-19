# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Storm is a modern C++26 ORM (Object-Relational Mapping) library for SQLite and PostgreSQL databases. It leverages cutting-edge C++26 features including modules, reflection, concepts, and coroutines to provide type-safe database operations.

## Build Commands

### Standard Development Build
```bash
# Debug build with ninja
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release
```

### Testing
```bash
# Build and run tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
cd build/debug && ctest --output-on-failure
```

### Code Quality
```bash
# Format code (requires clang-format)
cmake --build --preset ninja-debug --target format

# Check code formatting
cmake --build --preset ninja-debug --target format-check
```

### Sanitizer Builds
```bash
# Address sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug

# Thread sanitizer (separate build)
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread"
cmake --build --preset ninja-debug

# Memory sanitizer (Clang only)
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="memory"
cmake --build --preset ninja-debug
```

## Architecture Overview

### Module Structure
The codebase is organized using C++26 modules with a hierarchical structure:

- **`storm.core`**: Core utilities, exceptions, and type traits
- **`storm.reflect`**: Custom reflection system for C++ objects
- **`storm.query_set`**: Main ORM interface (QuerySet class)
- **`storm.orm.*`**: Sub-modules for ORM functionality:
  - `condition`: WHERE clause conditions
  - `statement`: SQL statement builders (SELECT, INSERT, UPDATE, DELETE)
  - `field`: Database field representations
  - `function`: SQL functions and aggregates
  - `expression`: SQL expression builders

### Key Components

1. **QuerySet**: The main ORM interface that provides CRUD operations
   - Template-based for type safety
   - Supports method chaining for query building
   - Uses compile-time reflection for automatic table mapping

2. **Reflection System** (`src/reflect/`):
   - Custom reflection implementation using template metaprogramming
   - Automatic field discovery and type mapping
   - SQL DDL generation from C++ structs

3. **Statement Builders** (`src/orm/statement/`):
   - Type-safe SQL query construction
   - Parameter binding with compile-time validation
   - Support for complex queries with joins, aggregations, etc.

4. **Database Connections** (`src/db/`):
   - Abstract connection interface
   - SQLite and PostgreSQL implementations
   - Connection pooling and transaction management

### Code Conventions

The project follows modern C++26 patterns as defined in `rules.md`:

- **C++26 Standard**: Uses modules, concepts, ranges, and other cutting-edge features
- **Module Organization**: Each major component is a module (`*.cppm` files)
- **Type Safety**: Heavy use of concepts and `std::expected` for error handling
- **Modern STL**: Uses `std::ranges`, `std::print`, `std::format`, etc.
- **Memory Safety**: RAII patterns, smart pointers, and `std::span`

### Testing Structure

- **GoogleTest Framework**: Uses GTest with module support
- **Test Location**: `tests/` directory with corresponding CMakeLists.txt
- **Coverage**: Comprehensive tests for ORM operations and SQL generation
- **Sanitizers**: Full sanitizer support for memory safety validation

### Database Support

- **SQLite**: Primary database backend with full feature support
- **PostgreSQL**: Secondary backend with connection pooling
- **Schema Management**: Automatic table creation and migration support

## Development Workflow

1. **Code Style**: The project uses clang-format with repository-specific configuration
2. **Module Dependencies**: Always import required modules in the correct order
3. **Error Handling**: Use `std::expected` for recoverable errors, exceptions for programming errors
4. **Reflection**: Use the custom reflection system for automatic ORM mapping
5. **Testing**: Write comprehensive tests for all public APIs

## Important Notes

- This is a **C++26 project** - use the most modern features available
- **Module imports** are required instead of traditional includes for Storm components
- **Reflection macros** are not used - the system uses template metaprogramming
- **Database connections** should be managed through the Connection abstraction
- **QuerySet** is the primary interface for all database operations

TODO:
- [x] Overload group_concat , overwrite group_concat_order with overloaded group_concat and fix tests
- [x] Rename only_with_aliases to overloaded version of only and add tests
- [x] Rename where_all to overloaded version of where and fix tests
- [x] Successfully removed all unused where_impl implementations from the codebase!
- [x] Remove the Aggregatable concept definition from QuerySet.cppm
- [x] Check if i need compile_time_sql
