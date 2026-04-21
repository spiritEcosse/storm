# Storm ORM Cookbook

Quick-reference patterns for common Storm ORM operations.

## Setup

```cpp
import storm;
using namespace storm;

// Define a model — reflection maps fields to columns automatically
struct Person {
    [[storm::primary_key, storm::auto_increment]] int id;
    std::string name;
    int age;
    double salary;
    bool is_active;
    std::optional<int> score;
};

// Connect (thread-local, one per thread)
QuerySet<Person>::set_default_connection(":memory:");
QuerySet<Person> qs;
```

## INSERT

```cpp
// Single insert (id auto-generated)
Person p{.name = "Alice", .age = 30, .salary = 75000.0, .is_active = true};
qs.insert(p);

// Batch insert
plf::hive<Person> people;
people.insert({.name = "Bob", .age = 25, .salary = 60000.0, .is_active = true});
people.insert({.name = "Carol", .age = 35, .salary = 80000.0, .is_active = false});
qs.insert(people);
```

## SELECT

```cpp
// Select all
auto all = qs.select();

// With WHERE
auto seniors = qs.where(field<^^Person::age>() > 30).select();

// With ORDER BY + LIMIT + OFFSET
auto page = qs
    .where(field<^^Person::is_active>() == true)
    .order_by<^^Person::name>()
    .limit(10)
    .offset(20)
    .select();
```

## WHERE Clauses

```cpp
// Comparison operators
qs.where(field<^^Person::age>() == 30).select();
qs.where(field<^^Person::age>() != 30).select();
qs.where(field<^^Person::age>() > 30).select();
qs.where(field<^^Person::age>() >= 30).select();
qs.where(field<^^Person::age>() < 30).select();
qs.where(field<^^Person::age>() <= 30).select();

// LIKE
qs.where(field<^^Person::name>().like("A%")).select();

// IN
qs.where(field<^^Person::age>().in(25, 30, 35)).select();

// BETWEEN
qs.where(field<^^Person::age>().between(25, 35)).select();

// AND / OR
qs.where(field<^^Person::age>() > 30 && field<^^Person::is_active>() == true).select();
qs.where(field<^^Person::age>() < 25 || field<^^Person::age>() > 40).select();
```

## UPDATE

```cpp
// Single update
Person p = /* ... fetched from select ... */;
p.salary = 85000.0;
qs.update(p);

// Batch update
auto people = qs.select();
for (auto& p : people) { p.salary *= 1.1; }
qs.update(people);
```

## DELETE

```cpp
// Single erase
qs.erase(p);

// Erase all matching
auto inactive = qs.where(field<^^Person::is_active>() == false).select();
qs.erase(inactive);

// Erase all
qs.erase_all();
```

## Aggregates

```cpp
// Scalar aggregates (no GROUP BY) — use .get()
auto count = qs.count().execute();                           // int64_t
auto total = qs.sum<^^Person::salary>().execute();           // int64_t
auto avg   = qs.avg<^^Person::salary>().execute();           // double
auto lo    = qs.min<^^Person::age>().execute();              // int64_t
auto hi    = qs.max<^^Person::age>().execute();              // int64_t

// With WHERE
auto active_count = qs.where(field<^^Person::is_active>() == true).count().execute();
```

## GROUP BY + HAVING

```cpp
// Group and count
auto groups = qs.group_by<^^Person::is_active>().count().execute();

// GROUP BY with HAVING
auto large_groups = qs
    .group_by<^^Person::age>()
    .having(field<^^Person::age>() > 30)
    .count()
    .execute();
```

## DISTINCT

```cpp
// Single field
auto names = qs.distinct<^^Person::name>().execute();

// Multi-field
auto combos = qs.distinct<^^Person::name, ^^Person::age>().execute();
```

## Column Projection (VALUES)

```cpp
// Single column → hive<T>
auto names = qs.values<^^Person::name>().execute();  // plf::hive<std::string>

// Multiple columns → hive<tuple<...>>
auto pairs = qs.values<^^Person::name, ^^Person::age>().execute();
// plf::hive<std::tuple<std::string, int>>
```

## JOIN

```cpp
struct Message {
    [[storm::primary_key, storm::auto_increment]] int id;
    std::string content;
    [[storm::foreign_key<^^Person::id>]] int sender;
};

QuerySet<Message> msg_qs;
auto results = msg_qs.join<Message>().where(...).select();
```

## Set Operations

```cpp
QuerySet<Person> qs1, qs2;

// UNION (deduplicated)
auto combined = qs1.where(field<^^Person::age>() > 30)
    .union_with(qs2.where(field<^^Person::is_active>() == true))
    .select();

// UNION ALL (keeps duplicates)
auto all = qs1.where(...).union_all(qs2.where(...)).select();

// INTERSECT / EXCEPT
auto common = qs1.where(...).intersect(qs2.where(...)).select();
auto diff   = qs1.where(...).except_with(qs2.where(...)).select();
```

## Thread Safety

```cpp
// Each thread gets its own connection — safe
void worker() {
    QuerySet<Person>::set_default_connection(":memory:");
    QuerySet<Person> qs;
    qs.where(field<^^Person::age>() > 30).select();
}

std::jthread t1(worker), t2(worker);
```
