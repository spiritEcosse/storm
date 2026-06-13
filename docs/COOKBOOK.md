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
auto seniors = qs.where(f<^^Person::age>() > 30).select();

// With ORDER BY + LIMIT + OFFSET
auto page = qs
    .where(f<^^Person::is_active>() == true)
    .order_by<^^Person::name>()
    .limit(10)
    .offset(20)
    .select();
```

## WHERE Clauses

```cpp
// Comparison operators
qs.where(f<^^Person::age>() == 30).select();
qs.where(f<^^Person::age>() != 30).select();
qs.where(f<^^Person::age>() > 30).select();
qs.where(f<^^Person::age>() >= 30).select();
qs.where(f<^^Person::age>() < 30).select();
qs.where(f<^^Person::age>() <= 30).select();

// LIKE
qs.where(f<^^Person::name>().like("A%")).select();

// IN
qs.where(f<^^Person::age>().in(25, 30, 35)).select();

// BETWEEN
qs.where(f<^^Person::age>().between(25, 35)).select();

// AND / OR
qs.where(f<^^Person::age>() > 30 && f<^^Person::is_active>() == true).select();
qs.where(f<^^Person::age>() < 25 || f<^^Person::age>() > 40).select();
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

## Automatic Timestamps

```cpp
struct Article {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string title;
    [[=storm::meta::FieldAttr::auto_create]] std::chrono::system_clock::time_point created_at;
    [[=storm::meta::FieldAttr::auto_update]] std::chrono::system_clock::time_point updated_at;
};

QuerySet<Article> qs;

// INSERT — created_at and updated_at are stamped automatically (any value you set is ignored).
qs.insert(Article{.title = "Hello"}).execute();

// The object in memory is NOT mutated — re-SELECT to read the stamped values.
auto saved = *qs.select().execute().value().begin();

// UPDATE — updated_at is re-stamped; created_at is preserved by passing the saved value back.
saved.title = "Hello (edited)";
qs.update(saved).execute();
```

`auto_create` stamps on INSERT only; `auto_update` stamps on INSERT and UPDATE. Both
fields must be `std::chrono::system_clock::time_point`. See
[reference/FIELD_TYPES.md](reference/FIELD_TYPES.md#automatic-timestamps-auto_create--auto_update).

## DELETE

```cpp
// Single erase
qs.erase(p);

// Conditional bulk erase — one DELETE ... WHERE statement (#198)
qs.where(f<^^Person::is_active>() == false).erase().execute();

// Erase all (explicit full-table wipe). erase() with no where() is refused.
qs.erase_all().execute();
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
auto active_count = qs.where(f<^^Person::is_active>() == true).count().execute();
```

## GROUP BY + HAVING

```cpp
// Group and count
auto groups = qs.group_by<^^Person::is_active>().count().execute();

// GROUP BY with HAVING
auto large_groups = qs
    .group_by<^^Person::age>()
    .having(f<^^Person::age>() > 30)
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
auto combined = qs1.where(f<^^Person::age>() > 30)
    .union_with(qs2.where(f<^^Person::is_active>() == true))
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
    qs.where(f<^^Person::age>() > 30).select();
}

std::jthread t1(worker), t2(worker);
```
