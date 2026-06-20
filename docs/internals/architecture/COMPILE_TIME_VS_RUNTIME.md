# Compile-Time vs Runtime WHERE Expressions

## Overview

This document explores the **compile-time vs runtime** trade-offs for WHERE clause expressions in Storm ORM. While we ultimately chose the **runtime approach with virtual functions**, this documents what a true compile-time implementation would require and the performance characteristics of both approaches.

## Current Implementation: Runtime Expressions

### Architecture

```cpp
// All expressions inherit from Expression base class
class Expression {
    virtual std::string to_sql() const = 0;
    virtual void collect_params(std::vector<ParamValue>& params) const = 0;
    virtual auto bind_params_direct(void* stmt_ptr, int& idx) const -> std::expected<void, Error> = 0;
};

// Stored in QuerySet via shared_ptr (type erasure)
class QuerySet<T> {
    std::shared_ptr<Expression> where_expr_;  // Type-erased

    auto where(std::shared_ptr<Expression> expr) {
        where_expr_ = expr;
        return *this;  // Same type returned
    }
};

// Usage - QuerySet type never changes
QuerySet<Person> qs;
auto result = qs.where(f<^^Person::age>() > 30).select();
```

### Characteristics

**Advantages:**
- ✅ Simple, clean API - QuerySet type never changes
- ✅ Easy method chaining - always returns same type
- ✅ Natural builder pattern
- ✅ Easy to store and pass around QuerySet objects
- ✅ No complex template error messages
- ✅ Excellent compile times

**Disadvantages:**
- ❌ Virtual function calls (1-2 per expression node)
- ❌ Heap allocation for each expression (`shared_ptr`)
- ❌ SQL generation happens at runtime
- ❌ Small runtime overhead (~5-15% compared to raw SQLite for simple queries)

### Performance Measurements

From benchmarks (10,000 rows, 100 iterations):

| Operation | Storm ORM | Raw SQLite | Efficiency |
|-----------|-----------|------------|------------|
| Simple (2 AND) | 7.51M rows/sec | 8.20M rows/sec | 91.6% |
| Medium (4 cond) | 1.37M rows/sec | 1.42M rows/sec | 96.5% |
| Complex (8+ cond) | 0.73M rows/sec | 0.74M rows/sec | **98.6%** |
| LIKE pattern | 2.80M rows/sec | 2.89M rows/sec | 96.9% |
| BETWEEN range | 5.09M rows/sec | 5.34M rows/sec | 95.3% |

**Key Insight**: Complex queries have **better efficiency** because SQLite spends more time on evaluation, reducing relative overhead.

**Average Efficiency**: **85-95%** of raw SQLite performance for most operations.

---

## Alternative: True Compile-Time Expressions

### What Would Be Required

#### 1. Template QuerySet on Expression Type

```cpp
// QuerySet must be templated on the expression type
template<typename T, typename Conn = Connection, typename WhereExpr = void>
class QuerySet {
    [[no_unique_address]] WhereExpr where_expr_;  // Zero overhead if void

    // Each .where() returns a DIFFERENT type
    template<typename NewExpr>
    auto where(NewExpr expr) {
        // Return new QuerySet with different template parameter
        return QuerySet<T, Conn, NewExpr>{conn_, std::move(expr)};
    }
};
```

#### 2. Compile-Time Expression Templates

```cpp
// No virtual functions, no inheritance
template<typename FieldInfo, typename Op, typename Value>
struct ComparisonExpr {
    Value value;

    // SQL generated at COMPILE TIME
    static constexpr auto sql() {
        constexpr auto field_name = std::meta::identifier_of(FieldInfo);
        return ConstexprString(field_name) + " > ?";  // Compile-time string concat
    }

    // Direct binding, no virtual calls
    auto bind_params(Statement* stmt, int idx) const {
        return stmt->bind_int(idx, value);  // Statically dispatched
    }
};
```

#### 3. Complex Type Signatures

```cpp
// Type changes with EVERY operation
QuerySet<Person> qs;

auto qs1 = qs.where(f<^^Person::age>() > 30);
// qs1 type: QuerySet<Person, Connection, ComparisonExpr<Person::age, Greater, int>>

auto qs2 = qs1.where(f<^^Person::name>() == "Alice");
// qs2 type: QuerySet<Person, Connection,
//                    AndExpr<ComparisonExpr<Person::age, Greater, int>,
//                            ComparisonExpr<Person::name, Equal, std::string>>>

auto result = qs2.select();
```

#### 4. Implementation Complexity

```cpp
// AND operator must create new expression type
template<typename Left, typename Right>
struct AndExpr {
    Left left;
    Right right;

    static constexpr auto sql() {
        return "(" + Left::sql() + " AND " + Right::sql() + ")";
    }

    auto bind_params(Statement* stmt, int idx) const {
        left.bind_params(stmt, idx);
        right.bind_params(stmt, idx + Left::param_count);
    }
};

// OR, complex nesting - all need template implementations
```

### Theoretical Performance Characteristics

**Advantages:**
- ✅ Zero virtual function overhead
- ✅ Zero heap allocations
- ✅ SQL generated at **compile time** (stored in binary)
- ✅ Perfect inlining opportunities
- ✅ Theoretical maximum performance (~99-100% of raw SQLite)

**Disadvantages:**
- ❌ Extremely complex API - type changes with every `.where()` call
- ❌ Cannot store QuerySet in a variable easily (need `auto` everywhere)
- ❌ Template error messages would be nightmarish
- ❌ Compile times would increase significantly
- ❌ Code bloat - every unique expression generates new template instantiation
- ❌ Cannot pass QuerySet to functions without complex template signatures
- ❌ Hard to compose expressions dynamically at runtime

### Estimated Performance Gain

Based on profiling and analysis:

**Simple Queries (2-3 conditions):**
- Current: 90-95% efficiency
- Compile-time: 95-99% efficiency
- **Gain: ~3-5%** (negligible in practice)

**Complex Queries (8+ conditions):**
- Current: 98.6% efficiency
- Compile-time: 99-100% efficiency
- **Gain: ~1-2%** (negligible - SQLite dominates execution time)

**Where Overhead Matters:**
- Only in **micro-operations** with tiny result sets
- Real-world queries: dominated by SQLite's own processing
- Network latency: completely dominates any ORM overhead

### Why Runtime Was Chosen

After analysis, we chose the **runtime approach** because:

1. **Performance Is Already Excellent**: 85-95% efficiency is more than sufficient
2. **Simplicity Matters**: Clean API is worth 5-10% overhead
3. **Compile-Time Complexity**: Template explosion would be severe
4. **Real-World Use Cases**: Network latency >> ORM overhead
5. **Maintainability**: Virtual functions are well-understood and debuggable

---

## Design Decision Matrix

| Factor | Runtime (Current) | Compile-Time | Winner |
|--------|-------------------|--------------|--------|
| **Performance** | 85-95% efficiency | 95-99% efficiency | Compile-Time (+5-10%) |
| **API Simplicity** | ⭐⭐⭐⭐⭐ Excellent | ⭐⭐ Poor | Runtime |
| **Compile Time** | ⭐⭐⭐⭐⭐ Fast | ⭐⭐ Slow | Runtime |
| **Error Messages** | ⭐⭐⭐⭐ Good | ⭐ Terrible | Runtime |
| **Code Size** | ⭐⭐⭐⭐ Small | ⭐⭐ Large (bloat) | Runtime |
| **Debuggability** | ⭐⭐⭐⭐⭐ Easy | ⭐⭐ Hard | Runtime |
| **Dynamic Composition** | ⭐⭐⭐⭐⭐ Yes | ❌ No | Runtime |
| **Type Safety** | ⭐⭐⭐⭐⭐ Excellent | ⭐⭐⭐⭐⭐ Excellent | Tie |

**Overall**: Runtime approach wins decisively due to **usability and maintainability** while maintaining excellent performance.

---

## Hybrid Approach Considered

We considered a **hybrid approach** with two APIs:

```cpp
// Runtime (default) - clean API
queryset.where(f<^^Person::age>() > 30).select();

// Compile-time (opt-in) - maximum performance
queryset.where_ct(field_ct<^^Person::age>() > 30);
```

**Rejected because:**
- API confusion - which one should users choose?
- Minimal performance gain doesn't justify complexity
- Two implementations to maintain
- Performance difference only matters in microbenchmarks

---

## Benchmarking Methodology

All measurements taken with:
- Release build (`-O3`)
- 10,000 rows
- 100 iterations
- In-memory SQLite database (`:memory:`)
- Warm cache (statements cached after first use)

Raw SQLite baseline:
```cpp
auto stmt = conn.prepare("SELECT ... WHERE age > ?");
stmt.bind_int(1, 30);
while (stmt.step() == SQLITE_ROW) {
    // Manual extraction
}
```

Storm ORM (runtime):
```cpp
queryset.where(f<^^Person::age>() > 30).select();
```

---

## Future Considerations

If Storm ORM ever needs **maximum performance** (99%+ efficiency):

1. **Profile First**: Ensure WHERE overhead is actually the bottleneck
2. **Consider Compile-Time**: Only if measurements show significant benefit
3. **Opt-In Hybrid**: Provide `where_ct()` for performance-critical code paths
4. **Document Trade-offs**: Make users aware of complexity costs

For **99.9% of use cases**, the current runtime approach is optimal.

---

## Conclusion

**Current Runtime Approach:**
- ✅ Simple, clean API
- ✅ 85-95% of raw SQLite performance
- ✅ Sufficient for all real-world use cases
- ✅ Easy to maintain and extend

**Theoretical Compile-Time Approach:**
- ✅ 95-99% of raw SQLite performance
- ❌ Extremely complex API
- ❌ Not worth 5-10% performance gain
- ❌ Would hurt developer productivity

**Verdict**: Runtime expressions with virtual functions are the right choice for Storm ORM, providing an excellent balance of performance, usability, and maintainability.
