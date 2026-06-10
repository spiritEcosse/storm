# JOIN Operations

Storm ORM supports efficient single and multi-FK JOIN operations using a type-erased SQL builder pattern that achieves **77% average efficiency** compared to raw SQLite.

## Overview

Key features:
- **Single and multi-FK JOINs** - Variadic template handles both cases
- **Type erasure without std::function** - Uses abstract base class pattern
- **Compile-time SQL generation** - Zero runtime SQL construction
- **Automatic FK population** - Fully populates joined objects
- **77% average efficiency** - Near-raw SQLite performance

## Basic Usage

### Single FK JOIN

```cpp
struct User {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string username;
    int level;
};

struct Message {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string content;
    User sender;  // FK field
};

storm::orm::QuerySet<Message> message_qs(conn);

// Populates sender field fully for each message
auto result = message_qs.join<^^Message::sender>().select();
if (result) {
    for (const auto& msg : result.value()) {
        std::cout << msg.content << " from " << msg.sender.username << std::endl;
    }
}
```

**SQL Generated**:
```sql
SELECT t1.id, t1.content, t1.sender_id, t2.id, t2.username, t2.level
FROM Message t1
INNER JOIN User t2 ON t2.id = t1.sender_id
```

**Performance**: 4.4M rows/sec (59% of raw SQLite for INNER JOIN)

### Multi-FK JOIN

```cpp
struct Message {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string content;
    User sender;    // First FK
    User receiver;  // Second FK
};

// Populates both sender and receiver fields
auto result = message_qs.join<^^Message::sender, ^^Message::receiver>().select();
if (result) {
    for (const auto& msg : result.value()) {
        std::cout << msg.sender.username << " → "
                  << msg.receiver.username << ": "
                  << msg.content << std::endl;
    }
}
```

**SQL Generated**:
```sql
SELECT t1.id, t1.content, t1.sender_id, t1.receiver_id,
       t2.id, t2.username, t2.level,
       t3.id, t3.username, t3.level
FROM Message t1
INNER JOIN User t2 ON t2.id = t1.sender_id
INNER JOIN User t3 ON t3.id = t1.receiver_id
```

**Performance**: 3.7M rows/sec (62% of raw SQLite for INNER JOIN)

## JOIN Types

Storm ORM supports all standard SQL JOIN types:

### INNER JOIN (Default)

Only returns rows where FK relationship exists:

```cpp
auto result = message_qs.join<^^Message::sender>().select();
```

**Use when**: You only want messages with valid senders

### LEFT JOIN

Returns all base table rows, FK fields may be NULL:

```cpp
auto result = message_qs.left_join<^^Message::sender>().select();
```

**Use when**: You want all messages, even those without senders
**Note**: FK fields must use `std::optional<User>` for NULL safety

### RIGHT JOIN

Returns all FK table rows, base fields may be NULL:

```cpp
auto result = message_qs.right_join<^^Message::sender>().select();
```

**Use when**: You want all users, even those without messages

## Many-to-Many Joins (#203)

Many-to-many relationships use a container field annotated with `many_to_many`
(auto-generated junction table) or `many_to_many_through<Model>` (explicit
junction model). The same `join<^^Field>()` API eager-loads the relationship in
a **single query** — no N+1 problem.

### Phase 1: Auto-Generated Junction Table

```cpp
struct Course {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string title;
};

struct Student {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::meta::many_to_many]] std::vector<Course> courses;
};

// One query: students deduplicated, courses collected into each student
auto students = QuerySet<Student>().join<^^Student::courses>().select().execute();
for (const auto& s : *students) {
    // s.courses holds ALL of the student's courses
}
```

- The related type (`Course`) is extracted from the container via C++26 `std::meta`
  — `std::vector<T>`, `plf::hive<T>`, `std::deque<T>`, and
  `std::vector<std::shared_ptr<T>>` / `unique_ptr` elements all work.
- `create_table_if_not_exists<Student>` also creates the junction table
  `Student_Course (Student_id, Course_id, PRIMARY KEY(both))` — naming is
  `<OwnerTable>_<RelatedTable>` with `<Table>_id` columns.
- The m2m container member is **not a column**: plain `select()`, `insert()`,
  `update()` ignore it entirely (zero cost for models without m2m fields).

**SQL Generated**:
```sql
SELECT t1.id, t1.name, t1.age, t3.id, t3.title
FROM (SELECT id, name, age FROM Student) t1
INNER JOIN Student_Course t2 ON t1.id = t2.Student_id
INNER JOIN Course t3 ON t2.Course_id = t3.id
ORDER BY t1.id
```

### Phase 2: Explicit Junction Model (metadata)

```cpp
struct Enrollment {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Pupil pupil;
    [[= storm::meta::FieldAttr::fk]] Course course;
    std::string grade;  // relationship metadata
};

struct Pupil {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::meta::many_to_many_through<Enrollment>]] std::vector<Course> courses;
};

// Simple access — metadata ignored, same single-query eager load
auto pupils = QuerySet<Pupil>().join<^^Pupil::courses>().select().execute();

// Metadata access — query the junction model directly (regular FK joins)
auto enrollments = QuerySet<Enrollment>()
        .join<^^Enrollment::pupil, ^^Enrollment::course>()
        .where(field<^^Enrollment::grade>() == "A")
        .select().execute();
```

The junction table is the through model's own table; FK columns come from its
field names (`pupil_id`, `course_id`). The through model must have exactly one
`FieldAttr::fk` field per side (enforced at compile time).

### Semantics

| Aspect | Behavior |
|---|---|
| `WHERE` / `ORDER BY` / `LIMIT` / `OFFSET` | Apply to **base entities** (inside a base-table subquery) — `limit(1)` returns one student with ALL courses |
| `first()` / `get()` | Entity-level: `get()` errors only on 0 or >1 *entities*, never on multiple relations |
| `rows()` | Yields one aggregated entity at a time (lazy) |
| `join` (INNER) | Drops base entities with no relations |
| `left_join` | Keeps them with an empty container |
| Aggregates (`count()` …) | Count (base, related) **pairs** over the flat 3-table join |
| Result order | Deterministic: user `order_by` (t1-qualified) + pk tiebreak |

### Limitations

- One m2m field per model; self-referential m2m rejected at compile time.
- Write-side helpers (`add()`/`remove()`) are not provided — insert junction
  rows via the through model (`QuerySet<Enrollment>`) or raw SQL for Phase 1.
- The annotation spelling differs from issue #203's sketch
  (`FieldAttr::many_to_many<T>` is impossible — `FieldAttr` is an enum).

## Architecture

### Type-Erased SQL Builder Pattern

Storm uses an abstract base class for type erasure without `std::function` (which causes linker issues with custom libc++):

```cpp
// Abstract base for type erasure
class IJoinStatement {
    virtual std::string to_sql() const = 0;
    virtual std::string build_qualified_select_fields() const = 0;
    virtual void extract_row(void* stmt, void* obj) const = 0;
};

// Unified variadic template (handles single + multi FK)
template <typename T, ConnType, std::meta::info... FKFields>
class JoinStatement : public IJoinStatement {
    // Pure SQL builder - no execute(), no caching

    std::string to_sql() const override {
        // Generate: " INNER JOIN table t2 ON t2.id = t1.fk_id"
        // Uses fold expressions for variadic FKFields
    }

    void extract_row(void* stmt, void* obj) const override {
        // Extract FK fields using compile-time type information
    }
};
```

### QuerySet Integration

QuerySet stores the JOIN statement using type erasure:

```cpp
template <class T> class QuerySet {
    mutable std::unique_ptr<IJoinStatement> join_stmt_;

    template <std::meta::info... FKFields>
    auto&& join(this auto&& self) {
        self.join_stmt_ = std::make_unique<JoinStatement<T, ConnType, FKFields...>>();
        return self;  // Chainable
    }

    auto select() {
        if (join_stmt_) {
            return get_select_statement().execute_optimized(join_stmt_.get());
        }
        return get_select_statement().execute_optimized();
    }
};
```

### SelectStatement Integration

SelectStatement handles JOIN queries with separate statement caching:

```cpp
template <typename JoinStmt = void>
auto execute_optimized(JoinStmt* join_stmt = nullptr) {
    if constexpr (!std::is_void_v<JoinStmt>) {
        // Build SQL: SELECT t1.*, t2.* FROM table t1 + join_stmt->to_sql()
        // Extract rows: join_stmt->extract_row(stmt, obj)
        // Use separate statement cache for JOIN queries
    } else {
        // Simple SELECT without JOIN
    }
}
```

## Performance Analysis

### Benchmark Results (10,000 rows, 100 iterations, Release build)

| JOIN Operation | Storm ORM | Raw SQLite | Efficiency |
|----------------|-----------|------------|------------|
| Simple SELECT (no JOIN) | 8.4M rows/sec | - | - |
| RIGHT JOIN (single FK) | 4.5M rows/sec | 5.0M rows/sec | 90% |
| RIGHT JOIN (multi FK) | 2.7M rows/sec | 3.2M rows/sec | 84% |
| LEFT JOIN (single FK) | 6.1M rows/sec | 6.8M rows/sec | 90% |
| LEFT JOIN (multi FK) | 3.9M rows/sec | 5.2M rows/sec | 75% |
| INNER JOIN (single FK) | 4.4M rows/sec | 7.4M rows/sec | 59% |
| INNER JOIN (multi FK) | 3.7M rows/sec | 6.0M rows/sec | 62% |

**Average Efficiency**: ~77% of raw SQLite performance

### Performance Characteristics

**LEFT/RIGHT JOINs**: 75-90% efficiency (excellent)
- Less object construction complexity
- Simpler NULL handling

**INNER JOINs**: 59-62% efficiency (good)
- More complex object construction
- Higher allocation overhead

### Performance Bottlenecks

The real performance bottlenecks are **NOT function pointers** (we tested this):

1. **String allocations** (30-40% of runtime)
   - Each `std::string` field requires heap allocation
   - Multiple string fields per joined object

2. **Object construction** (~10%)
   - Creating and populating complex objects
   - Multiple fields per object

3. **Vector management**
   - Resizing, copying, moving objects in result vectors

4. **Multi-column extraction** (~35%)
   - 5-8 columns for JOIN vs 2-3 for simple SELECT
   - Type dispatch overhead for each field

5. **SQL execution** (~20%)
   - Unavoidable, same as raw SQLite

### Template vs Function Pointer Experiment

We attempted to eliminate function pointer overhead using templates to preserve FK information at compile-time:

**Results: Templates Made It WORSE**
- **Function pointer approach** (current): 6.9M rows/sec (70% of raw) ✅ **BEST**
- **Template approach** (attempted): 4.9M rows/sec (49% of raw) ❌ **28% SLOWER**

**Why templates failed:**
1. **Code bloat** - More template instantiations → worse instruction cache locality
2. **Compiler optimization** - Modern compilers optimize indirect calls well with PGO
3. **Inlining limits** - Deep call chains hit compiler inlining budget
4. **Register pressure** - Fully inlined code increased register spilling

**Key lesson**: Don't assume eliminating indirect calls improves performance. Profile first, optimize second.

## Optimization Recommendations

Current 50-70% efficiency is **respectable for a full ORM** with reflection-based mapping. Further gains require architectural changes:

### 1. String Handling (Biggest Potential Win)
- Use `std::string_view` for read-only operations
- Implement move semantics throughout extraction chain
- Consider string interning for repeated values
- Arena allocator for temporary strings

### 2. Memory Management
- Object pooling to reuse allocated objects
- Custom allocator optimized for ORM access patterns
- Better size estimation (currently pre-allocates 10K)

### 3. Column Extraction
- Batch extraction by type (all ints, then all strings)
- SIMD for type checking and conversion
- Specialized fast paths for common type combinations

### 4. Caching Improvements
- Cache field offset calculations
- Reuse extraction buffers across queries
- Pre-build type dispatch tables at compile-time

## Chaining with WHERE

JOIN operations can be combined with WHERE clauses:

```cpp
using namespace storm::orm::where;

// Filter on both base table and joined table
auto result = message_qs.join<^^Message::sender>()
                        .where(field<^^User::level>() > 5 and
                               field<^^Message::content>().like("%urgent%"))
                        .select();
```

**SQL Generated**:
```sql
SELECT t1.id, t1.content, t1.sender_id, t2.id, t2.username, t2.level
FROM Message t1
INNER JOIN User t2 ON t2.id = t1.sender_id
WHERE t2.level > 5 AND t1.content LIKE '%urgent%'
```

See [WHERE Clauses](WHERE_CLAUSES.md) for more details.

## Testing

Comprehensive JOIN tests in `tests/test_fk_fields.cpp`:
- Single FK JOINs
- Multi-FK JOINs
- Full object population verification
- INSERT/UPDATE/DELETE with FK fields
- Batch operations with FK fields

## Benchmarking

Run JOIN performance benchmarks:

```bash
# Python benchmark suite (recommended)
python3 bench.py --joins --messages=10000

# Direct C++ benchmark
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Run all JOIN benchmarks
./build/release/benchmarks/bench_join

# Run specific JOIN type
./build/release/benchmarks/bench_join --storm-join-1 --size=10000
./build/release/benchmarks/bench_join --storm-join-multi --size=10000

# Compare with raw SQLite
./build/release/benchmarks/bench_join --storm-join-1 --raw-join-1 --size=10000
```

See `benchmarks/bench_join.cpp` for benchmark implementation.

## Key Benefits

✅ **No std::function** - Avoids custom libc++ linker errors
✅ **Single variadic template** - Not separate Single/Multi classes
✅ **Abstract base class** - Clean type erasure pattern
✅ **Compile-time SQL generation** - Zero runtime overhead
✅ **Zero runtime overhead** - Uses `if constexpr` dispatch
✅ **Separate statement caching** - JOIN vs simple SELECT
✅ **77% average efficiency** - Respectable ORM performance

## See Also

- [WHERE Clauses](WHERE_CLAUSES.md) - Combine JOINs with filtering
- [SELECT Queries](SELECT_QUERIES.md) - Statement caching details
- [Statement Caching](../architecture/STATEMENT_CACHING.md) - Caching architecture
- [SQL Generation](../architecture/SQL_GENERATION.md) - Compile-time SQL generation
