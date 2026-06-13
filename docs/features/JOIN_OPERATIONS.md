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

Storm ORM supports INNER and LEFT JOIN. RIGHT JOIN was removed in #397: its only
distinguishing output — unmatched related-side rows materialized as defaulted base
entities — was not useful. The related-side query ("all users, each with the
messages pointing at them") is tracked as a reverse-relation feature in #398.

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

## Many-to-Many Joins (#203)

Many-to-many relationships use a container field annotated with `many_to_many`
(auto-generated junction table) or `many_to_many_through<Model>` (explicit
junction model). The same `join<^^Field>()` API eager-loads the relationship with
a **predicate-pushdown two-query** strategy (#391) — no N+1 problem. See
[Execution Strategy](#execution-strategy-391) below for why two queries beat one.
Several m2m fields of the same model can be loaded in **one call** (#392) — see
[Multiple m2m relations](#multiple-m2m-relations-392).

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

// Two queries (in one transaction): base students, then their courses, stitched
// client-side by a pk→entity hash map.
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

**SQL Generated** (two statements, run inside one transaction):
```sql
-- Q1: the base entities to load (a plain SELECT — no join, no sorter)
SELECT id, name, age FROM Student;

-- Q2: their courses, filtered by the SAME base subquery; stitched by owner pk
SELECT t2.Student_id, t3.id, t3.title
FROM Student_Course t2 INNER JOIN Course t3 ON t2.Course_id = t3.id
WHERE t2.Student_id IN (SELECT id FROM Student)
```

`WHERE` / `ORDER BY` / `LIMIT` / `OFFSET` are applied to Q1 **and** repeated inside
Q2's `IN (…)` subquery, so both pick exactly the same base entities.

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

// Simple access — metadata ignored, same two-query eager load
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

### Multiple m2m relations (#392)

A model may carry several m2m fields; one `join<>()`/`left_join<>()` call loads
any subset of them:

```cpp
struct Member {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::meta::many_to_many]] std::vector<Course> courses;
    [[= storm::meta::many_to_many]] std::vector<Club> clubs;
};

// One Q1 (base members) + one Q2 PER relation, all in one transaction:
auto members = QuerySet<Member>()
        .join<^^Member::courses, ^^Member::clubs>()
        .select().execute();
```

- **Cost is additive** (`K1 + K2` related rows), never multiplicative — each
  relation is one extra Q2 keyed off the same Q1 `pk → entity` map. There is no
  `K1×K2` cartesian product anywhere on the eager-load path.
- **INNER** (`join`) drops a base entity if its container is empty in **any**
  listed relation; **LEFT** (`left_join`) keeps every base entity and fills each
  relation's container independently (Django semantics).
- `create_table_if_not_exists<Member>` creates **one junction table per
  auto-m2m field** (`Member_Course`, `Member_Club`).
- Duplicate fields (`join<^^M::courses, ^^M::courses>`) and mixing FK fields
  with m2m fields in one call are rejected at compile time.

### Semantics

| Aspect | Behavior |
|---|---|
| `WHERE` / `ORDER BY` / `LIMIT` / `OFFSET` | Apply to **base entities** (Q1 and the Q2 IN-subquery) — `limit(1)` returns one student with ALL courses |
| `first()` / `get()` | Entity-level: `get()` errors only on 0 or >1 *entities*, never on multiple relations |
| `rows()` | Yields aggregated entities; **materialized eagerly** (Q2 needs the full base set, so true streaming is not possible) |
| `join` (INNER) | Drops base entities empty in **any** listed relation (a post-stitch filter) |
| `left_join` | Keeps them with empty container(s) (the natural case — Q1 already yielded them) |
| Aggregates (`count()` …) | Count (base, related…) **tuples** over the flat chained join (`get_complete_sql`) — pairs for one relation, cartesian tuples for several |
| Result order | The order Q1 returns base entities (`order_by` if given) — no pk tiebreak is needed, since the stitch is a hash map, not row-adjacency |
| Consistency | Q1 + Q2 run inside one transaction (snapshot consistency) |

### Limitations

- Self-referential m2m, duplicate fields in one call, and mixed FK + m2m in one
  call are rejected at compile time. INNER vs LEFT applies to the whole call —
  mixing per relation is not expressible (chaining a second `join()` call
  replaces the first, as for FK joins).
- Write-side helpers (`add()`/`remove()`) are not provided — insert junction
  rows via the through model (`QuerySet<Enrollment>`) or raw SQL for Phase 1.
- The annotation spelling differs from issue #203's sketch
  (`FieldAttr::many_to_many<T>` is impossible — `FieldAttr` is an enum).

### Execution Strategy (#391)

The m2m eager load runs as **two queries inside one transaction**, stitched
client-side by a `pk → entity` hash map (`SelectStatement::execute_m2m_2query`):

1. **Q1** selects the base entities (`build_base_subquery`) — a plain `SELECT`,
   no join, no sorter. Its results go into a `plf::hive<T>` (stable pointers),
   and a `std::unordered_map<int64_t, T*>` indexes them by primary key.
2. **Q2** — one per eager-loaded relation (#392) — selects `(owner_pk, related.*)`
   from that relation's junction ⋈ related table, filtered by
   `WHERE owner_id IN (<the same base subquery>)` (`build_q2_sql`).
   Each row is appended to its owner's container via the hash-map lookup.
3. **INNER** then drops entities whose container stayed empty in any inner
   relation; **LEFT** keeps them. Every Q2 is the *same* shape (an INNER
   junction ⋈ related join) — the INNER/LEFT difference is a post-stitch
   filter, never an SQL difference.

**Why two queries beat one.** The former single-query plan was a 3-table join over
a base-table subquery that ended in `ORDER BY t1.<pk>` so one entity's rows were
adjacent (the aggregation loop relied on that). SQLite executes that `ORDER BY`
with a `USE TEMP B-TREE FOR ORDER BY` — *every* one of the `N×K` result rows,
including the duplicated base columns, passes through the sorter. The two-query
plan never sorts (the stitch is a hash map) and reads each base row's columns
once. Measured in-Storm (Release, fan-out sweep, N=100 base entities):

| fan-out | rows | 1-query | 2-query | speedup |
|---|---|---|---|---|
| 1 | 100 | 24.0 µs | 26.1 µs | −8.7% (FK-shaped; least representative) |
| 10 | 1,000 | 190 µs | 127 µs | **+33.5%** |
| 50 | 5,000 | 907 µs | 519 µs | **+42.8%** |
| 200 | 20,000 | 3.70 ms | 2.01 ms | **+45.6%** |

Fan-out 1 (one related row per entity) is FK-shaped, not m2m-shaped, and pays a
~2 µs constant cost (the extra prepare + transaction); every representative m2m
fan-out wins by 33–46%. `count()` and other aggregates still run the modifier-free
chained join (`get_complete_sql`) — `(base, related)` pairs for one relation,
cartesian tuples when several relations are joined (#392).

## Reverse-FK Joins (#398)

Storm's other join selectors start from the base model: FK fields (`^^Task::assignee`)
or m2m containers (`^^Student::courses`). A **reverse-FK join** starts from the
*related* side: "all Persons, each with the Tasks that point at them". The SQL
identity is `Task RIGHT JOIN Person ≡ Person LEFT JOIN Task` — Storm expresses the
right-hand side directly, with the base model (`Person`) on the correct side.

### 1. Annotated container destination (`select()`)

`select()` returns `hive<Person>`, so `Person` needs a member to receive the tasks.
Declare a reverse-FK container (not a column — invisible to CRUD, like m2m):

```cpp
struct Task; // forward declaration breaks the Person⟷Task reference cycle

struct Person {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    // Filled on eager load. The annotation names the OWNER TYPE; the unique FK
    // back at Person is resolved at instantiation.
    [[= storm::meta::reverse_fk<^^Task>]] std::vector<Task> tasks;
};

struct Task {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string title;
    [[= storm::meta::FieldAttr::fk]] Person assignee;   // FK → Person
};

person_qs.left_join<^^Person::tasks>().select();   // all Persons; .tasks empty when none
person_qs.join<^^Person::tasks>().select();        // INNER: drops Persons with no Tasks
```

The annotation argument is **the owning model type** (`^^Task`), resolved to that
model's unique `FieldAttr::fk` member pointing back at `Person`. The type form (not
a member splice) is required: `Person` holds `vector<Task>` and `Task` holds `Person`
by value, a reference cycle that only compiles when `Task` is forward-declared at the
annotation site — which rules out a `^^Task::assignee` member splice there.
The container element may be `T`, `plf::hive<T>`, or `vector<shared_ptr<T>>`, as for m2m.

### 2. Cross-model FK selector (aggregate / filter chains)

`join`/`left_join` also accept an FK field **of another model** that points at the
base. No destination is needed because these chains never materialize base+related
entities — they absorb the aggregate-over-RIGHT-JOIN capability that `right_join`
(removed in #397) pretended to offer:

```cpp
// Tasks per person, INCLUDING persons with zero tasks (LEFT keeps the NULL row)
QuerySet<Person>().left_join<^^Task::assignee>().count().execute();

// Disambiguation: when the owner has several FKs to the base, name the exact field
QuerySet<Reporter>().join<^^Bug::author>().count().execute();    // ON t2.author_id = t1.id
QuerySet<Reporter>().join<^^Bug::reviewer>().count().execute();  // ON t2.reviewer_id = t1.id
```

### Execution & semantics

Identical to m2m (#391): `select()` runs **two queries in one transaction** stitched
by a `pk → entity` hash map. Q1 is the base subquery; Q2 hits the **owning table
directly** (no junction) —
`SELECT t2.<fk>_id, t2.<owner cols> FROM <Owner> t2 WHERE t2.<fk>_id IN (<base subquery>)`.
WHERE / ORDER BY / LIMIT / OFFSET bound the **base** entities; INNER drops
zero-relation entities after the stitch, LEFT keeps them. Multi-relation joins
compose per #392 (one Q2 per relation, additive cost). Aggregates / anti-joins use
the modifier-free complete SQL `Person t1 <KW> Owner t2 ON t2.<fk>_id = t1.<pk>`.

### Limitations

- **`select()` disambiguation** of several owner FKs to the base is not expressible:
  the destination annotation must use the owner *type* (the reference cycle forbids a
  member splice), so the owner must have exactly **one** FK back at the base.
  Multi-FK disambiguation is available only on the aggregate/filter selector path
  (`^^Bug::author` vs `^^Bug::reviewer`), which has no container and no cycle.
- Eager only (no lazy loading), no tuple/pair result shapes, no `right_join`-style
  base rows with defaulted fields (removed in #397).
- The annotation spelling carries the owner type, not `FieldAttr::reverse_fk` —
  `FieldAttr` is an enum, so a templated enumerator is impossible (as for m2m).

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
| LEFT JOIN (single FK) | 6.1M rows/sec | 6.8M rows/sec | 90% |
| LEFT JOIN (multi FK) | 3.9M rows/sec | 5.2M rows/sec | 75% |
| INNER JOIN (single FK) | 4.4M rows/sec | 7.4M rows/sec | 59% |
| INNER JOIN (multi FK) | 3.7M rows/sec | 6.0M rows/sec | 62% |

**Average Efficiency**: ~77% of raw SQLite performance

### Performance Characteristics

**LEFT JOINs**: 75-90% efficiency (excellent)
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
