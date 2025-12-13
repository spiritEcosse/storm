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
- ✅ **Batch UPDATE Support** - Benchmark single and batch update-by-PK operations with CRTP base class
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
│   ├── base.hpp               # CRTP base class for data-driven benchmarks
│   ├── select.hpp             # WHERE clause benchmark implementation
│   ├── insert.hpp             # INSERT benchmark implementation
│   ├── update.hpp             # UPDATE by PK benchmark implementation
│   └── delete.hpp             # DELETE benchmark implementation
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

> **Note:** This is the new unified benchmark system. The old Python-based benchmark system (`bench.py`, `scripts/bench/`) has been deprecated. All benchmark functionality is now available through the `storm_bench` executable with compile-time dispatch and zero runtime overhead.

### Show Help

```bash
./build/release/benchmarks/storm_bench --help
```

**Output:**
```
Storm ORM Benchmark System

Usage: ./build/release/benchmarks/storm_bench [options]

Options:
  --filter=<pattern>      Run only tests with EXACT name match
  --scale-test            Test performance with increasing sizes (substring match)
  --iterations=<n>        Number of iterations per test (default: 1000)
  --disk                  Use disk-based database (default: in-memory)
  --db=<path>             Use specific database file path
  --list, -l              List all available tests
  --help, -h              Show this help message

Examples:
  ./build/release/benchmarks/storm_bench --filter=insert_batch_100                # Run only insert_batch_100
  ./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test       # Test degradation: 10,100,1000,10000...
  ./build/release/benchmarks/storm_bench --filter=where_int --scale-test          # Run all where_int_* variants
  ./build/release/benchmarks/storm_bench --iterations=5000
  ./build/release/benchmarks/storm_bench --disk                                   # Use disk-based database
  ./build/release/benchmarks/storm_bench --db=/tmp/bench.db                       # Use specific database file
  ./build/release/benchmarks/storm_bench --list
```

### List Available Tests

```bash
./build/release/benchmarks/storm_bench --list
```

**Output:**
```
=== Available Benchmark Tests ===
Total: 28 tests

Test Name                      Category         Operation
────────────────────────────────────────────────────────────
where_int_comparison_gt        WHERE            where
where_bool_equality            WHERE            where
where_double_comparison        WHERE            where
where_int_less_than            WHERE            where
insert_single                  INSERT           insert
insert_batch_10                INSERT           insert
insert_batch_100               INSERT           insert
...
update_pk_single               UPDATE_PK        update_pk
update_pk_batch_10             UPDATE_PK        update_pk
update_pk_batch_100            UPDATE_PK        update_pk
...
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
Total tests: 28
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
- **Storm ORM**: Uses `QuerySet::insert(std::span<const T>)` with automatic transaction management and chunked bulk SQL
- **Raw SQLite**: Manual prepared statement binding + BEGIN/COMMIT transaction wrapping using chunked bulk SQL
- **Fair comparison**: Both versions use the SAME chunked bulk SQL strategy for batches exceeding SQLite's 999-variable limit

### Batch INSERT Performance Characteristics

**Three INSERT Strategies (matching Storm ORM):**

Both Storm ORM and the Raw SQLite benchmark use **identical strategies** for fair comparison:

| Strategy | Batch Size | SQL Pattern | Transaction |
|----------|------------|-------------|-------------|
| **1. Single INSERT** | 1 row | `INSERT INTO ... VALUES (?, ?, ...)` | None |
| **2. Bulk VALUES** | 2-249 rows | `INSERT INTO ... VALUES (...), (...), ...` | None (single statement) |
| **3. Chunked Bulk** | 250+ rows | Multiple bulk INSERTs (249 rows each) | None (each chunk is atomic) |

**Threshold Calculation:**
- SQLite limit: 999 variables per statement
- Person model has 4 non-PK fields → max chunk size = 999 / 4 = **249 rows**
- Batches exceeding 249 rows split into multiple bulk INSERT statements
- Each chunk: `INSERT INTO ... VALUES (...), (...), ... (up to 249 rows)`
- **Note:** INSERT doesn't need transaction wrapping because each bulk INSERT is already atomic

**⚠️ Chunking Boundary Performance:**

Tests reveal a performance cliff at the chunking boundary:

| Batch Size | Chunks | Efficiency | Notes |
|------------|--------|------------|-------|
| 248 | 1 | **98.6%** | Just under boundary |
| 249 | 1 | **96.7%** | Exactly at boundary |
| 250 | 2 | **74.5%** | ⚠️ Just over boundary - 25% drop! |

This is expected behavior due to the overhead of preparing and executing multiple SQL statements when crossing the chunk boundary. Consider batch sizes that align with chunk boundaries for optimal performance.

**Single INSERT Performance (verified 2025-12-06, 20,000 iterations, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single INSERT** | 0.96-0.99 M/s | 0.99 M/s | **~97%** | ✅ **Only ~3% overhead** - excellent for full ORM abstraction! |

**Batch INSERT Performance (verified 2025-12-06, fair chunked bulk SQL comparison, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Chunks | Notes |
|------------|-----------|------------|------------|--------|-------|
| 10 | 2.93-3.10 M/s | 3.10 M/s | **~95%** | 1 | ✅ Near parity |
| 100 | 4.21-4.56 M/s | 4.21 M/s | **~108%** | 1 | ✅ Storm FASTER - SQL caching |
| **1000** | 4.22-4.52 M/s | 4.27-4.52 M/s | **99-100%** | 5 (249×4+4) | ✅ **Near parity - fair chunked bulk SQL** |
| 5000 | 4.24-4.29 M/s | 4.28-4.31 M/s | **98-100%** | 21 (249×20+5) | ✅ Near parity |
| 10000 | 4.21-4.44 M/s | 4.33-4.38 M/s | **97-101%** | 41 (249×40+10) | ✅ Near parity |
| 50000 | 4.03-4.35 M/s | 4.20-4.24 M/s | **96-103%** | 201 (249×200+50) | ✅ Near parity |
| 100000 | 4.28-4.35 M/s | 4.43-4.48 M/s | **96-98%** | 402 (249×401+151) | ✅ Near parity |

**🎯 What Fixed the Benchmark (2025-12-06 Update)**

**Critical Issue Identified and Resolved:**

**❌ UNFAIR COMPARISON** - Raw SQLite benchmark was NOT using the same strategy as Storm ORM for large batches!

**The Problem:**
- For `batch_1000`, Storm uses **chunked bulk SQL** (5 chunks of 249 rows each)
- Raw SQLite was using **1000 individual INSERT statements** with transaction
- This made Storm appear 3x faster (296% efficiency) - completely misleading!

**The Root Cause:**
```cpp
// In benchmarks/operations/insert.hpp (line 122)
if constexpr (BatchSize <= bulk_sweet_spot) {  // bulk_sweet_spot = 124
    // Use bulk SQL: INSERT INTO ... VALUES (...), (...), ...
} else {
    // WRONG: Was using individual INSERTs for batch_1000
    // Should use: CHUNKED bulk SQL like Storm!
}
```

**The Fix:**
1. ✅ **Implemented chunked bulk SQL** in raw SQLite benchmark for large batches (>124 rows)
2. ✅ **Added statement caching** - prepare statements ONCE, reuse across iterations
3. ✅ **Fair apples-to-apples comparison** - Both Storm and raw SQLite now use identical strategy

**Result:**
- **Before fix**: 296% efficiency (Storm 3x faster than raw SQLite) - UNFAIR
- **After fix**: 99-100% efficiency (near parity) - FAIR COMPARISON!

**Key Findings:**

1. **Single INSERT: ~97% efficiency (only ~3% overhead!)**
   - **Optimizations applied:**
     - `statement_cache_.reserve(32)` prevents rehashing → safe raw pointer caching
     - `[[unlikely]]` hints on error paths → better branch prediction
     - Simple imperative style → no lambda overhead
     - Direct pointer access → no hash lookup on every call
   - The remaining ~3% overhead comes from:
     - `std::expected` error handling
     - Additional function call layer (QuerySet → InsertStatement)
   - **This is excellent** for a full ORM providing type safety, automatic ID retrieval, and clean interface!

2. **Batch Operations: 96-108% efficiency (near parity!)**
   - After fixing the unfair comparison, Storm ORM achieves **96-108% efficiency** across all batch sizes
   - The overhead is amortized across batch operations, making it nearly invisible

3. **Why batch_100 is fastest (~108% efficiency)?**
   - **SQL String Caching** - Thread-local cache avoids regenerating SQL every iteration
   - **Compile-Time SQL Generation** - Pre-computed SQL with optimized string handling
   - **Statement Preparation Optimization** - Connection-level caching reduces overhead

4. **Consistent 4.2-4.5 M ops/sec for large batches** - Performance plateaus around 4.3 M/s for batches ≥1000, likely hitting SQLite's internal bottleneck (disk I/O, B-tree operations, etc.)

5. **Storm's overhead is only ~3% for both single and batch operations**, demonstrating that the ORM abstraction layer is extremely efficient!

### Run Batch UPDATE Benchmarks

**✅ NEW FEATURE!** Benchmark update-by-primary-key operations with various batch sizes:

```bash
# Run all UPDATE benchmarks (single + batch)
./build/release/benchmarks/storm_bench --filter="update_pk" --scale-test

# Run only single UPDATE benchmark
./build/release/benchmarks/storm_bench --filter="update_pk_single"

# Run specific batch size
./build/release/benchmarks/storm_bench --filter="update_pk_batch_100"
```

**Available batch sizes:**
- `update_pk_single` - Single row updates (1 row per operation)
- `update_pk_batch_10` - Batch updates with 10 rows per batch
- `update_pk_batch_100` - Batch updates with 100 rows per batch
- `update_pk_batch_198` - Just under chunking boundary
- `update_pk_batch_199` - Exactly at chunking boundary
- `update_pk_batch_200` - Just over chunking boundary
- `update_pk_batch_500` to `update_pk_batch_100000` - Large batch sizes

**What's tested:**
- **Storm ORM**: Uses `QuerySet::update(obj)` for single and `QuerySet::update(span)` for batch with automatic transaction management
- **Raw SQLite**: Manual `UPDATE ... SET ... WHERE id=?` with transaction wrapping for batches
- **Fair comparison**: Both versions use the SAME strategy (individual UPDATEs within transaction for batches)

### Batch UPDATE Performance Characteristics

**Two UPDATE Strategies (matching Storm ORM):**

Both Storm ORM and the Raw SQLite benchmark use **identical strategies** for fair comparison:

| Strategy | Batch Size | SQL Pattern | Transaction |
|----------|------------|-------------|-------------|
| **1. Single UPDATE** | 1 row | `UPDATE ... SET ... WHERE id = ?` | None |
| **2. Batch UPDATE** | 2+ rows | Individual UPDATEs in loop | One transaction wrapping all |

**Note:** Unlike INSERT and DELETE, there's no multi-row UPDATE syntax in SQLite. All batch updates execute individual UPDATE statements within a single transaction for atomicity.

**Architecture: CRTP Base Class**

UPDATE and INSERT benchmarks share common code via CRTP (Curiously Recurring Template Pattern):

```cpp
// In operations/base.hpp
template<typename Derived, typename Model, int BatchSize, size_t FieldsPerRow>
class DataBenchmarkBase {
    // Shared: data_, qs_, create_model(), prepare(), bind utilities
};

// INSERT: 4 fields (name, age, is_active, salary - id is auto-increment)
class InsertBenchmark : public DataBenchmarkBase<..., 4> { ... };

// UPDATE: 5 fields (name, age, is_active, salary + id for WHERE clause)
class UpdateBenchmark : public DataBenchmarkBase<..., 5> { ... };
```

**Chunking Boundary for UPDATE:**
- UPDATE binds 5 parameters per row (4 data fields + 1 PK for WHERE clause)
- Max rows per transaction: 999 / 5 = **199 rows**
- Compare to INSERT: 999 / 4 = **249 rows**

**⚠️ Chunking Boundary Performance:**

| Batch Size | Efficiency | Notes |
|------------|------------|-------|
| 198 | **~90.6%** | Just under boundary |
| 199 | **~90.0%** | Exactly at max_bulk |
| 200 | **~91.3%** | Just over boundary |

**Note:** Unlike INSERT (which uses bulk SQL), UPDATE always executes individual statements within a transaction. The "chunking boundary" is less significant for UPDATE since there's no multi-row UPDATE syntax in SQLite.

**Single UPDATE Performance (verified 2025-12-13, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single UPDATE** | ~2.26 M/s | ~2.42 M/s | **~93%** | ✅ Excellent for full ORM! |

**Batch UPDATE Performance (verified 2025-12-13, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|------------|-----------|------------|------------|-------|
| 10 | ~3.47 M/s | ~3.38 M/s | **~103%** | ✅ Storm FASTER! |
| 100 | ~3.67 M/s | ~4.03 M/s | **~91%** | ✅ Good |
| 500 | ~3.44 M/s | ~3.83 M/s | **~90%** | ✅ Good |
| 1000 | ~3.37 M/s | ~3.72 M/s | **~91%** | ✅ Good |
| 5000 | ~3.30 M/s | ~3.66 M/s | **~90%** | ✅ Good |
| 10000 | ~3.33 M/s | ~3.67 M/s | **~91%** | ✅ Good |
| 50000 | ~3.09 M/s | ~3.40 M/s | **~91%** | ✅ Good |
| 100000 | ~3.05 M/s | ~3.30 M/s | **~92%** | ✅ Good |

**Key Optimizations Applied (2025-12-13 Update):**

1. **RAII TransactionGuard** - Clean transaction management with automatic rollback
2. **Statement Pointer Caching** - Cached `sqlite3_stmt*` avoids hash lookup per row
3. **Inline Binding** - `inline_bind_all_fields()` with compile-time type dispatch
4. **Zero-Overhead Abstraction** - TransactionGuard fully inlined by compiler
5. **CRTP Pattern** - Zero-overhead abstraction for shared functionality

**TransactionGuard Pattern (Python context manager style):**
```cpp
// ❌ OLD: Verbose flat code with manual rollback
auto begin_result = conn_->execute("BEGIN TRANSACTION");
if (!begin_result) return std::unexpected(begin_result.error());
for (const auto& obj : objects) {
    if (!bind_result) { (void)conn_->execute("ROLLBACK"); return ...; }
    if (!exec_result) { (void)conn_->execute("ROLLBACK"); return ...; }
}
auto commit_result = conn_->execute("COMMIT");
if (!commit_result) { (void)conn_->execute("ROLLBACK"); return ...; }

// ✅ NEW: Clean RAII style (zero overhead - fully inlined)
auto txn = TransactionGuard<ConnType>::begin(*conn_);
if (!txn) return std::unexpected(txn.error());
for (const auto& obj : objects) {
    if (!bind_result) return std::unexpected(bind_result.error());  // Auto-rollback
    if (!exec_result) return std::unexpected(exec_result.error());  // Auto-rollback
}
return txn->commit();
```

### Run Batch DELETE Benchmarks

**✅ NEW FEATURE!** Benchmark delete-by-primary-key operations with various batch sizes:

```bash
# Run all DELETE benchmarks (single + batch)
./build/release/benchmarks/storm_bench --filter="delete_pk" --scale-test

# Run only single DELETE benchmark
./build/release/benchmarks/storm_bench --filter="delete_pk_single"

# Run specific batch size
./build/release/benchmarks/storm_bench --filter="delete_pk_batch_100"
```

**Available batch sizes:**
- `delete_pk_single` - Single row deletes (1 row per operation)
- `delete_pk_batch_10` - Batch deletes with 10 rows per batch
- `delete_pk_batch_100` - Batch deletes with 100 rows per batch
- `delete_pk_batch_500` - Batch deletes with 500 rows per batch
- `delete_pk_batch_1000` to `delete_pk_batch_100000` - Large batch sizes

**What's tested:**
- **Storm ORM**: Uses `QuerySet::remove(span)` with automatic strategy selection (bulk IN clause vs individual deletes in transaction)
- **Raw SQLite**: Manual `DELETE FROM ... WHERE id=?` with transaction wrapping for batches
- **Fair comparison**: Both versions tested with same batch sizes

### Batch DELETE Performance Characteristics

**Three DELETE Strategies (matching Storm ORM):**

Both Storm ORM and the Raw SQLite benchmark use **identical strategies** for fair comparison:

| Strategy | Batch Size | SQL Pattern | Transaction |
|----------|------------|-------------|-------------|
| **1. Single DELETE** | 1 row | `DELETE FROM ... WHERE id = ?` | None |
| **2. Bulk IN Clause** | 2-799 rows | `DELETE FROM ... WHERE id IN (?, ?, ...)` | None |
| **3. Chunked IN Clause** | 800+ rows | Multiple `IN (?, ?, ...)` queries (799 each) | One transaction wrapping all chunks |

**Threshold Calculation:**
- SQLite limit: 999 variables per statement
- DELETE uses 1 variable per row (just the primary key)
- Safe threshold: 80% of 999 = **799 rows** per IN clause
- For batches > 799: Split into chunks of 799, wrapped in single transaction

**Example for 1000 rows:**
```sql
BEGIN TRANSACTION;
DELETE FROM Person WHERE id IN (?, ?, ..., ?);  -- 799 placeholders
DELETE FROM Person WHERE id IN (?, ?, ..., ?);  -- 201 placeholders
COMMIT;
```

**Single DELETE Performance (verified 2025-12-13, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single DELETE** | ~4.04 M/s | ~4.62 M/s | **~87.5%** | ✅ Good for full ORM! |

**Batch DELETE Performance (verified 2025-12-13, fair comparison, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Strategy | Notes |
|------------|-----------|------------|------------|----------|-------|
| 10 | ~1.40 M/s | ~1.43 M/s | **~97.7%** | IN clause | ✅ Near parity |
| 100 | ~2.15 M/s | ~2.19 M/s | **~98.1%** | IN clause | ✅ Near parity |
| 500 | ~3.29 M/s | ~3.30 M/s | **~99.5%** | IN clause | ✅ Near parity |
| 1000 | ~3.20 M/s | ~3.24 M/s | **~98.8%** | Chunked (2 queries) | ✅ Near parity |
| 5000 | ~3.11 M/s | ~3.13 M/s | **~99.3%** | Chunked (7 queries) | ✅ Near parity |
| 10000 | ~3.08 M/s | ~3.08 M/s | **~100.1%** | Chunked (13 queries) | ✅ Storm FASTER! |
| 50000 | ~2.84 M/s | ~2.87 M/s | **~99.1%** | Chunked (63 queries) | ✅ Near parity |
| 100000 | ~2.77 M/s | ~2.80 M/s | **~98.7%** | Chunked (126 queries) | ✅ Near parity |

**Key Optimizations Applied (2025-12-13 Update):**

1. **RAII TransactionGuard** - Clean transaction management with automatic rollback on early return
   - Similar to Python's `with transaction():` context manager
   - Zero overhead - fully inlined by compiler

2. **Chunked IN Clause for Large Batches** - Instead of 1000 individual DELETEs, uses chunked IN clauses
   - `DELETE WHERE id IN (?, ?, ...)` with up to 799 IDs per chunk

3. **Statement Caching** - Uses `prepare_cached()` for all DELETE statements
   - Prepared statements reused across iterations
   - SQL strings cached per chunk size via thread-local cache

4. **Fair Benchmark Comparison** - Both Storm and Raw SQLite use identical strategies
   - Same threshold calculations (799 rows = 80% of SQLite limit)
   - Same chunking logic for large batches
   - Pre-prepared BEGIN/COMMIT statements for raw SQLite

**Why Results Show ~98-100% Efficiency:**
- Both Storm and Raw SQLite use the **exact same strategy**
- Overhead comes from ORM abstraction layer (type dispatch, error handling)
- Statement caching eliminates SQL parsing overhead
- Chunked IN clause is optimal for SQLite's query planner

### Run Benchmarks by Filter (Test Name)

**✅ IMPLEMENTED!** Filter tests by name with exact or substring matching:

```bash
# Exact match (default) - run ONLY tests with exact name match
./build/release/benchmarks/storm_bench --filter=insert_batch_100
# Runs only: insert_batch_100

# Scale test mode - test performance degradation with increasing sizes
./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test
# Runs: insert_batch_10, insert_batch_100, insert_batch_500, insert_batch_1000, etc.

# Test specific progression
./build/release/benchmarks/storm_bench --filter=insert_batch_100 --scale-test
# Runs: insert_batch_100, insert_batch_1000, insert_batch_10000, insert_batch_100000

# Run all WHERE tests with "where_int" in the name
./build/release/benchmarks/storm_bench --filter=where_int --scale-test
# Runs: where_int_comparison_gt, where_int_less_than
```

**Filter Modes:**
- **Default (exact match)**: `--filter=<name>` runs only the test with that exact name
- **Scale test mode**: `--filter=<pattern> --scale-test` uses substring matching to run all tests containing the pattern
  - Perfect for testing performance degradation as batch sizes increase
  - Example: `--filter=insert_batch --scale-test` runs all batch insert benchmarks

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

**Current Status:** Specific category filtering is not yet implemented, but you can use `--filter` with `--scale-test` to match category names in test names:

```bash
# Filter WHERE tests (matches test names containing "where")
./build/release/benchmarks/storm_bench --filter=where --scale-test

# Filter INSERT tests (matches test names containing "insert")
./build/release/benchmarks/storm_bench --filter=insert --scale-test
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

## 📈 Statistical Analysis

Each benchmark runs **5 times** and reports statistical metrics:

- **Median**: Most stable metric, less affected by outliers
- **Mean**: Average throughput with standard deviation (±)
- **Range**: [min - max] shows variance between runs

**Example output:**
```
Storm ORM:
  Operations: 100
  Median:  4.21 M ops/sec
  Mean:    4.18 M ops/sec (±0.15)
  Range:   [4.01 - 4.35]

Raw SQLite:
  Operations: 100
  Median:  4.15 M ops/sec
  Mean:    4.12 M ops/sec (±0.12)
  Range:   [3.98 - 4.28]

Efficiency: 101.4% (FASTER than raw SQLite)
```

**Why 5 runs?**
- Enough samples for median/stddev calculation
- Catches variance from system noise
- Not too slow for development iteration

## 💾 In-Memory vs Disk Benchmarks

By default, benchmarks use **in-memory SQLite** (`:memory:`) because:

| Aspect | In-Memory (default) | Disk (`--disk`) |
|--------|---------------------|-----------------|
| **Speed** | Fastest | Slower (I/O bound) |
| **Reproducibility** | High (no disk variability) | Lower (depends on I/O) |
| **Isolation** | No leftover files | Creates temp file (auto-cleaned) |
| **Use case** | Development, CI/CD | Production-realistic testing |

**When to use `--disk`:**
- Testing real-world performance with disk I/O
- Benchmarking with fsync/journaling overhead
- Comparing WAL vs other journal modes

**Example:**
```bash
# In-memory benchmark (default, fast)
./build/release/benchmarks/storm_bench --filter=insert_single

# Disk-based benchmark (realistic I/O)
./build/release/benchmarks/storm_bench --filter=insert_single --disk

# Specific database file
./build/release/benchmarks/storm_bench --filter=insert_single --db=/tmp/bench.db
```

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

- **Main benchmark guide:** [README.md](README.md)
- **Performance guidelines:** [../docs/development/performance-guidelines.md](../docs/development/performance-guidelines.md)
- **Storm ORM architecture:** [../docs/architecture/](../docs/architecture/)

---

**This is the future of Storm ORM benchmarking: Pure C++26 with compile-time dispatch!** 🚀

## Profiling and Performance Debugging

### Using timing.hpp for Detailed Profiling

When you need to profile specific functions or code sections, use the `timing.hpp` header:

**1. Add timing to your code:**
```cpp
// In src/orm/statements/insert.cppm (or any file you want to profile)
#include "benchmarks/timing.hpp"  // Add at top

auto execute_bulk(std::span<const T> objects) noexcept -> ... {
    STORM_TRACE("execute_bulk");  // Times entire function
    
    const auto& sql = get_bulk_insert_sql(objects.size());
    STORM_TRACE("get_bulk_insert_sql");  // Times this call
    
    return conn_->prepare_cached(sql).and_then([...] {
        STORM_TRACE("prepare_cached");  // Times prepare
        // ...
    });
}
```

**2. Build with timing enabled:**
```bash
# Configure with timing flag
cd build/release
cmake ../.. -GNinja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DSTORM_ENABLE_TIMING_TRACE" \
    -DENABLE_BENCH=ON

# Build
ninja storm_bench

# Run benchmark - will show timing output
./benchmarks/storm_bench --filter=insert_batch_100 --iterations=100
```

**3. Example output:**
```
[TRACE] execute_bulk                        : 45.234 μs
[TRACE]   get_bulk_insert_sql               : 0.412 μs
[TRACE]   prepare_cached                    : 35.891 μs
[TRACE]   bind_objects                      : 6.543 μs
[TRACE]   execute                           : 2.103 μs
```

**4. Remove timing after profiling:**
```bash
# Remove timing includes and STORM_TRACE calls
# Rebuild without the flag for clean benchmarks
cmake ../.. -GNinja -DCMAKE_BUILD_TYPE=Release -DENABLE_BENCH=ON
ninja storm_bench
```

**Tips:**
- Use descriptive labels for STORM_TRACE
- Nest traces to understand call hierarchy
- Compare timings before/after optimizations
- Only enable timing when actively profiling (adds overhead)
- Remember to rebuild without timing for final benchmarks


### Choosing Between timing.hpp and timing_trace.hpp

There are two timing utilities available:

**timing.hpp** - Compile-time control (recommended for most cases)
- **Pros**: Zero overhead when disabled (macros compile to no-op)
- **Cons**: Requires rebuild to enable/disable
- **Use when**: You want minimal overhead and don't mind rebuilding

```cpp
#include "benchmarks/timing.hpp"

void my_function() {
    STORM_TRACE("my_function");  // Only active if -DSTORM_ENABLE_TIMING_TRACE
    // ...
}
```

**timing_trace.hpp** - Runtime control
- **Pros**: Can enable/disable without rebuilding
- **Cons**: Small overhead even when disabled (bool check)
- **Use when**: You want to toggle timing on/off during execution

```cpp
#include "benchmarks/timing_trace.hpp"

void my_function() {
    const bool ENABLE_TIMING_TRACE = true;  // Control at runtime
    STORM_TRACE_TIMER("my_function");
    // ...
}
```

**Recommendation**: Use `timing.hpp` for most profiling work. It has zero overhead when disabled and is simpler to use.

