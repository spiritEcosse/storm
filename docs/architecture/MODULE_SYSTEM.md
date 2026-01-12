# C++26 Module System: Database-Agnostic Design

This document describes the technique for writing database-agnostic modules in Storm ORM while maintaining zero-cost abstraction through cross-module inlining.

## The Problem

C++20/26 modules have a significant limitation: **function bodies defined in module implementation units are not visible to importers**, preventing inlining across module boundaries.

```cpp
// sqlite.cppm - Module interface
export module storm_db_sqlite;

export class Statement {
    // This function CANNOT be inlined by callers in other modules
    // because the body is not visible at the call site
    [[nodiscard]] auto step_raw() noexcept -> int {
        return sqlite3_step(raw_);  // Body compiled separately
    }
};

// select.cppm - Different module
import storm_db_sqlite;

// This loop has ~2-3% overhead because step_raw() cannot be inlined
while ((result = stmt->step_raw()) == SQLITE_ROW) {
    // Hot loop - function call overhead per iteration
}
```

**Why this matters:**
- Function call overhead: push/pop stack, jump, return
- Cannot inline small functions (even with `always_inline` attribute)
- Prevents compiler optimizations across module boundaries
- LTO (Link-Time Optimization) can help, but requires special toolchain support

## The Solution: Template Trick

**Making functions templates forces the compiler to include their bodies in the module interface**, because templates must be instantiated at the call site.

```cpp
// sqlite.cppm - Module interface
export module storm_db_sqlite;

export class Statement {
    // Template with dummy parameter - body IS visible to importers
    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
        return sqlite3_step(raw_);  // Body available for inlining!
    }
};

// select.cppm - Different module
import storm_db_sqlite;

// Now step_raw() CAN be inlined - zero overhead!
while ((result = stmt->step_raw()) == SQLITE_ROW) {
    // Compiler inlines sqlite3_step() directly here
}
```

## Implementation Pattern

### Step 1: Define Template Methods in Database Backend

```cpp
// src/db/sqlite.cppm
export module storm_db_sqlite;

export class Statement {
    sqlite3_stmt* raw_;  // Cached raw pointer

public:
    // All hot-path methods are templates for cross-module inlining

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
        return sqlite3_step(raw_);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_int(int col) const noexcept -> int {
        return sqlite3_column_int(raw_, col);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_int64(int col) const noexcept -> int64_t {
        return sqlite3_column_int64(raw_, col);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_double(int col) const noexcept -> double {
        return sqlite3_column_double(raw_, col);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_text_ptr(int col) const noexcept
            -> const unsigned char* {
        return sqlite3_column_text(raw_, col);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_bytes(int col) const noexcept -> int {
        return sqlite3_column_bytes(raw_, col);
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto is_null(int col) const noexcept -> bool {
        return sqlite3_column_type(raw_, col) == SQLITE_NULL;
    }

    template <typename = void>
    __attribute__((always_inline)) auto reset_raw() noexcept -> void {
        sqlite3_reset(raw_);
    }

    // Database-agnostic constants
    static constexpr int ROW_AVAILABLE = SQLITE_ROW;
    static constexpr int NO_MORE_ROWS  = SQLITE_DONE;
};
```

### Step 2: Use Statement Methods in ORM Modules

```cpp
// src/orm/statements/select.cppm
module;

#include <meta>  // C++26 reflection
// NOTE: No #include <sqlite3.h> - fully database-agnostic!

export module storm_orm_statements_select;

import storm_db_sqlite;  // Import database backend

export class SelectStatement {
    // Extract column using Statement methods (database-agnostic)
    template <typename FieldType>
    static auto extract_column(Statement* stmt, int col_idx) -> FieldType {
        if constexpr (std::is_same_v<FieldType, int>) {
            return stmt->extract_int(col_idx);
        } else if constexpr (std::is_same_v<FieldType, int64_t>) {
            return stmt->extract_int64(col_idx);
        } else if constexpr (std::is_same_v<FieldType, double>) {
            return stmt->extract_double(col_idx);
        } else if constexpr (std::is_same_v<FieldType, std::string>) {
            const unsigned char* text = stmt->extract_text_ptr(col_idx);
            if (text) {
                auto len = static_cast<size_t>(stmt->extract_bytes(col_idx));
                return std::string(reinterpret_cast<const char*>(text), len);
            }
            return {};
        }
        // ... other types
    }

    // Query loop using Statement methods
    auto execute_query_loop(Statement* stmt) {
        while (stmt->step_raw() == Statement::ROW_AVAILABLE) {
            T obj;
            extract_all_columns(stmt, obj);
            results.insert(std::move(obj));
        }
        stmt->reset();
        return results;
    }
};
```

### Step 3: Adding PostgreSQL Support

To add PostgreSQL support, create a new backend module with the same interface:

```cpp
// src/db/postgresql.cppm
export module storm_db_postgresql;

export class Statement {
    PGresult* result_;
    int current_row_ = 0;

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
        if (current_row_ < PQntuples(result_)) {
            ++current_row_;
            return ROW_AVAILABLE;
        }
        return NO_MORE_ROWS;
    }

    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto extract_int(int col) const noexcept -> int {
        return std::atoi(PQgetvalue(result_, current_row_ - 1, col));
    }

    // ... same interface as SQLite

    static constexpr int ROW_AVAILABLE = 1;
    static constexpr int NO_MORE_ROWS  = 0;
};
```

## Performance Results

The template trick enables cross-module inlining without LTO:

| Approach | SELECT Efficiency | JOIN Efficiency |
|----------|-------------------|-----------------|
| Direct `sqlite3_*` calls | 96-97% | 100-106% |
| Non-template Statement methods | 92-94% | 98-100% |
| **Template Statement methods** | **95-96%** | **106%** |

The ~1% difference from direct calls is acceptable for full database abstraction.

## Rules for Database-Agnostic Modules

### DO:

1. **Make all hot-path methods templates** with `template <typename = void>`
2. **Use `__attribute__((always_inline))`** as a hint (templates enable it to work)
3. **Cache raw pointers** in Statement class to avoid `unique_ptr::get()` overhead
4. **Define constants** like `ROW_AVAILABLE` in Statement class
5. **Keep database headers** only in backend modules (sqlite.cppm, postgresql.cppm)
6. **Use Statement methods** instead of direct database API calls in ORM modules

### DON'T:

1. **Don't include database headers** in ORM modules (no `#include <sqlite3.h>`)
2. **Don't use database-specific types** in ORM module interfaces
3. **Don't rely on LTO** for inlining - it may not be available
4. **Don't make non-hot-path methods templates** - unnecessary complexity

## Verification Checklist

Before committing database-agnostic changes:

- [ ] No `#include <sqlite3.h>` (or other DB headers) in ORM modules
- [ ] All hot-path Statement methods are templates
- [ ] Run benchmarks: efficiency should be ≥95% vs raw database calls
- [ ] All tests pass (290 tests)
- [ ] Build succeeds without database headers in ORM modules

## Why This Works

1. **Template instantiation requires visible body**: The C++ standard requires template definitions to be available where they're instantiated
2. **Module interface includes template bodies**: When a class has template methods, those bodies are part of the module interface
3. **Compiler can inline**: With the body visible, the compiler can apply `always_inline` and optimize

## Limitations

1. **Compile time**: Template methods increase module interface size slightly
2. **Error messages**: Template errors can be harder to debug
3. **IDE support**: Some IDEs may not fully support C++26 modules + templates

## References

- [P1103R3: Merging Modules](https://wg21.link/p1103r3)
- [P2996: Reflection for C++26](https://wg21.link/p2996)
- Storm ORM implementation: `src/db/sqlite.cppm`, `src/orm/statements/select.cppm`
