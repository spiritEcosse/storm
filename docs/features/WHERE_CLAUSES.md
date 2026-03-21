# WHERE Clauses

Build WHERE filters using field expressions and operators.

## Basic Syntax

WHERE expressions use field access via reflection with compile-time operators.

```cpp
auto results = QuerySet<Person>()
    .where(field<^^Person::age>() > 30)
    .select();
```

The `field<^^Member>()` function creates a field expression for type-safe comparisons at compile time.

## Comparison Operators

All 6 comparison operators are supported.

```cpp
// Equals
auto results = QuerySet<Person>()
    .where(field<^^Person::age>() == 30)
    .select();

// Not equals
auto results = QuerySet<Person>()
    .where(field<^^Person::age>() != 30)
    .select();

// Greater/Less than
auto results = QuerySet<Person>()
    .where(field<^^Person::age>() > 30)
    .select();

auto results = QuerySet<Person>()
    .where(field<^^Person::age>() <= 65)
    .select();
```

| Operator | SQL | Example |
|---|---|---|
| `==` | `=` | `age == 30` |
| `!=` | `!=` | `age != 30` |
| `>` | `>` | `age > 30` |
| `>=` | `>=` | `age >= 30` |
| `<` | `<` | `age < 30` |
| `<=` | `<=` | `age <= 30` |

## String Operations

### LIKE pattern matching

The `%` wildcard matches any sequence of characters.

```cpp
// WHERE name LIKE 'Al%'
auto results = QuerySet<Person>()
    .where(field<^^Person::name>().like("Al%"))
    .select();
```

### BETWEEN range queries

The `between()` method creates a range check.

```cpp
// WHERE age BETWEEN 25 AND 65
auto results = QuerySet<Person>()
    .where(field<^^Person::age>().between(25, 65))
    .select();
```

## COLLATE

Collation for string comparisons (case-insensitive, etc).

```cpp
auto results = QuerySet<Person>()
    .where(field<^^Person::name>().collate(Collate::NoCase) == "alice")
    .select();
```

| Option | Behavior |
|---|---|
| `Collate::Binary` | Binary comparison (default) |
| `Collate::NoCase` | Case-insensitive (locale-independent) |
| `Collate::RTrim` | Right-trim whitespace before comparing |

Collation is applied during query construction and compiled into the WHERE SQL.

## NULL Checks

Test for NULL or NOT NULL values on optional fields.

### is_null()

Generate an `IS NULL` check.

```cpp
// SELECT * FROM person WHERE score IS NULL
auto nulls = QuerySet<Person>()
    .where(field<^^Person::score>().is_null())
    .select();
```

### is_not_null()

Generate an `IS NOT NULL` check.

```cpp
// SELECT * FROM person WHERE score IS NOT NULL
auto non_nulls = QuerySet<Person>()
    .where(field<^^Person::score>().is_not_null())
    .select();
```

### Using nullopt for NULL checks

Comparison with `std::nullopt` generates the same SQL.

```cpp
// These are equivalent:
.where(field<^^Person::score>().is_null())
.where(field<^^Person::score>() == std::nullopt)

// These are equivalent:
.where(field<^^Person::score>().is_not_null())
.where(field<^^Person::score>() != std::nullopt)
```

### NULL checks with COLLATE

COLLATE can be combined with NULL checks on optional string fields.

```cpp
// SELECT * FROM person WHERE nickname COLLATE NOCASE IS NULL
auto results = QuerySet<Person>()
    .where(field<^^Person::nickname>().collate(Collate::NoCase).is_null())
    .select();
```

### Composing NULL checks

NULL checks compose with AND/OR like any other expression.

```cpp
// SELECT * FROM person WHERE score IS NULL AND age > 30
auto results = QuerySet<Person>()
    .where(field<^^Person::score>().is_null() && field<^^Person::age>() > 30)
    .select();
```

## Logical Composition

Expressions can be combined with `&&` (AND) and `||` (OR).

```cpp
// WHERE (age > 30) AND (name == "Alice")
auto results = QuerySet<Person>()
    .where((field<^^Person::age>() > 30) && (field<^^Person::name>() == "Alice"))
    .select();

// WHERE (age < 25) OR (salary > 100000)
auto results = QuerySet<Person>()
    .where((field<^^Person::age>() < 25) || (field<^^Person::salary>() > 100000))
    .select();

// Complex nesting
auto results = QuerySet<Person>()
    .where(
        (field<^^Person::age>() > 30 && field<^^Person::salary>() > 50000) ||
        (field<^^Person::years_experience>() >= 10)
    )
    .select();
```

Parentheses improve readability and ensure correct precedence.
