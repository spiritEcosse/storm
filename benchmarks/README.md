# Storm ORM - Compile-Time Benchmark System

**Pure C++ benchmark system using compile-time JSON parsing and template metaprogramming!**

## 🎯 Overview

The unified benchmark system is a **100% compile-time C++ solution** that loads benchmark tests from JSON at compile time using C++26 `#embed` and executes them with zero runtime dispatch overhead through template metaprogramming.

### Key Features

- ✅ **Pure C++26** - Uses `#embed` for compile-time JSON parsing
- ✅ **Zero Runtime Overhead** - All test dispatch happens at compile time
- ✅ **Template Metaprogramming** - Each test gets its own specialized function
- ✅ **Type Safety** - Field names and operators resolved at compile time
- ✅ **Automatic Unrolling** - Template recursion unrolls test execution loop
- ✅ **JSON Configuration** - Easy to add new tests via `benchmark_tests.json`
- ✅ **Compile-Time Field Dispatch** - Uses reflection to map field names to struct members
- ✅ **Accurate Timing** - Uses `std::chrono::steady_clock` (monotonic, nanosecond precision)
- ✅ **Colorized Output** - Performance-based color coding for easy analysis
- ✅ **Runtime Filtering** - Filter tests by name without rebuilding
- ✅ **Raw SQLite Comparison** - Side-by-side Storm ORM vs Raw SQLite benchmarks
- ✅ **Efficiency Metrics** - Automatic calculation of Storm ORM efficiency percentage
- ✅ **Batch INSERT Support** - Benchmark single and batch insert operations (10, 100, 500, 1000 rows)
- [ ] **Lower priority Parallel** - Run multiple benchmarks in parallel
- [ ] **High priority Add aggregate** - Add aggregate benchmarks, add raw version, add accoringly in benchmark_tests.json
- [ ] **High priority Add join** - Add join benchmarks, add raw version, add accoringly in benchmark_tests.json
- [ ] **High priority Add order by** - Add ORDER BY benchmarks, add raw version, add accoringly in benchmark_tests.json
- [ ] **High priority Add limit** - Add LIMIT benchmarks, add raw version, add accoringly in benchmark_tests.json
- [ ] **High priority Add offset** - Add OFFSET benchmarks, add raw version, add accoringly in benchmark_tests.json
- [ ] **High priority Change dataset_size to init_dataset_size**
- [ ] **Lower priority Add distinct bench** - Add distinct benches with support aggregate, join, order by, limit, offset as well as raw version, add accoringly in benchmark_tests.json # Its a big one
- [ ] **Lets to change test_category** - Select, Insert, Update, Delete and operation to have test_category is multi or single and for select operation: aggregate, join, order by, limit, offset
- [ ] **lets check commit 7d38854b5a5621322893b9af7e33586e8b9f4be6** - Add according benchmarks in benchmark_tests.json by that commit

## 📦 Components

```
benchmarks/
├── main.cpp                    # Main benchmark executable
├── runner.hpp                  # BenchmarkRunner with template recursion
├── parser.hpp                  # Compile-time JSON parser using #embed
├── schema.hpp                  # Benchmark test schema (C++ structs)
├── operations/
│   ├── select.hpp             # WHERE clause benchmark implementation
│   └── insert.hpp             # INSERT benchmark implementation
└── tests/
    └── benchmark_tests.json   # Test definitions (loaded at compile time)
```

## 🚀 Usage

### Build the Benchmark System

**⚠️ CRITICAL: ONLY use Release builds for benchmarks!**

```bash
# Release build (MANDATORY for accurate performance measurements)
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
```

**Why Release-Only:**
- **Debug builds are 10-100x slower** - Results are meaningless
- **No `-O3` optimization** - No inlining, loop unrolling, or dead code elimination
- **Cannot detect real-world performance** - All measurements will be wrong
- **Benchmark comparisons invalid** - Cannot compare with other systems

**DO NOT use debug builds for benchmarking!**

### Show Help

```bash
./build/release/benchmarks/storm_bench --help
```

**Output:**
```
Storm ORM Benchmark System

Usage: ./build/release/benchmarks/storm_bench [options]

Options:
  --filter=<pattern>      Run only tests matching pattern (substring match)
  --iterations=<n>        Number of iterations per test (default: 1000)
  --list, -l              List all available tests
  --help, -h              Show this help message

Examples:
  ./build/release/benchmarks/storm_bench --filter=insert_single
  ./build/release/benchmarks/storm_bench --filter=where_int
  ./build/release/benchmarks/storm_bench --iterations=5000
  ./build/release/benchmarks/storm_bench --list
```

### List Available Tests

```bash
./build/release/benchmarks/storm_bench --list
```

**Output:**
```
=== Available Benchmark Tests ===
Total: 5 tests

Test Name                      Category         Operation
────────────────────────────────────────────────────────────
where_int_comparison_gt        WHERE            where
where_bool_equality            WHERE            where
where_double_comparison        WHERE            where
where_int_less_than            WHERE            where
insert_single                  INSERT           insert
```

### Run All Benchmarks

```bash
# Run all tests defined in benchmark_tests.json
./build/release/benchmarks/storm_bench
```

**Output:**
```
Inserting test data...
✅ Inserted 10,000 test records
=== Running All Benchmark Tests (Compile-Time Dispatch) ===
Total tests: 5
Iterations per test: 1000
Using compile-time JSON parsing with nested C++ structs


=== where_int_comparison_gt ===
Field: age, Operator: >, Value: 30
Iterations: 1000
Total operations: 7800000
Duration: 2543274 μs
Throughput: 3.06691 M ops/sec
✅ Benchmark complete!

=== where_bool_equality ===
Field: is_active, Operator: ==, Value: 0
Iterations: 1000
Total operations: 5000000
Duration: 1687314 μs
Throughput: 2.96329 M ops/sec
✅ Benchmark complete!

...

✅ All tests completed with COMPILE-TIME dispatch!
✅ Zero runtime string parsing overhead!
✅ Each test has its own specialized function!
✅ Tests loaded from JSON at compile time!
```

### Run Batch INSERT Benchmarks

**✅ NEW FEATURE!** Benchmark batch insert operations with various batch sizes:

```bash
# Run all INSERT benchmarks (single + batch)
./build/release/benchmarks/storm_bench --filter="insert"

# Run only batch INSERT benchmarks
./build/release/benchmarks/storm_bench --filter="insert_batch"

# Run specific batch size
./build/release/benchmarks/storm_bench --filter="insert_batch_100"
```

**Available batch sizes:**
- `insert_single` - Single row inserts (1 row per operation)
- `insert_batch_10` - Batch inserts with 10 rows per batch
- `insert_batch_100` - Batch inserts with 100 rows per batch
- `insert_batch_500` - Batch inserts with 500 rows per batch
- `insert_batch_1000` - Batch inserts with 1000 rows per batch

**What's tested:**
- **Storm ORM**: Uses `QuerySet::insert(std::span<const T>)` with automatic transaction management
- **Raw SQLite**: Manual prepared statement binding + BEGIN/COMMIT transaction wrapping
- **Fair comparison**: Both versions use prepared statements and explicit transactions

### Run Benchmarks by Filter (Test Name)

**✅ IMPLEMENTED!** Filter tests by name using substring matching:

```bash
# Run only the insert_single test
./build/release/benchmarks/storm_bench --filter="insert_single"

# Run all tests with "where_int" in the name
./build/release/benchmarks/storm_bench --filter="where_int"
# Runs: where_int_comparison_gt, where_int_less_than

# Run all WHERE tests
./build/release/benchmarks/storm_bench --filter="where"
```

**Output (with colors and comparison!):**
```
Inserting test data...
✅ Inserted 10,000 test records
=== Running Filtered Benchmark Tests ===
Filter: "insert_single"
Iterations per test: 100
Using compile-time dispatch with runtime filtering


=== insert_single ===                    (bold cyan)
Operation: INSERT (single row)
Iterations: 100                           (yellow)

Storm ORM:                                 (bold)
  Operations: 100                          (yellow)
  Duration: 222.15 μs                      (magenta)
  Throughput: 0.45 M ops/sec               (yellow)

Raw SQLite:                                (bold)
  Operations: 100                          (yellow)
  Duration: 319.90 μs                      (magenta)
  Throughput: 0.31 M ops/sec               (yellow)

Efficiency: 144.0% (FASTER than raw SQLite)
            ^^^^^^  (bold green = ≥90%, green = 70-90%, yellow = 50-70%, red = <50%)
✅ Benchmark complete!                    (green)

✅ Filtered tests completed!
```

**Performance-Based Color Coding:**

**Throughput:**
- **Bold Green** (≥5M ops/sec): Excellent performance
- **Green** (1-5M ops/sec): Good performance
- **Yellow** (<1M ops/sec): Needs optimization

**Efficiency:**
- **Bold Green** (≥90%): Excellent - near parity with raw SQLite
- **Green** (70-90%): Good - acceptable overhead
- **Yellow** (50-70%): Moderate - needs investigation
- **Red** (<50%): Poor - significant overhead

### Custom Iterations

```bash
# Run with custom iteration count
./build/release/benchmarks/storm_bench --filter="insert_single" --iterations=5000
```

### Run Benchmarks by Category

**Current Status:** Specific category filtering is not yet implemented, but you can use `--filter` to match category names in test names:

```bash
# Filter WHERE tests (matches test names containing "where")
./build/release/benchmarks/storm_bench --filter="where"

# Filter INSERT tests (matches test names containing "insert")
./build/release/benchmarks/storm_bench --filter="insert"
```

## 🔧 How It Works

### 1. **Compile-Time JSON Parsing**

The system uses C++26 `#embed` to load JSON at compile time:

```cpp
// In parser.hpp
constexpr const char* BENCHMARK_JSON =
#embed "tests/benchmark_tests.json"
;

// Parse JSON into C++ structs at compile time
constexpr auto BENCHMARK_TESTS = parse_benchmark_tests();
```

### 2. **Template Metaprogramming Test Dispatch**

Each test is executed via template recursion with compile-time dispatch:

```cpp
template<typename Model, size_t TestIndex, size_t TotalTests>
struct TestExecutor {
    static void execute(BenchmarkRunner& runner, int iterations) {
        constexpr auto& test = BENCHMARK_TESTS[TestIndex];
        constexpr std::string_view operation = test.operation.view();

        // Dispatch based on operation type at COMPILE TIME
        if constexpr (operation == "where") {
            constexpr std::string_view field_name = test.where.field.view();
            constexpr auto field_info = dispatch_field<Model>(field_name);
            SelectBenchmark<Model, field_info, ...> bench(...);
            runner.run_benchmark(test.test_name.c_str(), bench, iterations);
        } else if constexpr (operation == "insert") {
            InsertBenchmark<Model> bench;
            runner.run_benchmark(test.test_name.c_str(), bench, iterations);
        }

        // Recurse to next test at compile time
        if constexpr (TestIndex + 1 < TotalTests) {
            TestExecutor<Model, TestIndex + 1, TotalTests>::execute(runner, iterations);
        }
    }
};
```

**What this achieves:**
- ✅ Zero runtime string parsing or dispatch
- ✅ Each test gets its own specialized template instantiation
- ✅ Compiler unrolls the loop completely
- ✅ All field names and operators resolved at compile time
- ✅ Maximum performance - no virtual dispatch, no function pointers

### 3. **Field Dispatch Using Reflection**

Field names are mapped to struct members at compile time using C++26 reflection:

```cpp
template<typename Model>
constexpr auto dispatch_field(std::string_view field_name) {
    constexpr auto members = members_of(^^Model);

    // Find matching field at compile time
    template for (constexpr auto member : members) {
        constexpr auto name = display_name_of(member);
        if (field_name == name) {
            return member;  // Return reflection info for field
        }
    }
}
```

## 📊 Adding New Benchmarks

### Step 1: Add Test Definition to JSON

Edit `benchmarks/tests/benchmark_tests.json`:

```json
{
  "test_name": "where_string_like",
  "test_category": "WHERE",
  "description": "WHERE with LIKE pattern (name LIKE 'Person%')",
  "model": "Person",
  "operation": "where",
  "where_field": "name",
  "where_op": "LIKE",
  "where_value_string": "Person%",
  "iterations": 1000,
  "dataset_size": 10000
}
```

### Step 2: Implement Operation (if needed)

If the operation type doesn't exist yet, create `benchmarks/operations/your_operation.hpp`:

```cpp
template<typename Model, /* template params */>
class YourOperationBenchmark {
public:
    void print_info() const {
        std::cout << "Operation: YOUR_OPERATION\n";
    }

    int execute(int iterations) {
        // Perform operation iterations times
        // Return total operations count
    }
};
```

### Step 3: Add Dispatch Case

Update `runner.hpp` to handle your operation:

```cpp
if constexpr (operation == "your_operation") {
    // Extract compile-time parameters from test JSON
    // Instantiate benchmark with template parameters
    // Run benchmark
}
```

### Step 4: Rebuild and Run

```bash
cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench
```

The new test will be automatically included and executed with compile-time dispatch!

## ⏱️ Accurate Timing

The benchmark system uses **`std::chrono::steady_clock`** for timing, which provides:

- **Monotonic timing**: Clock never goes backwards (immune to system time changes)
- **Nanosecond precision**: Accurate timing even for fast operations
- **Same as Google Benchmark**: Uses the same clock that Google Benchmark uses internally
- **Better than `high_resolution_clock`**: More reliable on many platforms

**Why not full Google Benchmark?**
- Module system compatibility issues with the reflection compiler
- We only need accurate timing, not the full benchmarking framework
- Simpler integration without external dependencies breaking the build
- Custom colored output and Storm-specific reporting

## 📊 Storm ORM vs Raw SQLite Comparison

Every benchmark runs **two versions** of the same operation:

1. **Storm ORM**: Using the full ORM abstraction layer
2. **Raw SQLite**: Direct SQLite API calls with manual object construction

This provides an **apples-to-apples comparison** to measure the actual overhead of the ORM layer.

### Fair Comparison Methodology

**Both versions do the same work:**
- **Prepare statement once** (outside loop - realistic production usage)
- Execute the same SQL query with statement reuse
- Bind the same parameters
- Extract all column values from result rows
- Construct C++ objects from the data

**Example - SELECT with WHERE:**
```cpp
// Storm ORM version (automatic statement caching)
for (int i = 0; i < iterations; i++) {
    auto results = qs.where(field<age>() > 30).select();
}

// Raw SQLite version (manual statement reuse)
auto stmt = conn->prepare("SELECT * FROM Person WHERE age > ?");
for (int i = 0; i < iterations; i++) {
    stmt.reset();  // Reset for reuse
    stmt.bind_int(1, 30);
    while (stmt.step()) {
        Person obj;
        obj.id = stmt.extract_int(0);
        obj.name = std::string(stmt.extract_text_view(1));
        // ... extract all fields
    }
}
```

**Key Point:** Both versions use **prepared statement caching** to provide a realistic comparison. This is how you should use SQLite in production code!

**What the efficiency % tells you:**
- **≥100%**: Storm ORM is **faster** (statement caching, optimizations)
- **90-99%**: Storm ORM has **minimal overhead** (excellent)
- **70-89%**: Storm ORM has **acceptable overhead** (good)
- **50-69%**: Storm ORM has **moderate overhead** (needs optimization)
- **<50%**: Storm ORM has **significant overhead** (investigation needed)

**Why Storm can be faster than raw SQLite:**
- **Statement caching**: Prepared statements reused across calls
- **SQL string caching**: Compile-time SQL generation, no runtime concatenation
- **Inline optimizations**: Compiler can fully inline ORM templates
- **Thread-local caching**: Per-thread statement pools eliminate contention

## 🎨 Why Compile-Time Dispatch?

**Advantages of compile-time JSON + template metaprogramming:**

1. ✅ **Zero Runtime Overhead** - No string parsing or dispatch at runtime
2. ✅ **Maximum Performance** - Each test is a fully specialized template function
3. ✅ **Type Safety** - All field names and operators checked at compile time
4. ✅ **Easy Configuration** - Add tests via JSON without changing C++ code
5. ✅ **Compiler Optimization** - Full inlining and optimization of each test
6. ✅ **Compile-Time Errors** - Invalid field names caught during compilation
7. ✅ **Self-Documenting** - JSON serves as both config and documentation
8. ✅ **Colorized Output** - Performance-based color coding for easy analysis

## 📝 JSON Schema

### Test Definition

```json
{
  "test_name": "unique_test_identifier",
  "test_category": "WHERE|INSERT|SELECT|JOIN|DISTINCT|AGGREGATE|ORDER_BY|GROUP_BY",
  "description": "Human-readable description",
  "model": "Person",
  "operation": "where|insert|select|join",
  "iterations": 1000,
  "dataset_size": 10000,
  "batch_size": 1,  // For batch operations (1 = single, >1 = batch)

  // For WHERE operations
  "where_field": "age",
  "where_op": ">|<|>=|<=|==|!=",
  "where_value_int": 30,
  "where_value_double": 50000.0,
  "where_value_bool": true,
  "where_value_string": "pattern"
}
```

**Parameters:**
- `batch_size` (optional, default: 1): Number of rows per batch operation
  - `1` = Single row operation
  - `10, 50, 100, 500, 1000` = Predefined batch sizes (compile-time optimized)
  - Other values default to 100

## ✅ Implemented Features

- [x] **Runtime filtering** by test name (substring matching)
- [x] **Command-line arguments** (--filter, --iterations, --list, --help)
- [x] **Test listing** (--list shows all available tests)
- [x] **Custom iterations** (--iterations=N)
- [x] **Help system** (--help)

## 🚧 Current Limitations

### Not Yet Implemented

- [ ] **Benchmark comparison** against raw SQLite
- [ ] **Performance history tracking** across commits
- [ ] **Regression detection**
- [ ] **JSON export** of results
- [ ] **Category-specific filtering** (dedicated --category flag)
- [ ] **Multi-field WHERE clauses** (AND/OR combinations)
- [ ] **JOIN operations**
- [ ] **DISTINCT operations**
- [ ] **Aggregate functions** (MIN, MAX, AVG, SUM, COUNT)
- [ ] **ORDER BY benchmarks**
- [ ] **GROUP BY benchmarks**

### Design Trade-offs

**Hybrid Dispatch Model (Current Implementation):**
- ✅ **Pro:** Maximum performance - compile-time operation dispatch
- ✅ **Pro:** Runtime flexibility - filter tests without rebuilding
- ✅ **Pro:** Best of both worlds achieved!
- ⚠️ **Trade-off:** Small runtime overhead for filtering (negligible - simple string match)
- ❌ **Con:** Longer compile times (every test instantiates templates)
- ❌ **Con:** Binary size increases with number of tests

**Why This Works:**
- Compile-time dispatch for operation types (where, insert, etc.) → zero overhead
- Runtime filtering on test names → minimal overhead (simple string check)
- All tests still get specialized template functions
- No virtual dispatch or function pointers

## 🎯 Future Enhancements

### Planned Features

1. **Enhanced Command-Line Interface:**
   ```bash
   storm_bench --category=WHERE        # Dedicated category filter
   storm_bench --output=json           # JSON output for CI/CD
   storm_bench --compare-baseline      # Compare against baseline
   ```

3. **Baseline Comparison:**
   - Compare against raw SQLite performance
   - Calculate efficiency percentage
   - Detect regressions

4. **Performance History:**
   - Track results across git commits
   - Visualize trends
   - Auto-detect regressions in CI

5. **Advanced Operations:**
   - Complex WHERE clauses (AND/OR/nested)
   - JOIN operations (INNER, LEFT, etc.)
   - DISTINCT queries
   - Aggregates (MIN, MAX, AVG, SUM, COUNT)
   - ORDER BY with multiple fields
   - GROUP BY with HAVING

## 🔍 Troubleshooting

### "No tests executed"

**Cause:** JSON file is empty or malformed.

**Solution:** Check `benchmarks/tests/benchmark_tests.json` is valid JSON and contains at least one test definition.

### Compilation error: "field not found"

**Cause:** JSON references a field name that doesn't exist in the model struct.

**Solution:** Ensure `where_field` matches an exact field name in the model (case-sensitive):

```cpp
struct Person {
    int id;
    std::string name;  // Use "name" in JSON, not "Name"
    int age;           // Use "age" in JSON
};
```

### Benchmark runs but shows 0 operations

**Cause:** Query returned no results or operation failed silently.

**Solution:** Check that test data was inserted correctly and WHERE conditions match some rows.

---

## 📚 Related Documentation

- **Main benchmark guide:** [../BENCHMARKS.md](../BENCHMARKS.md)
- **Performance guidelines:** [../docs/development/performance-guidelines.md](../docs/development/performance-guidelines.md)
- **Storm ORM architecture:** [../docs/architecture/](../docs/architecture/)

---

**This is the future of Storm ORM benchmarking: Pure C++26 with compile-time dispatch!** 🚀
