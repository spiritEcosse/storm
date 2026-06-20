# Adding New Features

Guide for adding new database operations or features to Storm ORM.

## Feature Implementation Checklist

- [ ] Implement feature in `src/orm/statements/`
- [ ] Add comprehensive tests in `tests/test_<feature>.cpp`
- [ ] Create performance benchmark in `benchmarks/bench_<feature>.cpp`
- [ ] Run benchmark and measure efficiency vs raw SQLite
- [ ] If efficiency <95%, optimize or document reasons
- [ ] Update relevant documentation
- [ ] Document any DRY/KISS tradeoffs made for performance
- [ ] Commit with performance metrics in message

## Adding a New Database Operation

### 1. Create Statement Class

```cpp
// src/orm/statements/your_operation.cppm
export module storm_orm_statements_your_operation;

import storm_orm_statements_base;
import storm_orm_utilities;

export namespace storm::orm {

template <typename T>
class YourOperationStatement : private BaseStatement<T> {
    using Base = BaseStatement<T>;

    // Compile-time SQL generation
    static consteval auto build_sql_array() {
        ConstexprString<sql_size> result;
        result.append("YOUR SQL HERE ");
        result.append(Base::table_name_);
        return result;
    }

    static constexpr auto sql_array = build_sql_array();
    static inline const std::string sql_string =
        std::string{sql_array.data.data(), sql_array.len};

public:
    // Single operation
    auto execute_single(const T& obj) -> std::expected<void, Error> {
        // Implementation
    }

    // Batch operation
    auto execute_batch(std::span<const T> objects) -> std::expected<void, Error> {
        // Implementation
    }
};

} // namespace storm::orm
```

### 2. Statement Reuse via the Connection-Level Cache

Storm uses a single Connection-level prepared-statement cache (`prepare_cached`),
keyed by SQL text. There is no per-QuerySet or per-Statement cache — those L1/L2
layers were removed in #214. Each call simply asks the connection for the
statement:

```cpp
template <typename T>
class YourOperationStatement : private BaseStatement<T> {
    auto execute_single_optimized(const T& obj) -> std::expected<void, Error> {
        auto stmt_result = conn_->prepare_cached(sql_string);
        if (!stmt_result) {
            return std::unexpected(stmt_result.error());
        }
        auto* stmt_ptr = *stmt_result;
        stmt_ptr->reset();
        // Bind and execute using stmt_ptr
    }
};
```

### 3. Add to QuerySet

```cpp
// src/orm/queryset.cppm
template <class T> class QuerySet {
public:
    auto your_operation(const T& obj) {
        // Statement is a cheap per-call object; the Connection-level
        // prepare_cached cache (keyed by SQL text) holds the compiled
        // statement, so there is no per-QuerySet cached member.
        return YourOperationStatement<T, ConnType>{conn_}.execute_single_optimized(obj);
    }
};
```

### 4. Choose Return Type

- **INSERT**: `std::expected<int64_t, Error>` or `std::expected<std::vector<int64_t>, Error>`
- **UPDATE/DELETE**: `std::expected<void, Error>`
- **SELECT**: `std::expected<std::vector<T>, Error>`

### 5. Add Tests

```cpp
// tests/test_your_feature.cpp
#include <gtest/gtest.h>

TEST(YourFeatureTest, BasicOperation) {
    // Setup
    auto conn = create_connection();
    QuerySet<TestStruct> qs(conn);

    // Test
    auto result = qs.your_operation(obj);

    // Verify
    ASSERT_TRUE(result.has_value());
}

TEST(YourFeatureTest, BatchOperation) {
    // Test batch variant
}
```

### 6. Create Benchmark

```cpp
// benchmarks/bench_your_feature.cpp
#include "common.h"

void benchmark_storm() {
    QuerySet<Person> qs(conn);
    for (int i = 0; i < iterations; ++i) {
        qs.your_operation(obj);
    }
}

void benchmark_raw_sqlite() {
    // Raw SQLite baseline
}

int main() {
    measure("Storm ORM", benchmark_storm);
    measure("Raw SQLite", benchmark_raw_sqlite);
    calculate_efficiency();
}
```

### 7. Run Performance Tests

```bash
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_your_feature --size=10000
```

### 8. Update Documentation

Add to relevant docs:
- Feature documentation in `docs/features/`
- Performance table in `docs/README.md`
- Architecture notes in `docs/architecture/` if novel approach

### 9. Commit

```bash
git add .
git commit -m "feat: add YOUR_FEATURE support (XX% of raw SQLite)"
```

## Optimization Techniques

### Compile-Time Generation

Move as much work as possible to compile-time:

```cpp
static consteval auto calculate_sql_size() {
    return table_name_size + field_names_size + 50;  // + buffer
}

static constexpr auto sql_array = build_sql_array();
```

### Type Dispatch

Use `if constexpr` for zero runtime overhead:

```cpp
if constexpr (std::is_same_v<FieldType, int>) {
    stmt.bind_int(idx, value);
} else if constexpr (std::is_same_v<FieldType, std::string>) {
    stmt.bind_text(idx, value);
}
```

### Pre-allocation

Calculate exact sizes upfront:

```cpp
std::vector<T> result;
result.reserve(expected_size);  // or resize(expected_size)
```

### Index Sequences

Use fold expressions for field iteration:

```cpp
template <size_t... Is>
auto bind_all(Statement& stmt, const T& obj, std::index_sequence<Is...>) {
    return (bind_field_at_index<Is>(stmt, obj, Is + 1) && ...);
}
```

## Common Patterns

### Transaction Wrapping

```cpp
auto execute_batch(std::span<const T> objects) -> std::expected<void, Error> {
    auto txn = TransactionGuard<ConnType>::begin(conn_);   // RAII; auto-ROLLBACK on early return
    if (!txn) return std::unexpected(txn.error());
    for (const auto& obj : objects) {
        if (auto r = execute_single(obj); !r) return r;    // guard rolls back on scope exit
    }
    return txn->commit();
}
```

### Error Handling

```cpp
auto execute() -> std::expected<void, Error> {
    if (auto result = stmt->execute(); !result) {
        return std::unexpected(Error{result.error()});
    }
    return {};
}
```

## See Also

- [Performance Testing](../performance/PERFORMANCE.md#testing--benchmarking) - Benchmarking guidelines
- [Compiler Issues](../compiler/COMPILER_ISSUES.md) - Known workarounds
- [Architecture Overview](../architecture/OVERVIEW.md) - System architecture
