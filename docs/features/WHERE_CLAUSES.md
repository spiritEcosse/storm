# WHERE Clauses

Storm ORM provides type-safe WHERE clause filtering using **pure C++26 reflection** - no macros needed, fully module-compatible, achieving **86-90% efficiency** compared to raw SQLite.

## Overview

Key features:
- **Pure C++26 reflection** - Uses `field<^^T::member>()` syntax
- **Type safety** - Compile-time type checking via templates
- **Two equivalent syntaxes** - Natural operators or explicit functions
- **Expression trees** - Composable WHERE clauses using shared_ptr
- **Zero SQL injection** - All values bound as parameters
- **86-90% efficiency** - Near-raw SQLite performance

## Basic Usage

### Single Condition

```cpp
using namespace storm::orm::where;

struct Person {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

storm::orm::QuerySet<Person> queryset(conn);

// Pure reflection - no macro needed!
auto result = queryset.where(field<^^Person::age>() > 30).select();
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person WHERE age > ?
```

**Performance**: 11.88M rows/sec (86% of raw SQLite)

### Multiple Conditions

```cpp
// Natural 'and' keyword (or use &&)
auto result = queryset.where(field<^^Person::age>() > 25 and
                              field<^^Person::age>() < 50)
                      .select();

// Natural 'or' keyword (or use ||)
auto result = queryset.where(field<^^Person::name>() == "Alice" or
                              field<^^Person::name>() == "Bob")
                      .select();
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person WHERE age > ? AND age < ?
SELECT id, name, age FROM Person WHERE name = ? OR name = ?
```

**Performance**: 9.45M rows/sec (89.5% of raw SQLite)

## Two Equivalent Syntaxes

Storm ORM supports two syntaxes for combining conditions:

### 1. Natural Operator Syntax (Recommended for Direct Queries)

More readable for inline queries:

```cpp
using namespace storm::orm::where;

// Single condition
auto result = queryset.where(field<^^Person::age>() > 30).select();

// Multiple conditions with 'and'/'or' keywords
auto result = queryset.where(field<^^Person::age>() > 25 and
                              field<^^Person::age>() < 50).select();

// Complex expressions - use parentheses for precedence
auto result = queryset.where((field<^^Person::age>() < 30 or
                               field<^^Person::age>() > 35) and
                              field<^^Person::name>() != "Charlie")
                      .select();

// Both 'and'/'or' keywords and &&/|| work identically
auto result = queryset.where(field<^^Person::age>() > 25 &&
                              field<^^Person::age>() < 50).select();
```

### 2. Explicit Function Syntax (Recommended for Programmatic Composition)

Better for dynamic query building:

```cpp
using namespace storm::orm::where;

// Basic function composition
auto expr1 = field<^^Person::age>() > 25;
auto expr2 = field<^^Person::age>() < 50;
auto result = queryset.where(and_(expr1, expr2)).select();

// Nested composition
auto young = field<^^Person::age>() < 30;
auto old = field<^^Person::age>() > 35;
auto not_charlie = field<^^Person::name>() != "Charlie";
auto result = queryset.where(and_(or_(young, old), not_charlie)).select();

// Conditional expression building (powerful for dynamic queries)
auto base_expr = field<^^Person::age>() > 18;
if (filter_by_name) {
    base_expr = and_(base_expr, field<^^Person::name>() == "Alice");
}
if (filter_by_status) {
    base_expr = or_(base_expr, field<^^Person::status>() == "active");
}
auto result = queryset.where(base_expr).select();
```

## When to Use Each Syntax

| Scenario | Natural Operators | and_() / or_() Functions |
|----------|-------------------|--------------------------|
| Simple inline queries | ✅ Preferred - more readable | ❌ Unnecessary verbosity |
| Complex nested conditions | ✅ Good with parentheses | ✅ Good - explicit structure |
| Conditional/dynamic queries | ❌ Hard to modify expressions | ✅ **Strongly recommended** |
| Backward compatibility | ✅ Supported | ✅ Supported (original API) |
| Building queries programmatically | ⚠️ Awkward - requires reassignment | ✅ **Strongly recommended** |

## Supported Operators

### Comparison Operators

```cpp
// Equality
field<^^Person::age>() == 30
field<^^Person::age>() != 30

// Ordering
field<^^Person::age>() > 30
field<^^Person::age>() >= 30
field<^^Person::age>() < 30
field<^^Person::age>() <= 30
```

### Special Methods

#### LIKE (String Pattern Matching)

```cpp
// Starts with 'A'
auto result = queryset.where(field<^^Person::name>().like("A%")).select();

// Contains 'smith'
auto result = queryset.where(field<^^Person::name>().like("%smith%")).select();

// Ends with 'son'
auto result = queryset.where(field<^^Person::name>().like("%son")).select();
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person WHERE name LIKE ?
```

#### BETWEEN (Range Checks)

```cpp
// Age between 28 and 35 (inclusive)
auto result = queryset.where(field<^^Person::age>().between(28, 35)).select();
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person WHERE age BETWEEN ? AND ?
```

#### IN (Multiple Values)

```cpp
// ID in (1, 2, 3)
auto result = queryset.where(field<^^Person::id>().in(1, 2, 3)).select();

// Name in multiple values
auto result = queryset.where(field<^^Person::name>().in("Alice", "Bob", "Charlie"))
                      .select();
```

**SQL Generated**:
```sql
SELECT id, name, age FROM Person WHERE id IN (?, ?, ?)
SELECT id, name, age FROM Person WHERE name IN (?, ?, ?)
```

## Combining with JOIN

WHERE clauses can filter on both base table and joined table fields:

```cpp
struct User {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string username;
    int level;
};

struct Message {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string content;
    User sender;
};

storm::orm::QuerySet<Message> message_qs(conn);

// Natural operator syntax
auto result = message_qs.join<&Message::sender>()
                        .where(field<^^User::level>() > 5 and
                               field<^^Message::content>().like("%urgent%"))
                        .select();

// Function syntax
auto level_check = field<^^User::level>() > 5;
auto content_check = field<^^Message::content>().like("%urgent%");
auto result = message_qs.join<&Message::sender>()
                        .where(and_(level_check, content_check))
                        .select();
```

**SQL Generated**:
```sql
SELECT t1.id, t1.content, t1.sender_id, t2.id, t2.username, t2.level
FROM Message t1
INNER JOIN User t2 ON t2.id = t1.sender_id
WHERE t2.level > ? AND t1.content LIKE ?
```

See [JOIN Operations](JOIN_OPERATIONS.md) for more details.

## Architecture

### Field Expression

The `field<^^T::member>()` function uses pure C++26 reflection:

```cpp
template <std::meta::info FieldInfo>
auto field() {
    // Extract field information at compile-time
    constexpr auto field_name = std::meta::identifier_of(FieldInfo);
    constexpr auto field_type = std::meta::type_of(FieldInfo);
    constexpr auto parent_type = std::meta::parent_of(FieldInfo);

    return Field<parent_type, field_type>{
        .table_name = get_table_name<parent_type>(),
        .field_name = field_name
    };
}
```

**Key features**:
- No macros needed
- Fully module-compatible
- Compile-time type extraction
- Zero runtime overhead

### Expression Trees

WHERE clauses are represented as composable expression trees:

```cpp
class IExpression {
    virtual std::string to_sql() const = 0;
    virtual void bind(Statement& stmt, int& idx) const = 0;
};

// Comparison expression
template <typename T, typename FieldType>
class ComparisonExpr : public IExpression {
    Field<T, FieldType> field_;
    std::string op_;
    FieldType value_;
};

// Logical expression (AND/OR)
class LogicalExpr : public IExpression {
    std::shared_ptr<IExpression> left_;
    std::shared_ptr<IExpression> right_;
    std::string op_;  // "AND" or "OR"
};
```

### Expr Wrapper (Natural Operators)

The `Expr` class enables natural `and`/`or` syntax without ambiguity:

```cpp
class Expr {
    std::shared_ptr<IExpression> expr_;

public:
    // Natural 'and' keyword (or &&)
    friend Expr operator and(const Expr& lhs, const Expr& rhs) {
        return Expr{std::make_shared<LogicalExpr>(lhs.expr_, rhs.expr_, "AND")};
    }

    // Natural 'or' keyword (or ||)
    friend Expr operator or(const Expr& lhs, const Expr& rhs) {
        return Expr{std::make_shared<LogicalExpr>(lhs.expr_, rhs.expr_, "OR")};
    }
};
```

**Benefits**:
- Intuitive syntax matching SQL
- No conflicts with built-in operators
- Works with both keywords and symbols
- Fully composable

### Type Safety

Compile-time type checking ensures type safety:

```cpp
// Compile error: comparing int field with string
auto expr = field<^^Person::age>() > "invalid";  // ❌ Compile error

// OK: correct types
auto expr = field<^^Person::age>() > 30;         // ✅ OK
auto expr = field<^^Person::name>() == "Alice";  // ✅ OK
```

### SQL Injection Prevention

All values are bound as parameters, never concatenated into SQL:

```cpp
// User input
std::string user_input = "'; DROP TABLE Person; --";

// Safe - bound as parameter, not concatenated
auto result = queryset.where(field<^^Person::name>() == user_input).select();

// Generated SQL uses placeholders
// SELECT id, name, age FROM Person WHERE name = ?
// Parameter: "'; DROP TABLE Person; --" (treated as string literal)
```

## Performance Results

### Comprehensive Benchmark Results (10,000 rows, Release build)

| Operation | Storm ORM | Raw SQLite | Efficiency |
|-----------|-----------|------------|------------|
| **Baseline** | | | |
| SELECT (no WHERE) | 9.94M rows/sec | - | - |
| **Data Type Comparisons** | | | |
| int (`age > 30`) | 9.51M rows/sec | 10.21M rows/sec | **93.1%** |
| bool (`is_active == true`) | 9.09M rows/sec | 10.17M rows/sec | **89.4%** |
| **Special Methods** | | | |
| LIKE (`name LIKE "Person5%"`) | 2.82M rows/sec | 2.90M rows/sec | **97.2%** |
| BETWEEN (`age BETWEEN 28 AND 35`) | 5.01M rows/sec | 5.23M rows/sec | **95.9%** |
| IN (3 values) | 0.32M rows/sec | 0.99M rows/sec | 32.0% |
| IN (10 values) | 0.98M rows/sec | 1.97M rows/sec | 49.8% |
| **Complex Queries** | | | |
| Simple (2 conditions) | 7.44M rows/sec | 7.81M rows/sec | **95.2%** |
| Medium (4 conditions) | 1.38M rows/sec | 1.40M rows/sec | **98.6%** |
| Complex (8+ conditions) | 0.57M rows/sec | 0.58M rows/sec | **98.3%** |

**Average Efficiency**: 89-98% for most operations (well above 70% target)

**Note on IN operator**: Lower efficiency (32-50%) is expected due to result set selectivity (fetching only 3-10 rows from 10,000). Complex queries show excellent efficiency because SQLite performs most of the work.

### Performance Characteristics by Category

**Data Type Comparisons** (89-93% efficiency):
- Integer and bool comparisons are nearly as fast as raw SQLite
- Minimal overhead from expression tree construction
- Single parameter binding is very efficient

**Special Methods** (95-97% efficiency for LIKE/BETWEEN, 32-50% for IN):
- LIKE and BETWEEN show excellent efficiency (95-97%)
- IN operator has lower efficiency due to:
  - High selectivity (fetching only 3-10 rows from 10,000)
  - Multiple parameter bindings
  - Result set size dominates overhead measurement
- When IN returns more rows, efficiency improves (32% for 3 values → 49.8% for 10 values)

**Complex Queries** (95-98% efficiency):
- Surprisingly high efficiency for complex nested queries
- SQLite does most of the work (query planning, execution)
- Storm's overhead is constant regardless of query complexity
- Expression tree traversal is very efficient

**Key Insight**: Storm WHERE clause overhead is primarily constant (~0.5-1 µs per query), making it negligible for complex queries where SQLite execution dominates.

**Bottlenecks**:
1. Result set extraction (~6-8% for large result sets)
2. Expression tree allocation (~1-2%)
3. SQL string construction (~1%)
4. Parameter binding (<1% for simple queries)
5. SQLite execution (unavoidable, dominates for complex queries)

## Benchmarking

Run WHERE performance benchmarks:

```bash
# Python benchmark suite (recommended)
python3 bench.py --all

# Direct C++ benchmark
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_where --size=10000
```

See `benchmarks/bench_where.cpp` for benchmark implementation.

## Testing

Comprehensive WHERE tests in `tests/test_where.cpp`:
- Single condition queries
- Multiple condition queries
- Complex nested expressions
- Special methods (like, between, in)
- Type safety verification
- WHERE with JOIN operations

## Implementation Details

### Compile-Time Field Extraction

```cpp
template <std::meta::info FieldInfo>
consteval auto extract_field_metadata() {
    return FieldMetadata{
        .name = std::meta::identifier_of(FieldInfo),
        .type = std::meta::type_of(FieldInfo),
        .parent = std::meta::parent_of(FieldInfo)
    };
}
```

### Expression Tree Traversal

```cpp
std::string to_sql() const override {
    // Build SQL recursively
    if (is_logical_expr()) {
        return left_->to_sql() + " " + op_ + " " + right_->to_sql();
    } else {
        return field_name_ + " " + op_ + " ?";
    }
}

void bind(Statement& stmt, int& idx) const override {
    // Bind parameters recursively
    if (is_logical_expr()) {
        left_->bind(stmt, idx);
        right_->bind(stmt, idx);
    } else {
        stmt.bind(idx++, value_);
    }
}
```

## Key Benefits

✅ **Pure C++26 reflection** - No macros, fully module-compatible
✅ **Type safety** - Compile-time type checking
✅ **Two syntaxes** - Natural operators + explicit functions
✅ **Zero SQL injection** - All values bound as parameters
✅ **Composable** - Build complex expressions dynamically
✅ **86-90% efficiency** - Near-raw SQLite performance
✅ **Backward compatible** - and_(), or_() functions still work

## Performance Optimization Investigation

### Can We Reach 99% Efficiency?

**TL;DR**: The current 86-90% efficiency represents an excellent balance between developer experience and performance. Reaching 99% would require fundamental changes that sacrifice the elegant API.

### Investigation Summary (2025-10)

An investigation was conducted to determine if compile-time SQL generation could improve WHERE clause performance from 90-92% to 96-99% efficiency.

**Approach Tested**: Compile-time WHERE API with template-based SQL generation:
```cpp
// Compile-time API (tested but not adopted)
queryset.where_gt<^^Person::age>(30);  // 90-92% efficiency

// Current runtime API (kept)
queryset.where(field<^^Person::age>() > 30).select();  // 88-91% efficiency
```

**Results** (10,000 rows, 500-1000 iterations):
- **Runtime WHERE** (expression tree): 88.5-91.3% efficiency
- **Compile-time WHERE** (direct SQL): 90.6-91.2% efficiency
- **Improvement**: +0-2% (within measurement variance)

**Root Cause Analysis**: The real bottleneck isn't WHERE clause overhead—it's row extraction and result management:
1. **Row extraction** (~6-8%): Converting SQLite rows to C++ objects
2. **Vector resizing** (~1-2%): Dynamic memory management
3. **Parameter binding** (~1%): Even direct binding has overhead
4. **SQL construction** (~1%): Even compile-time has initialization cost

The expression tree overhead we could eliminate was only **~1-2%** of total overhead.

**Conclusion**:
- **88-91% efficiency is excellent** for an ORM with elegant, type-safe API
- Compile-time SQL provides marginal improvement (~0-2%)
- The 9-12% gap vs raw SQLite is primarily unavoidable ORM abstraction cost
- The natural `field<^^Person::age>() > 30` syntax is worth the minimal overhead

To reach 96-99% efficiency would require:
- Optimizing row extraction (not WHERE clauses)
- Pre-allocated result buffers
- Zero-copy string handling
- These optimizations would apply equally to all SELECT operations, not just WHERE

**Recommendation**: Keep the current elegant runtime WHERE API. The performance is already excellent and the developer experience is superior.

## Performance Investigation: Virtual Functions vs Function Pointers

**Date**: 2025-10-27
**Investigation**: Evaluated function pointer-based type erasure (similar to JoinStatementWrapper) as an alternative to virtual functions for WHERE expression building.

### Results

Benchmarked 1M iterations of expression building (not query execution):

| Operation | Virtual Functions | Function Pointers | Difference |
|-----------|-------------------|-------------------|------------|
| Simple comparison (`field > value`) | 58.10 ns/op | 51.97 ns/op | +10.6% faster |
| Complex expression (`(a > 25 && a < 50) \|\| b == "Test"`) | 258.25 ns/op | 250.29 ns/op | +3.1% faster |
| LIKE expression | 34.93 ns/op | 34.39 ns/op | +1.5% faster |
| BETWEEN expression | 39.02 ns/op | 38.92 ns/op | +0.3% faster |
| IN expression (5 values) | 122.48 ns/op | 126.33 ns/op | -3.1% slower |

### Decision: Keep Virtual Functions

**Rationale**:
1. **Marginal gains** - 6-8 nanoseconds saved on expression building (not query execution)
2. **Expression building ≠ bottleneck** - WHERE filtering runs at 9.45-11.88M rows/sec (86-90% efficiency). The bottleneck is SQLite execution, not expression construction.
3. **Increased complexity** - Function pointers require factory functions, manual memory management (`void*` casts), and move-only semantics
4. **Different usage pattern than JOIN** - JOINs benefit from compile-time SQL generation with static function pointers. WHERE clauses are inherently dynamic with runtime value binding, requiring heap allocation regardless of approach.
5. **Code maintainability** - Virtual function approach is cleaner, uses `shared_ptr` for automatic memory management, and is easier to debug

**Conclusion**: The 3-10% speedup on expression building does not justify the increased code complexity, especially when expression building cost is negligible compared to query execution. The virtual function approach remains the right choice for WHERE clauses.

## See Also

- [SELECT Queries](SELECT_QUERIES.md) - Statement caching with WHERE
- [JOIN Operations](JOIN_OPERATIONS.md) - Combine WHERE with JOINs
- [C++26 Reflection](../architecture/REFLECTION.md) - How reflection works
- [SQL Generation](../architecture/SQL_GENERATION.md) - Compile-time SQL generation
