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

### Benchmark Results (10,000 rows, Release build)

| Operation | Storm ORM | Raw SQLite | Efficiency |
|-----------|-----------|------------|------------|
| SELECT (no WHERE) | 12.81M rows/sec | - | - |
| WHERE (single condition) | 11.88M rows/sec | 13.82M rows/sec | 86.0% |
| WHERE (multiple conditions) | 9.45M rows/sec | 10.57M rows/sec | 89.5% |

**Average Efficiency**: 86-90% of raw SQLite (well above 70% target)

### Performance Characteristics

**Single condition**: 86% efficiency
- Minimal overhead
- Simple SQL generation
- One parameter binding

**Multiple conditions**: 89.5% efficiency
- Excellent composition overhead
- Expression tree traversal is efficient
- Multiple parameter bindings

**Bottlenecks**:
1. Expression tree allocation (~5%)
2. String construction for SQL (~3%)
3. Parameter binding (~2%)
4. SQLite execution (unavoidable)

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

## See Also

- [SELECT Queries](SELECT_QUERIES.md) - Statement caching with WHERE
- [JOIN Operations](JOIN_OPERATIONS.md) - Combine WHERE with JOINs
- [C++26 Reflection](../architecture/REFLECTION.md) - How reflection works
- [SQL Generation](../architecture/SQL_GENERATION.md) - Compile-time SQL generation
