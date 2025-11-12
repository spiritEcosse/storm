# Common Development Tasks

## Adding a New Database Operation

1. Create statement class in `src/orm/statements/` inheriting from `BaseStatement<T>`
2. Implement single-object and batch operations (`std::span<const T>`)
3. Choose return type:
   - INSERT: `std::expected<int64_t, Error>` or `std::expected<std::vector<int64_t>, Error>`
   - DELETE/UPDATE: `std::expected<void, Error>`
   - SELECT: `std::expected<std::vector<T>, Error>`
4. Consider statement caching pattern:
   - Add `cached_stmt_` member, implement `execute_single_optimized()`
   - Add `std::unique_ptr<YourStatement>` in QuerySet with lazy initialization
5. Implement compile-time SQL generation using ConstexprString
6. Add method to `QuerySet` class that delegates to statement
7. Add comprehensive tests in `tests/test_*.cpp`
8. Consider optimizations: `if constexpr` type dispatch, pre-allocation, inline functions with compiler hints

## Working with Bulk Operations

### INSERT operations with auto-generated IDs

```cpp
// Single insert - returns ID
auto result = queryset.insert(Person{0, "Dave", 40});
int64_t id = result.value();

// Batch insert - returns all IDs
std::vector<Person> people = {{0, "Alice", 25}, {0, "Bob", 30}};
auto result = queryset.insert(std::span<const Person>(people));
const auto& ids = result.value();
```

### UPDATE/DELETE operations

```cpp
// Single update
Person person{1, "Alice", 26};
queryset.update(person);

// Batch update/delete
queryset.update(std::span<const Person>(people));
queryset.remove(std::span<const Person>(people));
```

### SELECT operations

```cpp
auto result = queryset.select();  // Returns all rows
const auto& people = result.value();

// Repeated calls use cached statement
for (int i = 0; i < 100; ++i) {
    auto result = queryset.select();  // Optimal performance
}
```

## Optimizing Statement Performance

1. **Use BaseStatement utilities**: `execute_with_transaction()`, shared binding
2. **Implement compile-time field binding** with index sequences and fold expressions
3. **Smart thresholds**: Consider SQLite variable limit (999)
4. **Cache SQL strings**: Static methods + thread-local caching
5. **Pre-compute metadata**: `constexpr`/`consteval` functions
6. **Batch operations**: Support bulk SQL (IN, multiple VALUES) + transactions
7. **Optimize allocation**: Pre-calculate sizes, reserve memory
8. **Thread-local caching**: 8-entry cache for frequent patterns

## Index Sequence Optimization Template

```cpp
// In BaseStatement-derived class
static constexpr auto field_count_ = get_field_count();
static constexpr auto all_members_ = get_all_field_members<field_count_>();
using field_indices_t = std::make_index_sequence<field_count_>;

auto bind_all_fields(Statement& stmt, const T& obj) {
    return Base::template bind_all_fields_impl<ConnType, Statement>(
        stmt, obj, typename Base::field_indices_t());
}
```

## Compile-Time SQL Generation Template

```cpp
class YourStatement : private BaseStatement<T> {
    static consteval auto build_sql_array() {
        constexpr size_t sql_size = calculate_sql_size() + 50;
        ConstexprString<sql_size> result;
        result.append("SELECT * FROM ").append(Base::table_name_);
        // ... build SQL ...
        return result;
    }
    static constexpr auto sql_array = build_sql_array();
    static inline const std::string sql_string = std::string(sql_array);
};
```

## Thread-Local Caching Template

```cpp
struct YourStatementSQLCache {
    static constexpr size_t CACHE_SIZE = 8;
    struct CacheEntry { size_t key = 0; std::string sql; };
    std::array<CacheEntry, CACHE_SIZE> entries;
    size_t next_slot = 0;
};
thread_local YourStatementSQLCache your_cache;

static auto get_cached_sql(size_t key) -> std::string {
    // 1. Search cache, 2. Generate with pre-allocation, 3. Store round-robin
}
```

## SQL Generation Analysis

```bash
./sql_generation_analysis.sh  # Detailed analysis with color-coded results
```

**Features**:
- Batch size performance
- Cache hit/miss visualization
- 100-iteration tests with avg/min/max timing
- Optimization impact summary

**Use cases**:
- Validate cache performance
- Test batch optimizations
- Debug bottlenecks
- Regression testing

## Adding PostgreSQL Support

1. Create `src/db/postgresql.cppm` implementing concepts
2. Add PostgreSQL-specific statement implementations
3. Update `ConnectionManager` for multiple backends
4. Ensure concepts properly abstract differences

## Performance Checklist

1. ✅ **Compile-time generation**: Move computation to compile-time
2. ✅ **Memory pre-allocation**: Calculate exact sizes upfront
3. ✅ **Cache common patterns**: Thread-local caches
4. ✅ **Batch thresholds**: Optimize batch sizes
5. ✅ **Index sequences**: Use fold expressions
6. ✅ **Measurement**: Always validate with realistic data

## Expected Performance (10,000 rows, Release build)

- Single INSERT: ~992K/sec (2.0x vs sqlite_orm)
- Single DELETE: ~21.6M/sec (73% of raw SQLite, 36x vs sqlite_orm)
- SELECT: ~13.07M rows/sec (1.51x vs sqlite_orm, 74% of raw SQLite)
- Batch INSERT: ~2.7M/sec, Batch DELETE: ~3.9M/sec
- JOIN: 4-6M rows/sec (77% average efficiency vs raw SQLite)
- Cache hit rate: >90% for common sizes (1, 10, 25, 50)
