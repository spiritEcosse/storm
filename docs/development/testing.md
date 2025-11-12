# Testing Strategy

## Test Framework

- **GoogleTest** with C++26 module support
- Tests located in `tests/` directory
- In-memory database (`:memory:`) for fast execution
- Comprehensive sanitizer support (ASAN, TSAN, LSAN)

## Test Categories

### Unit Tests

**Location**: `tests/test_*.cpp`

**Coverage**:
- **ID Validation**: Tests verify returned auto-generated IDs
- **SELECT Testing**: Empty table, single/multiple rows, field types, large datasets, statement caching, integration tests
- **JOIN Testing**: Single FK and multi-FK JOINs with full object population verification (`tests/test_fk_fields.cpp`)
- **FK Field Testing**: INSERT/UPDATE/DELETE with FK fields, batch operations with FKs
- **WHERE Clause Testing**: Various conditions, operators, LIKE patterns, IN, BETWEEN
- **DISTINCT Testing**: Single and multi-field operations with type safety validation

### Performance Benchmarks

**Location**: `benchmarks/bench_*.cpp`

**Coverage**:
- CRUD operations (INSERT, SELECT, UPDATE, DELETE)
- JOIN operations (INNER, LEFT, RIGHT)
- DISTINCT queries
- Batch operations

See [BENCHMARKS.md](../../BENCHMARKS.md) for detailed benchmark documentation.

## Running Tests

```bash
# Run all tests
ctest --test-dir build/debug --output-on-failure

# Run specific test suite
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"

# Run with verbose output
./build/debug/tests/storm_tests --gtest_verbose

# Run with sanitizers
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug
ctest --test-dir build/debug
```

## Test Structure

```cpp
#include <gtest/gtest.h>
import storm;

TEST(YourTestSuite, YourTestCase) {
    // Setup: Create connection and table
    auto conn = storm::db::sqlite::Connection::create(":memory:");
    conn->execute("CREATE TABLE ...");

    // Execute: Perform operation
    QuerySet<YourModel> qs(conn);
    auto result = qs.insert(...);

    // Verify: Check results
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), expected_id);
}
```

## Test Coverage Goals

- ✅ All public APIs
- ✅ Edge cases (empty tables, null values, large datasets)
- ✅ Error conditions (invalid operations, constraint violations)
- ✅ Performance regression prevention
- ✅ Thread safety (where applicable)

## Current Test Statistics

- **104 unit tests** (100% passing)
- Test execution time: ~0.5 seconds
- Coverage areas:
  - FK field operations (17 tests)
  - SELECT operations (12 tests)
  - CRUD operations (44 tests)
  - Type support (14 tests)
  - WHERE clauses (24 tests)

## Writing New Tests

1. **Create test file**: `tests/test_<feature>.cpp`
2. **Include dependencies**:
   ```cpp
   #include <gtest/gtest.h>
   import storm;
   ```
3. **Follow AAA pattern**: Arrange, Act, Assert
4. **Test edge cases**: Empty input, null values, boundary conditions
5. **Verify error handling**: Check `std::expected` error cases
6. **Add performance tests**: If feature impacts performance

## Test Maintenance

- Run tests before committing: `ctest --test-dir build/debug`
- Update tests when changing APIs
- Add regression tests for fixed bugs
- Keep tests fast (use in-memory database)
- Document test intent with clear names
