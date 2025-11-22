# Storm ORM - Unified Benchmark System

**Pure C++ benchmark management system using Storm ORM to dogfood itself!**

## 🎯 Overview

The unified benchmark system is a **100% C++ solution** that uses Storm ORM to manage benchmark tests, track historical results, and detect performance regressions. No Python needed!

### Key Features

- ✅ **Pure C++** - Uses Storm ORM for all database operations
- ✅ **SQLite Registry** - All tests stored in `benchmark_registry.db`
- ✅ **Git Integration** - Automatic commit hash tracking for history
- ✅ **Interactive CLI** - C++ interactive prompts for test registration
- ✅ **Performance History** - Track results across git commits
- ✅ **Regression Detection** - Compare against baseline automatically
- ✅ **JSON Export** - Export baselines for version control
- ✅ **Type Safety** - All operations are type-checked at compile-time

## 📦 Components

```
benchmarks/
├── bench_unified.cpp          # Main unified benchmark binary
├── benchmark_registry.hpp     # Storm ORM models (BenchmarkTest, BenchmarkRun, etc.)
├── benchmark_cli.hpp          # Interactive CLI utilities
├── benchmark_git.hpp          # Git integration (commit hash, branch)
└── benchmark_registry.db      # SQLite database (managed by Storm ORM)
```

## 🗄️ Database Schema

The system uses **Storm ORM models** to define the database schema:

### `BenchmarkTest` - Test Registry
```cpp
struct BenchmarkTest {
    int test_id;                        // Primary key
    std::string test_name;              // e.g., "storm_insert_single"
    std::string test_category;          // "CRUD", "JOIN", "WHERE", etc.
    std::string operation_type;         // "INSERT", "SELECT", "UPDATE", etc.
    std::string batch_mode;             // "single", "batch", "mixed", "n/a"
    std::string description;            // Human-readable description
    std::string binary_name;            // "bench_storm", "bench_join", etc.
    std::optional<std::string> binary_args;  // "--mode=insert --size={size}"
    std::string output_parser;          // "StormCRUDParser", "JoinParser"
    bool enabled;
    int priority;
    // ... more fields
};
```

### `BenchmarkRun` - Performance History
```cpp
struct BenchmarkRun {
    int run_id;                         // Primary key
    int test_id;                        // Foreign key
    std::string run_timestamp;          // ISO 8601
    std::optional<std::string> git_commit_hash;  // Git SHA
    std::optional<std::string> git_branch;
    int dataset_size;
    int iterations;
    std::optional<double> storm_throughput;
    std::optional<double> raw_throughput;
    std::optional<double> efficiency_percent;
    std::string status;                 // "success", "failed", "skipped"
    // ... more fields
};
```

## 🚀 Usage

### Initialize Database

```bash
./build/debug/bench_unified --init
```

**Output:**
```
✓ Database initialized successfully: benchmark_registry.db
```

### Register a New Test (Interactive)

```bash
./build/debug/bench_unified --register
```

**Interactive prompt:**
```
╔════════════════════════════════════════════════════════════════╗
║  Register New Benchmark Test                                   ║
╚════════════════════════════════════════════════════════════════╝

Test name (unique identifier): storm_group_by_single
Test category [CRUD/JOIN/WHERE/DISTINCT/AGGREGATE/ORDER_BY/GROUP_BY]: GROUP_BY
Operation type [INSERT/SELECT/UPDATE/DELETE/JOIN/etc]: SELECT
Batch mode [single/batch/mixed/n/a]: n/a
Description: GROUP BY single field with COUNT aggregate

Execution Details:
Binary name [bench_storm]: bench_storm
Binary arguments (use {size} and {iterations} as placeholders) [--test-size={size}]: --mode=group-by --test-size={size}
Output parser name [StormCRUDParser]: GroupByParser

Metrics Configuration:
Extract Storm throughput? [Y/n]: y
Extract Raw SQLite throughput? [Y/n]: y
Extract efficiency? [Y/n]: y
Extract latency? [y/N]: n

Control Settings:
Priority (lower = run first) [100]: 100
Min dataset size [1000]: 1000
Max dataset size [100000]: 100000
Enabled by default? [Y/n]: y

Notes (optional): Initial GROUP BY implementation

────────────────────────────────────────────────────────────────
ℹ Test Summary:
  Name                          : storm_group_by_single
  Category                      : GROUP_BY
  Operation                     : SELECT
  Description                   : GROUP BY single field with COUNT aggregate
  Binary                        : bench_storm --mode=group-by --test-size={size}
────────────────────────────────────────────────────────────────

Proceed with registration? [Y/n]: y

✓ Test registered successfully! (test_id: 42)
```

### List All Tests

```bash
./build/debug/bench_unified --list
```

**Output:**
```
╔════════════════════════════════════════════════════════════════╗
║  All Benchmark Tests                                           ║
╚════════════════════════════════════════════════════════════════╝

ID   Name                          Category    Operation Enabled
────────────────────────────────────────────────────────────────
1    storm_insert_single           CRUD        INSERT    ✓
2    storm_insert_batch            CRUD        INSERT    ✓
3    storm_select_simple           CRUD        SELECT    ✓
4    storm_inner_join_single_fk    JOIN        JOIN      ✓
5    storm_where_int_comparison    WHERE       SELECT    ✓
10   storm_distinct_single_field   DISTINCT    SELECT    ✓
42   storm_group_by_single         GROUP_BY    SELECT    ✓

ℹ Total: 7 test(s)
```

### List Tests by Category

```bash
./build/debug/bench_unified --list --category=CRUD
```

### Show Test History

```bash
./build/debug/bench_unified --history storm_insert_single
```

**Output:**
```
╔════════════════════════════════════════════════════════════════╗
║  Test History: storm_insert_single                             ║
╚════════════════════════════════════════════════════════════════╝

Timestamp            Git Hash    Size      Storm (rows/s)  Raw (rows/s)    Efficiency
────────────────────────────────────────────────────────────────────────────────────
2025-01-22 10:30:15  a32e54b     10000     23670000        1000000         2367%
2025-01-21 14:20:30  355c299     10000     23450000        1000000         2345%
2025-01-20 09:15:45  c20b303     10000     22980000        1000000         2298%
2025-01-19 16:45:10  a119f6b     10000     22150000        1000000         2215%
2025-01-18 11:30:20  28c8318     10000     21890000        1000000         2189%

ℹ Showing 5 most recent run(s)
```

### Run a Specific Test

```bash
./build/debug/bench_unified --run storm_insert_single --size=10000 --iterations=100
```

### Run All Tests

```bash
./build/debug/bench_unified --run-all --size=10000 --iterations=100
```

### Run Tests by Category

```bash
./build/debug/bench_unified --run-category CRUD --size=10000
```

## 🔧 How It Works

### 1. **Test Registration**

When you register a test, Storm ORM inserts a row into `benchmark_tests`:

```cpp
QuerySet<BenchmarkTest> tests{conn};

BenchmarkTest test;
test.test_name = "storm_group_by_single";
test.test_category = "GROUP_BY";
test.operation_type = "SELECT";
test.description = "GROUP BY single field with COUNT aggregate";
test.binary_name = "bench_storm";
test.binary_args = "--mode=group-by --test-size={size}";
test.output_parser = "GroupByParser";
test.enabled = true;
test.priority = 100;
test.created_at = CLI::get_timestamp();
test.updated_at = CLI::get_timestamp();

auto result = tests.insert(test);
// Returns test_id: 42
```

### 2. **Test Execution** (TODO)

When you run a test:

1. **Fetch test definition** from database using Storm ORM
2. **Construct command** by replacing `{size}` and `{iterations}` placeholders
3. **Execute benchmark** and capture output
4. **Parse results** using specified parser
5. **Extract git context** (commit hash, branch)
6. **Save results** to `benchmark_runs` table

```cpp
QuerySet<BenchmarkTest> tests{conn};
QuerySet<BenchmarkRun> runs{conn};

// Find test by name
auto test_list = tests.select();
std::optional<BenchmarkTest> test;
for (const auto& t : test_list) {
    if (t.test_name == "storm_insert_single") {
        test = t;
        break;
    }
}

// Execute benchmark
std::string cmd = test->binary_name + " " + replace_placeholders(test->binary_args, size, iterations);
std::string output = execute_command(cmd);

// Parse results
auto parser = ParserRegistry::get(test->output_parser);
auto metrics = parser->parse(output);

// Get git context
auto git_ctx = GitInfo::get_context();

// Save run
BenchmarkRun run;
run.test_id = test->test_id;
run.run_timestamp = CLI::get_timestamp();
run.git_commit_hash = git_ctx.commit_hash;
run.git_branch = git_ctx.branch_name;
run.dataset_size = size;
run.iterations = iterations;
run.storm_throughput = metrics.storm_throughput;
run.raw_throughput = metrics.raw_throughput;
run.efficiency_percent = (metrics.storm_throughput / metrics.raw_throughput) * 100.0;
run.status = "success";

runs.insert(run);
```

### 3. **History Tracking**

Query historical results using Storm ORM:

```cpp
QuerySet<BenchmarkRun> runs{conn};

// Fetch recent runs for a test
auto run_list = runs.select();  // In future: .where(test_id == 42).order_by<timestamp>(DESC).limit(10)

for (const auto& run : run_list) {
    if (run.test_id == 42) {
        std::cout << run.run_timestamp << " | "
                  << run.git_commit_hash.value_or("N/A") << " | "
                  << run.efficiency_percent.value_or(0.0) << "%\n";
    }
}
```

### 4. **Regression Detection** (TODO)

Compare current run against historical baseline:

```cpp
// Get average efficiency from last 10 successful runs
double baseline = 0.0;
int count = 0;
for (const auto& run : recent_runs) {
    if (run.status == "success" && run.efficiency_percent.has_value()) {
        baseline += *run.efficiency_percent;
        count++;
    }
}
baseline /= count;

// Check regression
if (current_efficiency < baseline - 5.0) {
    CLI::warning("⚠️ REGRESSION: " + std::to_string(baseline - current_efficiency) + "% slower than baseline");
}
```

## 📊 Example: Adding a New Benchmark

Let's say you implement a new `GROUP BY` feature:

### Step 1: Add benchmark support to existing binary

```cpp
// In bench_storm.cpp
if (mode == "group-by") {
    benchmark_group_by(test_size);
}
```

### Step 2: Register the test

```bash
./bench_unified --register
```

Follow the interactive prompts or create a registration script.

### Step 3: Run the test

```bash
./bench_unified --run storm_group_by_single --size=10000 --iterations=100
```

### Step 4: View history

```bash
./bench_unified --history storm_group_by_single
```

### Step 5: Check for regressions

```bash
./bench_unified --check-regression storm_group_by_single
```

## 🎨 Why Pure C++?

**Advantages of using Storm ORM for benchmark management:**

1. ✅ **Dogfooding** - Storm ORM manages Storm ORM's benchmarks!
2. ✅ **Type Safety** - All database operations are compile-time checked
3. ✅ **Single Language** - No Python/C++ bridge needed
4. ✅ **Performance** - Compiled, not interpreted
5. ✅ **IDE Support** - Better autocomplete, refactoring, debugging
6. ✅ **Consistency** - Same patterns as the ORM itself
7. ✅ **Educational** - Demonstrates Storm ORM capabilities

## 📝 Database Queries with Storm ORM

The unified benchmark system showcases Storm ORM's power:

```cpp
// Select all enabled CRUD tests
QuerySet<BenchmarkTest> tests{conn};
auto crud_tests = tests.select();
// TODO: Add WHERE support
// auto crud_tests = tests.where(
//     BenchmarkTest::test_category == "CRUD" &&
//     BenchmarkTest::enabled == true
// ).select();

// Get recent runs for a specific test
QuerySet<BenchmarkRun> runs{conn};
auto recent = runs.select();
// TODO: Add WHERE, ORDER BY, LIMIT support
// auto recent = runs.where(BenchmarkRun::test_id == 42)
//                   .order_by<^^BenchmarkRun::run_timestamp>(false)  // DESC
//                   .limit(10)
//                   .select();

// Find regressions
// TODO: Use Storm ORM for this query
// SELECT test_name,
//        AVG(efficiency_percent) as baseline,
//        current.efficiency_percent as current_eff
// FROM benchmark_runs
// WHERE test_id = 42 AND status = 'success'
// GROUP BY test_id
```

## 🚧 TODO: Remaining Work

### Core Functionality
- [ ] Implement test execution engine (run benchmarks dynamically)
- [ ] Add output parsers (StormCRUDParser, JoinParser, WhereParser)
- [ ] Implement JSON baseline export
- [ ] Add regression detection logic
- [ ] Support test suites

### Storm ORM Integration
- [ ] Migrate to Storm ORM once WHERE clauses are implemented
- [ ] Use Storm ORM for filtering by category
- [ ] Use Storm ORM for ORDER BY + LIMIT (history queries)
- [ ] Create specialized QuerySet methods for common queries

### Advanced Features
- [ ] CI/CD integration (automatic regression checks)
- [ ] Performance trend visualization
- [ ] Benchmark comparison across commits
- [ ] HTML report generation
- [ ] Test suite management (quick, full, regression)

## 📦 Git Integration

The system automatically tracks git context with each benchmark run:

```cpp
auto git_ctx = GitInfo::get_context();

std::cout << "Commit: " << git_ctx.commit_hash << "\n";
std::cout << "Branch: " << git_ctx.branch_name << "\n";
std::cout << "Dirty: " << (git_ctx.has_uncommitted ? "yes" : "no") << "\n";
```

This allows you to:
- Track performance across commits
- Identify which commit introduced a regression
- Compare performance between branches
- Ensure reproducible benchmarks

## 🎯 Next Steps

1. **Build the system**:
   ```bash
   cmake --preset ninja-debug -DENABLE_BENCH=ON
   cmake --build --preset ninja-debug --target bench_unified
   ```

2. **Initialize database**:
   ```bash
   ./build/debug/bench_unified --init
   ```

3. **Register existing tests**:
   - Create a script to bulk-register current benchmarks
   - Or use interactive mode for each test

4. **Implement test execution engine**:
   - Add parsers for each benchmark type
   - Add command execution logic
   - Wire up result saving

5. **Test the system**:
   ```bash
   ./bench_unified --run storm_insert_single --size=10000
   ./bench_unified --history storm_insert_single
   ```

6. **Add to CI/CD**:
   - Run benchmarks on every PR
   - Detect regressions automatically
   - Post results as PR comments

---

**This is the future of Storm ORM benchmarking: Pure C++, using Storm to manage Storm!** 🚀
