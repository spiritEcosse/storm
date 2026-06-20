# Compiler Attributes Guide

This document defines when and how to use GCC/Clang compiler attributes in Storm ORM for performance optimization.

## Overview

Storm uses three key compiler attributes to achieve near-raw SQLite performance:

1. `__attribute__((hot))` - Mark frequently-called functions
2. `__attribute__((flatten))` - Inline entire call trees
3. `__attribute__((always_inline))` - Force inlining of helpers

**IMPORTANT**: These attributes directly impact code generation, instruction cache layout, and performance. Always benchmark after adding/removing them.

## Rules and Guidelines

### 1. `__attribute__((hot))` - Hot Path Marker

**When to Use:**
- Public API methods that are entry points to performance-critical paths
- Functions called in tight loops or high-frequency scenarios
- Main execution methods (e.g., `execute_optimized()`, `select()`)

**Effect:**
- Tells compiler this function is frequently executed
- Compiler optimizes for speed over size
- Function placed in hot text section for better instruction cache locality
- Enables more aggressive optimizations (loop unrolling, vectorization)

**Example:**
```cpp
// QuerySet::select() - main entry point called frequently
[[nodiscard]] __attribute__((hot))
std::expected<std::vector<T>, Error> select() {
    // ...
}

// SelectStatement::execute_optimized() - hot path execution
[[nodiscard]] __attribute__((hot)) __attribute__((flatten))
auto execute_optimized() noexcept -> std::expected<std::vector<T>, Error> {
    return execute_simple_select();
}
```

**When NOT to Use:**
- Rarely-called functions (initialization, error handling)
- Already-inlined functions (they won't have their own text section)
- Functions marked `always_inline` (redundant)

### 2. `__attribute__((flatten))` - Aggressive Inlining

**When to Use:**
- ALWAYS used together with `__attribute__((hot))`
- Main execution functions that coordinate multiple operations
- Functions where you want to inline the entire call tree
- Top-level methods in hot paths (e.g., `execute_with_where()`, `execute_query_loop()`)

**Effect:**
- Inlines ALL function calls within this function (entire call tree)
- Eliminates function call overhead completely
- Improves instruction cache locality
- Can significantly increase code size

**Example:**
```cpp
// Flatten entire execution path into single function
[[nodiscard]] __attribute__((hot)) __attribute__((flatten))
auto execute_with_where(std::shared_ptr<orm::where::Expression> where_expr) noexcept
        -> std::expected<std::vector<T>, Error> {
    // All calls within this function get inlined:
    // - build_where_sql()
    // - bind_where_params()
    // - execute_query_loop()
    // - extract_all_columns_inline_fast()
    return execute_where_impl(std::move(where_expr));
}
```

**When NOT to Use:**
- Functions with large call trees (excessive code bloat)
- Recursive functions (compiler ignores it anyway)
- Functions called from only one place (normal inlining suffices)
- Non-hot paths (waste of instruction cache)

**Combination Rule:**
```cpp
// ALWAYS use together:
__attribute__((hot)) __attribute__((flatten))

// NEVER use alone (defeats the purpose):
__attribute__((flatten))  // ❌ Wrong - needs hot to justify code bloat
```

### 3. `__attribute__((always_inline))` - Force Inlining

**When to Use:**
- Small utility/helper functions (1-10 lines)
- Functions wrapping SQLite C API calls
- Template helpers with compile-time parameters
- Functions called in tight loops
- Zero-cost abstractions that MUST have no overhead

**Effect:**
- Forces inlining regardless of compiler heuristics
- Compilation fails if inlining is impossible (e.g., recursion)
- Eliminates ALL function call overhead
- Used for zero-abstraction-cost helpers

**Example:**
```cpp
// SQLite API wrappers - must have zero overhead
[[nodiscard]] __attribute__((always_inline)) inline
auto step_raw() noexcept -> int {
    return sqlite3_step(stmt_.get());
}

[[nodiscard]] __attribute__((always_inline)) inline
auto extract_int(int col_index) const noexcept -> int {
    return sqlite3_column_int(stmt_.get(), col_index);
}

// Template helpers with compile-time index
template <size_t Index>
__attribute__((always_inline)) static inline
void extract_column_inline_fast(Statement* stmt, T& obj) noexcept {
    // ...
}

// Helper functions in hot paths
[[nodiscard]] __attribute__((always_inline)) static inline
std::string build_where_sql(const std::string& base_sql,
                            const std::shared_ptr<orm::where::Expression>& where_expr) {
    // ...
}
```

**When NOT to Use:**
- Large functions (>20 lines) - causes code bloat
- Functions with complex control flow
- Recursive functions (causes compilation error)
- Functions with many callers (excessive code duplication)
- Virtual functions (impossible to inline)

**Best Practices:**
```cpp
// ✅ Good - small wrapper, zero overhead
__attribute__((always_inline)) inline auto extract_int(...) {
    return sqlite3_column_int(...);
}

// ✅ Good - template helper with fold expression
template <size_t... Is>
__attribute__((always_inline)) static inline void
extract_all_columns_inline_fast_impl(..., std::index_sequence<Is...>) {
    ((extract_column_inline_fast<Is>(...)), ...);
}

// ❌ Bad - too large, causes excessive code bloat
__attribute__((always_inline)) auto execute_complex_query() {
    // 50 lines of complex logic...
}
```

## Usage Patterns by Location

### Database Layer (src/db/sqlite.cppm)

```cpp
class Statement {
    // SQLite API wrappers - always_inline for zero overhead
    [[nodiscard]] __attribute__((always_inline)) inline
    auto step_raw() noexcept -> int;

    [[nodiscard]] __attribute__((always_inline)) inline
    auto extract_int(int col_index) const noexcept -> int;

    [[nodiscard]] __attribute__((always_inline)) inline
    auto extract_text_view(int col_index) const noexcept -> std::string_view;

    // ... all extract_* methods use always_inline
};
```

### Statement Layer (src/orm/statements/*.cppm)

```cpp
// Base utilities - always_inline for shared helpers
template <typename FieldType, typename Statement>
[[nodiscard]] __attribute__((always_inline)) static inline FieldType
extract_column_value(Statement& stmt, int col_idx) noexcept;

// Execution methods - hot + flatten for main paths
[[nodiscard]] __attribute__((hot)) __attribute__((flatten))
auto execute_optimized() noexcept -> std::expected<std::vector<T>, Error>;

[[nodiscard]] __attribute__((hot)) __attribute__((flatten))
auto execute_with_where(std::shared_ptr<orm::where::Expression> where_expr) noexcept
    -> std::expected<std::vector<T>, Error>;

// Internal helpers - always_inline for inlining into hot paths
template <size_t Index>
__attribute__((always_inline)) static inline
void extract_column_inline_fast(Statement* stmt, T& obj) noexcept;

[[nodiscard]] __attribute__((always_inline)) static inline
std::string build_where_sql(const std::string& base_sql,
                            const std::shared_ptr<orm::where::Expression>& where_expr);
```

### QuerySet Layer (src/orm/queryset.cppm)

```cpp
// Public API - hot for frequently-called entry points
[[nodiscard]] __attribute__((hot))
std::expected<std::vector<T>, Error> select();
```

## Performance Testing Requirements

**CRITICAL**: Always benchmark after adding/changing attributes!

### Before/After Workflow

```bash
# 1. Baseline measurement (before changes)
cmake --preset ninja-release
cmake --build --preset ninja-release
./build/release/benchmarks/bench_where --benchmark_min_time=2s > baseline.txt

# 2. Make attribute changes

# 3. Rebuild and measure
cmake --build --preset ninja-release
./build/release/benchmarks/bench_where --benchmark_min_time=2s > after.txt

# 4. Compare results
# If ANY regression (even 1-2%), revert immediately!
git diff baseline.txt after.txt
```

### Why Benchmarking is Mandatory

Even "obvious" optimizations can regress performance due to:
- **Instruction cache effects**: Code layout changes affect cache hit rates
- **Inlining decisions**: Too much inlining can hurt more than help
- **Branch prediction**: Different code layout changes branch predictor behavior
- **Code alignment**: Function boundaries affect CPU pipeline efficiency

**Example**: Removing dead code can cause regression if it changes memory layout adversely.

## Common Mistakes

### ❌ Mistake 1: Using flatten without hot

```cpp
// Wrong - flatten without hot causes code bloat for cold path
__attribute__((flatten))
auto rarely_called_function() {
    // ...
}
```

**Fix**: Always combine `hot + flatten`:
```cpp
__attribute__((hot)) __attribute__((flatten))
auto frequently_called_function() {
    // ...
}
```

### ❌ Mistake 2: Overusing always_inline

```cpp
// Wrong - large function with always_inline
__attribute__((always_inline))
auto complex_business_logic() {  // 100 lines
    // ...
}
```

**Fix**: Use normal inlining for large functions, let compiler decide:
```cpp
auto complex_business_logic() {  // Compiler will inline if beneficial
    // ...
}
```

### ❌ Mistake 3: Forgetting inline keyword with always_inline

```cpp
// Wrong - missing inline can cause multiple definition errors in headers
__attribute__((always_inline))
auto helper() { ... }
```

**Fix**: Always include `inline` with `always_inline`:
```cpp
__attribute__((always_inline)) inline
auto helper() { ... }
```

### ❌ Mistake 4: Using hot on already-inlined functions

```cpp
// Wrong - hot is meaningless for always_inline functions
[[nodiscard]] __attribute__((hot)) __attribute__((always_inline)) inline
auto small_helper() { ... }
```

**Fix**: Choose one based on function size:
```cpp
// Small helper - just always_inline
__attribute__((always_inline)) inline
auto small_helper() { ... }

// Large entry point - hot + flatten
[[nodiscard]] __attribute__((hot)) __attribute__((flatten))
auto large_entry_point() { ... }
```

## Decision Flow Chart

```
Is this function frequently called in hot paths?
├─ Yes → Is it a main execution entry point?
│   ├─ Yes → Use __attribute__((hot)) __attribute__((flatten))
│   │         Example: execute_optimized(), execute_with_where()
│   └─ No → Is it a small helper (< 10 lines)?
│       ├─ Yes → Use __attribute__((always_inline)) inline
│       │         Example: extract_int(), step_raw(), build_where_sql()
│       └─ No → Let compiler decide (no attributes)
│
└─ No → No attributes needed
```

## Verification Checklist

Before committing code with these attributes:

- [ ] **hot** is ONLY on frequently-called entry points
- [ ] **flatten** is ALWAYS paired with **hot** (never alone)
- [ ] **always_inline** is ONLY on small helpers (< 10 lines typically)
- [ ] **always_inline** includes `inline` keyword
- [ ] Benchmarks run and show no regression
- [ ] Code size increase is justified by performance gain

## References

- **GCC Documentation**: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html
- **Clang Documentation**: https://clang.llvm.org/docs/AttributeReference.html
- **Performance Testing**: See [docs/internals/performance/PERFORMANCE.md](../performance/PERFORMANCE.md#testing-benchmarking)
- **Compiler Issues**: See [docs/internals/compiler/COMPILER_ISSUES.md](COMPILER_ISSUES.md)

## Summary

| Attribute | Use Case | Location | Always Combine |
|-----------|----------|----------|----------------|
| `__attribute__((hot))` | Frequently-called entry points | Public APIs, execute methods | With `flatten` |
| `__attribute__((flatten))` | Inline entire call tree | Main execution paths | With `hot` |
| `__attribute__((always_inline))` | Small helpers, zero-cost wrappers | SQLite wrappers, template helpers | With `inline` |

**Remember**: Performance > Intuition. Always benchmark before and after!
