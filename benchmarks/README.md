# Storm ORM - Compile-Time Benchmark System

**Pure C++ benchmark system using compile-time JSON parsing and template metaprogramming!**

## 🎯 Overview

The unified benchmark system is a **100% compile-time C++ solution** that loads benchmark tests from JSON at compile time using C++26 `#embed` and executes them with zero runtime dispatch overhead through template metaprogramming.

### Key Features

- ✅ **Pure C++26** - Uses `#embed` for compile-time JSON parsing
- ✅ **Google Benchmark backend** - median / mean / stddev / `BigO` / `RMS` come from gbench, not a custom runner
- ✅ **Template Metaprogramming** - Each test gets its own specialized fixture
- ✅ **Type Safety** - Field names and operators resolved at compile time
- ✅ **YAML Configuration** - Human-friendly test definitions in `benchmark_tests.yaml` (auto-converted to JSON at build time)
- ✅ **Compile-Time Field Dispatch** - Uses reflection to map field names to struct members
- ✅ **Runtime Filtering** - `--benchmark_filter=...` regex without rebuilding
- ✅ **Raw SQLite Anchors** - Sparse spot checks live in the separate `storm_anchors` binary (release-time only, not paired against per-Storm runs)
- ✅ **Batch INSERT/UPDATE/DELETE** - YAML profiles (`batch_standard`, `batch_*_edge`) drive size sweeps via gbench `Arg()`
- ✅ **Range / Complexity sweeps** - `dataset_standard` / `dataset_small` map to gbench `Range()` + `Complexity(oN)`

> **CLI**: This benchmark binary is a Google Benchmark executable. Use `--benchmark_filter`, `--benchmark_repetitions`, `--benchmark_min_time`, `--benchmark_format=json`, etc. The pre–#235 flags `--filter`, `--iterations`, `--scale-test`, `-c <category>` are gone — older sections of this document that still show them refer to the migrated equivalents `--benchmark_filter='Storm/<category>/.*'` and `--benchmark_repetitions=N`.

## 🚀 Quick Start

**When changing a module, run category benchmarks to verify performance:**

```bash
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/SELECT/.*'         # After changing SELECT module
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/DISTINCT/.*'       # After changing DISTINCT module
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/INSERT/.*'         # After changing INSERT module
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/AGGREGATE/.*'      # After changing AGGREGATE module
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/SELECT/.*' --benchmark_list_tests=true  # Preview which tests will run
```

### Open Issues

See [GitHub Issues (benchmarks)](https://github.com/spiritEcosse/storm/issues?q=is%3Aissue+is%3Aopen+label%3Abenchmarks) for planned improvements.

## 📉 Regression detection

The per-PR benchmark gate (#241) is self-hosted on GitHub Actions and uses **GitHub Actions artifacts** for the baseline store — no external service.

- **Push to `develop`** runs `storm_bench` and uploads the JSON as the `develop-baseline-latest` artifact (overwriting prior, 90-day retention).
- **Pull requests** run `storm_bench`, download `develop-baseline-latest` from the most recent successful develop run, diff via Google Benchmark's [`compare.py`](https://github.com/google/benchmark/blob/main/tools/compare.py) + Mann-Whitney U-test, post a marker-edited PR comment with the regression / improvement table, and **fail the gate on any benchmark slower than +5% with p<0.05**.
- **First-run case** (no `develop-baseline-latest` artifact yet): the PR comment notes the missing baseline and the gate marks green. The first push to `develop` after this lands seeds the baseline.

CI runs `benchmarks/scripts/compare_against_baseline.sh` — the same engine you can run locally against a saved JSON to get the same verdict before pushing. No committed baseline lives in the repo: a baseline is only meaningful on the same hardware class as the comparison run.

**Local workflow** (same engine, same numbers as CI)

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release

# 1. Run-only — produces current.json, no comparison.
./benchmarks/scripts/compare_against_baseline.sh

# 2. Save a snapshot before making changes, then diff after.
cp current.json /tmp/before.json
# ... edit code, rebuild ...
./benchmarks/scripts/compare_against_baseline.sh /tmp/before.json

# Narrow scope while iterating:
BENCH_FILTER='Storm/SELECT.*' \
    ./benchmarks/scripts/compare_against_baseline.sh /tmp/before.json

# Widen the regression threshold for noisy hardware:
REGRESSION_THRESHOLD=1.10 \
    ./benchmarks/scripts/compare_against_baseline.sh /tmp/before.json
```

Per-benchmark deltas are computed as `(current - baseline) / |baseline|`; the gate trips on `mean` or `median` aggregates only — `stddev`/`cv`/iteration-count rows are ignored. Significance gating uses the Mann-Whitney U-test at `p < UTEST_ALPHA` (default 0.05).

**Threshold knobs** (env vars, all optional):

| Var | Default | Effect |
|---|---|---|
| `REGRESSION_THRESHOLD` | `1.05` | Slowdown ratio. Accepts `1.05`, `0.05`, or `5%`. |
| `UTEST_ALPHA` | `0.05` | U-test significance bar. Set `0` to disable significance gating. |
| `BENCH_REPETITIONS` | `10` | Reps per benchmark. Lower for faster local turnaround. |
| `BENCH_FILTER` | (empty) | Google Benchmark `--benchmark_filter` regex. |
| `BENCH_MIN_TIME` | (empty) | Google Benchmark `--benchmark_min_time` (e.g. `0.5s`). |
| `PYTHON` | `python3` | Override e.g. `/path/to/venv/bin/python` if numpy/scipy aren't system-installed. |

`compare.py` requires `numpy` and `scipy` (used for the geometric mean and U-test respectively). Install via your distro's package manager (`pacman -S python-numpy python-scipy`, `apt install python3-numpy python3-scipy`, etc.) or a venv.

## 📦 Components

```
benchmarks/
├── main.cpp                    # Google Benchmark entry — owns <benchmark/benchmark.h>, no imports
├── register.cpp                # Storm-side bridge — `import storm_*` + builds the gbench registration table
├── bench_register.h            # POD-only handoff between main.cpp and register.cpp
├── benchmark_tests.hpp         # `#embed`-driven BENCHMARK_TESTS array (textual; consumed by register.cpp)
├── parser.cppm                 # Compile-time JSON parser (storm_benchmark_parser module)
├── schema.cppm                 # Benchmark test schema (storm_benchmark_schema module)
├── sizes.cppm                  # CRUD batch-size constexpr arrays (storm_benchmark_sizes module)
├── registry.cppm               # Compile-time model dispatch (storm_benchmark_registry module)
├── models.hpp                  # ORM model structs (textual — annotations need to be visible)
├── base.cppm                   # CRTP base for data-driven benchmarks (storm_benchmark_base module)
├── query_benchmark.cppm        # SELECT-family fixture (storm_benchmark_query module)
├── crud_benchmark.cppm         # INSERT/UPDATE/DELETE fixture (storm_benchmark_crud module)
├── anchors_raw.cpp             # `storm_anchors` binary — release-time raw SQLite spot checks
├── scripts/
│   ├── yaml_to_json.py             # YAML → JSON converter (runs at build time)
│   ├── compare_against_baseline.sh # Regression diff (Mann-Whitney U-test) — engine for local-dev
└── tests/
    ├── benchmark_tests.yaml   # Test definitions (human-friendly source of truth)
    └── benchmark_tests.json   # Auto-generated from YAML (loaded at compile time via #embed)
```

## 📐 Size Profiles

The benchmark system uses **size profiles** to automatically iterate over multiple sizes from a single JSON test definition. This reduces JSON redundancy from ~150 entries to ~60 entries while generating all the same benchmark variations at runtime.

### Available Profiles

| Profile | Sizes | Use Case |
|---------|-------|----------|
| `batch_standard` | 1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000 | INSERT/UPDATE/DELETE operations |
| `batch_insert_edge` | 248, 249, 250 | SQLite chunk boundary (999/4 fields) |
| `batch_update_edge` | 198, 199, 200 | SQLite chunk boundary (999/5 fields) |
| `dataset_standard` | 100, 1000, 10000, 100000 | SELECT/JOIN/DISTINCT operations |
| `dataset_small` | 10000, 50000 | Aggregate operations |

### How It Works

Instead of defining 9 separate entries for each INSERT batch size:

```json
// OLD: 9 separate entries
{ "test_name": "insert_single", "batch_size": 1, "iterations": 10000 }
{ "test_name": "insert_batch_10", "batch_size": 10, "iterations": 1000 }
{ "test_name": "insert_batch_100", "batch_size": 100, "iterations": 100 }
// ... 6 more entries
```

Define one entry with a size profile:

```json
// NEW: Single entry with size profile
{
  "test_name": "insert",
  "operation": "insert",
  "size_profile": "batch_standard"
}
```

The runner automatically generates: `insert_single`, `insert_10`, `insert_100`, `insert_500`, `insert_1000`, `insert_5000`, `insert_10000`, `insert_50000`, `insert_100000` with appropriate iterations.

### Naming Convention

- **Batch operations**: Uses `_single` for size=1, `_N` for other sizes (e.g., `insert_single`, `insert_100`)
- **Dataset operations**: Uses `_N` suffix (e.g., `select_100`, `select_1000`)

### Size Profile Definitions

Defined in `benchmarks/sizes.cppm` (`storm_benchmark_sizes` module):

```cpp
namespace storm::benchmark::sizes {
    inline constexpr std::array BATCH_STANDARD = {1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000};
    inline constexpr std::array BATCH_INSERT_EDGE = {248, 249, 250};
    inline constexpr std::array BATCH_UPDATE_EDGE = {198, 199, 200};
    inline constexpr std::array DATASET_STANDARD = {100, 1000, 10000, 100000};
    inline constexpr std::array DATASET_SMALL = {1000, 10000};

    constexpr int iterations_for_batch(int size);    // Returns iterations based on batch size
    constexpr int iterations_for_dataset(int size);  // Returns iterations based on dataset size
}
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
  -c, --category=<name>   Run tests matching category prefix (SELECT matches SELECT*)
  --scale-test            Test performance with increasing sizes (substring match)
  --iterations=<n>        Override iterations for all tests (default: use JSON values)
  --quick                 Quick validation mode (~3-5 min, 0.3x iterations)
  --thorough              Thorough regression mode (~15-20 min, 1.5x iterations)
  --disk                  Use disk-based database (default: in-memory)
  --db=<path>             Use specific database file path
  --list, -l              List all available tests (combine with -c to filter)
  --help, -h              Show this help message

Modes:
  (default)               Use JSON-defined iterations (~10 min for all tests)
  --quick                 0.3x iterations for fast development feedback
  --thorough              1.5x iterations for pre-commit validation

Examples:
  ./build/release/benchmarks/storm_bench --quick                          # Fast validation (~3-5 min)
  ./build/release/benchmarks/storm_bench --quick -c SELECT                # Quick SELECT tests only
  ./build/release/benchmarks/storm_bench --thorough                       # Thorough regression test
  ./build/release/benchmarks/storm_bench -c SELECT                        # Run SELECT* categories
  ./build/release/benchmarks/storm_bench -c SELECT --list                 # Preview SELECT tests
  ./build/release/benchmarks/storm_bench --filter=insert_batch_100        # Run only insert_batch_100
  ./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test  # Test all batch sizes
  ./build/release/benchmarks/storm_bench --iterations=5000                # Override all iterations
  ./build/release/benchmarks/storm_bench --list
```

### Benchmark Modes

The benchmark system supports three modes for different use cases:

| Mode | Flag | Runtime | Iterations | Use Case |
|------|------|---------|------------|----------|
| **Default** | (none) | ~10 min | Per-test from JSON | Standard benchmarking |
| **Quick** | `--quick` | ~3-5 min | 0.3x JSON values | Fast development validation |
| **Thorough** | `--thorough` | ~15-20 min | 1.5x JSON values | Pre-commit regression testing |

**Quick Mode** - Use during development for fast feedback:
```bash
./build/release/benchmarks/storm_bench --quick                 # All tests, ~3-5 min
./build/release/benchmarks/storm_bench --quick -c SELECT       # Just SELECT tests, ~30 sec
./build/release/benchmarks/storm_bench --quick --filter=insert_single  # Single test
```

**Thorough Mode** - Use before commits for comprehensive validation:
```bash
./build/release/benchmarks/storm_bench --thorough              # All tests, ~15-20 min
./build/release/benchmarks/storm_bench --thorough -c INSERT    # Thorough INSERT tests
```

**Mode Interactions:**
- `--iterations=N` always overrides mode multipliers
- `--quick` and `--thorough` are mutually exclusive (error if both specified)
- Without any mode flag, uses JSON-defined per-test iteration counts

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
# Run all tests defined in benchmark_tests.yaml (auto-converted to JSON at build time)
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

**Chunking Boundary Performance:**

Tests verify smooth performance across the chunking boundary (249 rows for 4-field models):

| Batch Size | Chunks | Efficiency | Notes |
|------------|--------|------------|-------|
| 248 | 1 | **~109%** | ✅ Storm FASTER |
| 249 | 1 | **~109%** | ✅ Storm FASTER |
| 250 | 2 | **~111%** | ✅ Storm FASTER - no cliff! |

SQL caching and compile-time generation eliminate the chunking overhead entirely. Storm maintains >100% efficiency across the boundary.

**Single INSERT Performance (verified 2026-02-06, 10,000 iterations, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single INSERT** | 0.70-0.73 M/s | 0.71-0.73 M/s | **~97%** | ✅ **Only ~3% overhead** - excellent for full ORM abstraction! |

**Batch INSERT Performance (verified 2026-02-06, fair chunked bulk SQL comparison, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Chunks | Notes |
|------------|-----------|------------|------------|--------|-------|
| 10 | 2.81-3.02 M/s | 2.93-3.14 M/s | **~98%** | 1 | ✅ Near parity |
| 100 | 4.78-5.48 M/s | 4.17-5.03 M/s | **~111%** | 1 | ✅ Storm FASTER - SQL caching |
| 500 | 5.16-6.17 M/s | 4.82-5.43 M/s | **~114%** | 3 (249×2+2) | ✅ Storm FASTER |
| **1000** | 4.84-6.24 M/s | 5.25-5.46 M/s | **~115%** | 5 (249×4+4) | ✅ **Storm FASTER - SQL caching** |
| 5000 | 6.03-6.39 M/s | 5.52-5.69 M/s | **~111%** | 21 (249×20+20) | ✅ Storm FASTER |
| 10000 | 5.55-6.48 M/s | 4.63-5.61 M/s | **~116%** | 41 (249×40+40) | ✅ Storm FASTER |
| 50000 | 6.09-6.28 M/s | 5.52-5.94 M/s | **~110%** | 201 (249×200+200) | ✅ Storm FASTER |
| 100000 | 6.04-6.15 M/s | 5.75-6.06 M/s | **~102%** | 402 (249×401+151) | ✅ Storm FASTER |

**🎯 What Fixed the Benchmark (2025-12-06 Update)**

**Critical Issue Identified and Resolved:**

**❌ UNFAIR COMPARISON** - Raw SQLite benchmark was NOT using the same strategy as Storm ORM for large batches!

**The Problem:**
- For `batch_1000`, Storm uses **chunked bulk SQL** (5 chunks of 249 rows each)
- Raw SQLite was using **1000 individual INSERT statements** with transaction
- This made Storm appear 3x faster (296% efficiency) - completely misleading!

**The Root Cause:** The old raw-SQLite benchmark switched to individual INSERTs for batches above the bulk sweet-spot (~124 rows), while Storm kept using chunked bulk SQL. The two paths weren't comparing the same algorithm.

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

2. **Batch Operations: 98-116% efficiency (Storm consistently FASTER!)**
   - Storm ORM achieves **98-116% efficiency** across all batch sizes
   - SQL caching and compile-time generation give Storm an edge over hand-written raw SQLite

3. **Why Storm beats raw SQLite for batches?**
   - **SQL String Caching** - Thread-local cache avoids regenerating SQL every iteration
   - **Compile-Time SQL Generation** - Pre-computed SQL with optimized string handling
   - **Statement Preparation Optimization** - Connection-level caching reduces overhead
   - **BulkSQLCache** - Eliminates SQL string construction for repeated batch sizes

4. **Consistent 5.5-6.5 M ops/sec for large batches** - Performance plateaus around 6 M/s for batches ≥500, with Storm consistently outperforming raw SQLite

5. **Storm's overhead is only ~3% for single INSERT**, demonstrating that the ORM abstraction layer is extremely efficient! For batch operations, Storm is actually **faster** than raw SQLite thanks to aggressive caching.

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

CRUD benchmarks share data setup via CRTP through `DataBenchmarkBase` (in `base.cppm`). The `CrudBenchmark<Model, test>` fixture in `crud_benchmark.cppm` dispatches `insert` / `insert_no_return` / `update_pk` / `delete_pk` from the `BenchmarkTest` NTTP, mirroring how `QueryBenchmark` handles SELECT-family operations.

**Chunking Boundary for UPDATE:**
- UPDATE binds 5 parameters per row (4 data fields + 1 PK for WHERE clause)
- Max rows per transaction: 999 / 5 = **199 rows**
- Compare to INSERT: 999 / 4 = **249 rows**

**✅ Chunking Boundary Performance (verified 2026-02-06, thorough mode):**

| Batch Size | Efficiency | Notes |
|------------|------------|-------|
| 198 | **~100%** | Just under boundary |
| 199 | **~103%** | Exactly at max_bulk |
| 200 | **~102%** | Just over boundary |

**Note:** Unlike INSERT (which uses bulk SQL), UPDATE always executes individual statements within a transaction. The "chunking boundary" is less significant for UPDATE since there's no multi-row UPDATE syntax in SQLite.

**Single UPDATE Performance (verified 2026-02-06, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single UPDATE** | ~1.80 M/s | ~1.83 M/s | **~98%** | ✅ Excellent for full ORM! |

**Batch UPDATE Performance (verified 2026-02-06, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|------------|-----------|------------|------------|-------|
| 10 | ~2.76 M/s | ~2.38 M/s | **~116%** | ✅ Storm FASTER! |
| 100 | ~2.98 M/s | ~2.84 M/s | **~105%** | ✅ Storm FASTER! |
| 500 | ~2.56 M/s | ~2.59 M/s | **~99%** | ✅ Excellent |
| 1000 | ~2.76 M/s | ~2.78 M/s | **~99%** | ✅ Excellent |
| 5000 | ~3.58 M/s | ~3.54 M/s | **~101%** | ✅ Storm FASTER! |
| 10000 | ~3.62 M/s | ~3.55 M/s | **~102%** | ✅ Storm FASTER! |
| 50000 | ~3.35 M/s | ~3.30 M/s | **~101%** | ✅ Storm FASTER! |
| 100000 | ~3.24 M/s | ~3.21 M/s | **~101%** | ✅ Storm FASTER! |

**Key Optimizations Applied:**

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
- **Storm ORM**: Uses `QuerySet::erase(span)` with automatic strategy selection (bulk IN clause vs individual deletes in transaction)
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

**Single DELETE Performance (verified 2026-02-06, Release build):**

| Operation | Storm ORM | Raw SQLite | Efficiency | Notes |
|-----------|-----------|------------|------------|-------|
| **Single DELETE** | ~3.86 M/s | ~3.86 M/s | **~100.0%** | ✅ Parity with raw! |

**Batch DELETE Performance (verified 2026-02-06, fair comparison, Release build):**

| Batch Size | Storm ORM | Raw SQLite | Efficiency | Strategy | Notes |
|------------|-----------|------------|------------|----------|-------|
| 10 | ~0.91 M/s | ~0.92 M/s | **~98.2%** | IN clause | ✅ Near parity |
| 100 | ~1.32 M/s | ~1.29 M/s | **~102.2%** | IN clause | ✅ Storm FASTER! |
| 500 | ~1.44 M/s | ~1.50 M/s | **~96.3%** | IN clause | ✅ Above 95% target |
| 1000 | ~1.92 M/s | ~1.87 M/s | **~102.7%** | Chunked (2 queries) | ✅ Storm FASTER! |
| 5000 | ~1.89 M/s | ~1.85 M/s | **~101.9%** | Chunked (7 queries) | ✅ Storm FASTER! |
| 10000 | ~1.85 M/s | ~1.77 M/s | **~104.6%** | Chunked (13 queries) | ✅ Storm FASTER! |
| 50000 | ~1.74 M/s | ~1.71 M/s | **~101.9%** | Chunked (63 queries) | ✅ Storm FASTER! |
| 100000 | ~1.70 M/s | ~1.71 M/s | **~99.4%** | Chunked (126 queries) | ✅ Near parity |

**Key Optimizations Applied (2026-02-06 Update):**

1. **RAII TransactionGuard** - Clean transaction management with automatic rollback on early return
   - Similar to Python's `with transaction():` context manager
   - Zero overhead - fully inlined by compiler

2. **Chunked IN Clause for Large Batches** - Instead of 1000 individual DELETEs, uses chunked IN clauses
   - `DELETE WHERE id IN (?, ?, ...)` with up to 799 IDs per chunk

3. **Statement Caching** - Uses `prepare_cached()` for all DELETE statements
   - Prepared statements reused across iterations
   - SQL strings cached per chunk size via thread-local `BulkSQLCache`

4. **Thread-Local Bulk SQL Cache** - Eliminates repeated string allocation for bulk DELETE SQL
   - Same pattern used by INSERT for bulk VALUES SQL generation
   - Returns cached `const std::string&` instead of constructing new string each call

5. **Fair Benchmark Comparison** - Both Storm and Raw SQLite use identical strategies
   - Same threshold calculations (799 rows = 80% of SQLite limit)
   - Same chunking logic for large batches
   - Pre-prepared BEGIN/COMMIT statements for raw SQLite

**Why Results Show ~98-105% Efficiency:**
- Both Storm and Raw SQLite use the **exact same strategy**
- Thread-local SQL caching eliminates allocation overhead in bulk paths
- Statement caching eliminates SQL parsing overhead
- Chunked IN clause is optimal for SQLite's query planner

### Run SELECT Benchmarks

**✅ Benchmark SELECT operations** with various configurations:

```bash
# Run all SELECT benchmarks
./build/release/benchmarks/storm_bench --filter="select" --scale-test

# Run simple SELECT (no WHERE, no JOIN)
./build/release/benchmarks/storm_bench --filter="select_1000"

# Run SELECT with JOIN
./build/release/benchmarks/storm_bench --filter="select_join" --scale-test

# Run SELECT with WHERE + JOIN
./build/release/benchmarks/storm_bench --filter="select_where_join" --scale-test
```

**Available SELECT tests:**
- `select_1000` / `select_5000` / `select_10000` - Simple SELECT (all rows)
- `select_join_100` to `select_join_100000` - SELECT with INNER JOIN
- `select_where_join_100` to `select_where_join_100000` - SELECT with WHERE + INNER JOIN
- `select_left_join_100` to `select_left_join_100000` - SELECT with LEFT JOIN
- `select_left_join_where_100` / `select_left_join_where_10000` - SELECT with LEFT JOIN + WHERE
- `select_right_join_100` to `select_right_join_100000` - SELECT with RIGHT JOIN
- `select_right_join_where_100` / `select_right_join_where_10000` - SELECT with RIGHT JOIN + WHERE
- `select_multi_fk_join_100` to `select_multi_fk_join_100000` - SELECT with multiple FK JOINs (sender + receiver)

**What's tested:**
- **Storm ORM**: Uses `QuerySet::select()` with automatic statement caching
- **Raw SQLite**: Manual `SELECT` with prepared statements
- **Fair comparison**: Both use statement caching and identical query patterns

### SELECT Performance Characteristics

**Seven SELECT Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **Simple SELECT** | `SELECT * FROM table` | Fetch all rows |
| **SELECT + INNER JOIN** | `SELECT ... FROM t INNER JOIN r ON ...` | Fetch with related data |
| **SELECT + WHERE + INNER JOIN** | `SELECT ... FROM t INNER JOIN r ON ... WHERE ...` | Filtered fetch with related data |
| **SELECT + LEFT JOIN** | `SELECT ... FROM t LEFT JOIN r ON ...` | Fetch with optional related data |
| **SELECT + LEFT JOIN + WHERE** | `SELECT ... FROM t LEFT JOIN r ON ... WHERE ...` | Filtered LEFT JOIN |
| **SELECT + RIGHT JOIN** | `SELECT ... FROM t RIGHT JOIN r ON ...` | Fetch from right table with optional left data |
| **SELECT + Multi-FK JOIN** | `SELECT ... FROM t JOIN r1 ON ... JOIN r2 ON ...` | Multiple foreign key JOINs |

**Simple SELECT Performance (verified 2026-01-03, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 1000 | ~10.0 M/s | ~10.5 M/s | **~96%** | ✅ Near parity |
| 5000 | ~9.3 M/s | ~9.7 M/s | **~96%** | ✅ Near parity |
| 10000 | ~9.1 M/s | ~9.4 M/s | **~96%** | ✅ Near parity |

**SELECT + JOIN Performance (verified 2026-01-03, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 1000 | ~7.8 M/s | ~7.6 M/s | **~103%** | ✅ Storm FASTER! |
| 5000 | ~7.7 M/s | ~7.5 M/s | **~103%** | ✅ Storm FASTER! |
| 10000 | ~7.6 M/s | ~7.5 M/s | **~102%** | ✅ Storm FASTER! |

**SELECT + WHERE + JOIN Performance (verified 2026-01-03, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 1000 | ~6.8 M/s | ~6.6 M/s | **~103%** | ✅ Storm FASTER! |
| 5000 | ~7.0 M/s | ~6.9 M/s | **~101%** | ✅ Near parity |
| 10000 | ~6.5 M/s | ~6.9 M/s | **~95%** | ✅ Good |

**SELECT WHERE (no JOIN) Performance (verified 2026-02-06, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| WHERE age > 30 | ~9.3 M/s | ~9.6 M/s | **~97%** | ✅ Near parity |
| WHERE LIKE prefix | ~2.8 M/s | ~2.9 M/s | **~98%** | ✅ Near parity |
| WHERE LIKE contains | ~8.0 M/s | ~8.3 M/s | **~96%** | ✅ Near parity |
| WHERE BETWEEN | ~7.8 M/s | ~7.9 M/s | **~99%** | ✅ Near parity |
| WHERE IN (3 values) | ~1.7 M/s | ~1.7 M/s | **~100%** | ✅ Near parity |
| WHERE AND | ~8.3 M/s | ~8.8 M/s | **~94%** | ✅ Good |
| WHERE OR | ~6.1 M/s | ~6.3 M/s | **~97%** | ✅ Near parity |

**SELECT + LEFT JOIN Performance (verified 2026-01-14, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 100 | ~7.3 M/s | ~7.3 M/s | **~100%** | ✅ Near parity |
| 1000 | ~7.1 M/s | ~7.2 M/s | **~99%** | ✅ Near parity |
| 10000 | ~7.0 M/s | ~7.1 M/s | **~99%** | ✅ Near parity |

**SELECT + RIGHT JOIN Performance (verified 2026-01-14, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 100 | ~4.5 M/s | ~4.5 M/s | **~101%** | ✅ Storm FASTER! |
| 1000 | ~4.4 M/s | ~4.4 M/s | **~100%** | ✅ Near parity |
| 10000 | ~4.3 M/s | ~4.3 M/s | **~100%** | ✅ Near parity |

**SELECT + Multi-FK JOIN Performance (verified 2026-01-14, Release build):**

| Dataset Size | Storm ORM | Raw SQLite | Efficiency | Notes |
|--------------|-----------|------------|------------|-------|
| 100 | ~4.9 M/s | ~5.5 M/s | **~88%** | ✅ Good for double JOIN |
| 1000 | ~4.7 M/s | ~5.3 M/s | **~89%** | ✅ Good for double JOIN |
| 10000 | ~4.5 M/s | ~5.1 M/s | **~88%** | ✅ Good for double JOIN |

**Key Findings:**

1. **Simple SELECT: ~96% efficiency**
   - Statement caching eliminates SQL parsing overhead
   - Raw pointer caching in extraction loops
   - Compile-time SQL generation matches raw performance

2. **SELECT + INNER JOIN: 102-103% efficiency (Storm is FASTER!)**
   - Statement pointer caching avoids connection cache hash lookup
   - Type-erased JOIN extraction pattern is highly optimized
   - SQL string caching avoids repeated concatenation

3. **SELECT + WHERE + JOIN: 95-103% efficiency**
   - Parameter binding adds minimal overhead
   - Larger datasets show slightly more overhead due to increased row extraction

4. **WHERE Operators: 94-100% efficiency**
   - **Basic operators (>, <, =)**: ~97% - Near parity with raw SQLite
   - **LIKE patterns**: 96-98% - Near parity with raw SQLite
   - **IN clauses**: ~100% - Parity with raw SQLite
   - **BETWEEN**: ~99% - Near parity with raw SQLite
   - **AND/OR combinations**: 94-97% - Good efficiency for complex expressions

5. **SELECT + LEFT/RIGHT JOIN: 99-101% efficiency**
   - LEFT JOIN and RIGHT JOIN have near-identical performance to INNER JOIN
   - Same compile-time SQL generation and statement caching optimizations apply
   - RIGHT JOIN slightly slower due to SQLite's internal JOIN handling

6. **SELECT + Multi-FK JOIN: ~88% efficiency**
   - Double JOIN (sender + receiver) has more overhead due to:
     - Two JOIN clauses in SQL
     - More columns to extract per row
     - More complex SQL generation
   - Still excellent for a multi-table JOIN operation

7. **Why JOIN operations show >100% efficiency:**
   - **Statement pointer caching**: Direct pointer reuse vs hash lookup every call
   - **SQL string caching**: Thread-local cache avoids regenerating SQL
   - **Optimized row extraction**: Raw pointer caching in hot loops

### Run DISTINCT Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT DISTINCT operations with various configurations:

```bash
# Run all DISTINCT benchmarks
./build/release/benchmarks/storm_bench --filter="distinct" --scale-test

# Run only simple DISTINCT benchmarks
./build/release/benchmarks/storm_bench --filter="distinct_simple" --scale-test

# Run DISTINCT with WHERE
./build/release/benchmarks/storm_bench --filter="distinct_where" --scale-test

# Run DISTINCT with JOIN
./build/release/benchmarks/storm_bench --filter="distinct_join" --scale-test

# Run specific test
./build/release/benchmarks/storm_bench --filter="distinct_simple_1000"
```

**Available DISTINCT tests:**
- `distinct_simple_1000` / `distinct_simple_10000` - Simple DISTINCT on age field
- `distinct_where_1000` / `distinct_where_10000` - DISTINCT with WHERE clause (salary >= 50000)
- `distinct_join_1000` / `distinct_join_10000` - DISTINCT with INNER JOIN
- `distinct_where_join_1000` / `distinct_where_join_10000` - DISTINCT with WHERE + JOIN

**What's tested:**
- **Storm ORM**: Uses `QuerySet::distinct<Field>().execute()` with automatic SQL generation
- **Raw SQLite**: Manual `SELECT DISTINCT field FROM table` with prepared statements
- **Fair comparison**: Both versions use prepared statement caching and identical query patterns

### DISTINCT Performance Characteristics

**Four DISTINCT Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **Simple DISTINCT** | `SELECT DISTINCT field FROM table` | Unique values from single table |
| **DISTINCT + WHERE** | `SELECT DISTINCT field FROM table WHERE ...` | Filtered unique values |
| **DISTINCT + JOIN** | `SELECT DISTINCT t.field FROM t JOIN r ON ...` | Unique values with related data |
| **DISTINCT + WHERE + JOIN** | `SELECT DISTINCT t.field FROM t JOIN r ON ... WHERE ...` | Filtered unique values with JOIN |

**DISTINCT Performance (verified 2025-01-01, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **distinct_simple_1000** | ~1.37 M/s | ~1.36 M/s | **~100%** | ✅ Near parity |
| **distinct_simple_10000** | ~0.15 M/s | ~0.15 M/s | **~100%** | ✅ Near parity |
| **distinct_where_1000** | ~1.12 M/s | ~1.12 M/s | **~100%** | ✅ Near parity |
| **distinct_where_10000** | ~0.12 M/s | ~0.12 M/s | **~99%** | ✅ Near parity |
| **distinct_join_1000** | ~6.57 M/s | ~6.21 M/s | **~106%** | ✅ Storm FASTER! |
| **distinct_join_10000** | ~6.52 M/s | ~3.92 M/s | **~166%** | ✅ Storm FASTER! |
| **distinct_where_join_1000** | ~6.00 M/s | ~5.63 M/s | **~107%** | ✅ Storm FASTER! |
| **distinct_where_join_10000** | ~6.16 M/s | ~3.84 M/s | **~160%** | ✅ Storm FASTER! |

**Key Findings:**

1. **Simple DISTINCT: ~100% efficiency**
   - Statement caching eliminates SQL parsing overhead
   - Compile-time SQL generation matches raw performance

2. **DISTINCT + WHERE: ~99-100% efficiency**
   - Parameter binding adds minimal overhead
   - Expression caching skips SQL rebuilding on repeated queries

3. **DISTINCT + JOIN: 106-166% efficiency (Storm is FASTER!)**
   - Statement pointer caching avoids connection cache hash lookup
   - SQL string caching avoids repeated concatenation
   - Storm's caching architecture outperforms naive raw SQLite usage

4. **Why JOIN operations show >100% efficiency:**
   - **Statement pointer caching**: Direct pointer reuse vs hash lookup every call
   - **SQL string caching**: Thread-local cache avoids regenerating SQL
   - **Optimized row extraction**: Raw pointer caching in hot loops

**Architecture:**
```cpp
// All SELECT-family operations (including DISTINCT) are driven by a single
// QueryBenchmark<Model, test> class; features dispatch at compile time via
// if constexpr on test.distinct.enabled / test.where.enabled / etc.
template <typename Model, auto const& test>
class QueryBenchmark : public DataBenchmarkBase<QueryBenchmark<Model, test>, Model, 1> {
    // Builds a fully configured QuerySet (JOIN → WHERE → ORDER BY → LIMIT)
    // and stores the terminal (.select() / .execute() / aggregate / distinct / group_by).
    static auto build_qs();
    auto prepare(int n) -> void; // populate dataset of size n, rebuild terminal
    auto run_once() -> void;     // one Storm-only execution — gbench owns the loop
};
// Raw SQLite anchors live in benchmarks/anchors_raw.cpp (the `storm_anchors`
// binary), not on QueryBenchmark itself.
```

### Run Multi-Field DISTINCT Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT DISTINCT on multiple fields:

```bash
# Run multi-field DISTINCT benchmarks
./build/release/benchmarks/storm_bench -c DISTINCT_MULTI

# Run DISTINCT + ORDER BY benchmarks
./build/release/benchmarks/storm_bench -c DISTINCT_ORDER

# Run specific test
./build/release/benchmarks/storm_bench --filter=distinct_multi_field_2_1000
```

**Available Multi-Field DISTINCT tests:**
- `distinct_multi_field_2` - DISTINCT on 2 fields (name, age)
- `distinct_multi_field_3` - DISTINCT on 3 fields (name, age, is_active)
- `distinct_order_by_asc_1000` / `distinct_order_by_asc_10000` - DISTINCT + ORDER BY ASC
- `distinct_order_by_desc_1000` / `distinct_order_by_desc_10000` - DISTINCT + ORDER BY DESC

**What's tested:**
- **Storm ORM**: Uses `QuerySet::distinct<Field1, Field2>().execute()` for multi-field
- **Raw SQLite**: Manual `SELECT DISTINCT field1, field2 FROM table` with prepared statements
- **Fair comparison**: Both versions use statement caching and identical query patterns

### Multi-Field DISTINCT Performance Characteristics

**Multi-Field DISTINCT Performance (verified 2026-02-06, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **distinct_multi_field_2** | ~3.4 M/s | ~3.5 M/s | **~98%** | ✅ 2 fields (name, age) |
| **distinct_multi_field_3** | ~3.3 M/s | ~3.3 M/s | **~101%** | ✅ 3 fields (name, age, is_active) |

**DISTINCT + ORDER BY Performance (verified 2026-01-14, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **distinct_order_by_asc_1000** | ~0.71 M/s | ~0.73 M/s | **~97%** | ORDER BY age ASC |
| **distinct_order_by_asc_10000** | ~0.07 M/s | ~0.07 M/s | **~100%** | Near parity |
| **distinct_order_by_desc_1000** | ~1.27 M/s | ~1.31 M/s | **~97%** | ORDER BY age DESC |
| **distinct_order_by_desc_10000** | ~0.13 M/s | ~0.14 M/s | **~92%** | Good efficiency |

**Key Findings:**

1. **Multi-Field DISTINCT: 98-101% efficiency**
   - Near parity with raw SQLite after benchmark fairness fix
   - Performance is consistent across 2 and 3 fields
   - Both Storm and raw SQLite extract typed columns into tuples

2. **DISTINCT + ORDER BY: 92-100% efficiency**
   - ORDER BY clause is handled at compile-time
   - Near-parity performance with raw SQLite
   - Larger datasets show better efficiency due to amortized overhead

3. **Why multi-field DISTINCT now shows near-parity:**
   - **Fair benchmark**: Both Storm and raw SQLite extract typed columns into `std::tuple<T1, T2, ...>`
   - **Statement caching**: Prepared statements reused across iterations
   - **Compile-time SQL generation**: Zero runtime SQL building overhead

### Why `values()` Has No Separate Benchmark

`values()` (column projection without DISTINCT) shares the same `ProjectionStatement` class as `distinct()` — the only difference is a `ProjectionMode` enum resolved via `if constexpr` at compile time. The compiler generates identical machine code for both modes except for the SQL string literal (`"SELECT "` vs `"SELECT DISTINCT "`). The query execution loop, statement caching, column extraction, and all hot paths are shared. Benchmarking `values()` separately would measure SQLite's query engine difference between `SELECT` and `SELECT DISTINCT`, not any ORM overhead.

### Run LIMIT/OFFSET Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT with LIMIT and OFFSET clauses:

```bash
# Run all LIMIT benchmarks
./build/release/benchmarks/storm_bench -c SELECT_LIMIT

# Run all LIMIT+OFFSET pagination benchmarks
./build/release/benchmarks/storm_bench -c SELECT_LIMIT_OFFSET

# Run WHERE + LIMIT benchmarks
./build/release/benchmarks/storm_bench -c SELECT_WHERE_LIMIT

# Run JOIN + LIMIT benchmarks
./build/release/benchmarks/storm_bench -c SELECT_JOIN_LIMIT

# Run specific test
./build/release/benchmarks/storm_bench --filter=select_limit_100
```

**Available LIMIT/OFFSET tests:**
- `select_limit_10` / `select_limit_50` / `select_limit_100` / `select_limit_500` / `select_limit_1000` - Simple LIMIT
- `select_offset_100` / `select_offset_500` - OFFSET only (with LIMIT -1)
- `select_limit_offset_page1` / `select_limit_offset_page10` / `select_limit_offset_page50` / `select_limit_offset_deep` - Pagination scenarios
- `select_where_limit_100` / `select_where_limit_500` - WHERE + LIMIT
- `select_join_limit_100` / `select_join_limit_500` - JOIN + LIMIT
- `select_join_limit_offset_page1` / `select_join_limit_offset_page50` - JOIN + pagination

**What's tested:**
- **Storm ORM**: Uses `QuerySet::limit(n).offset(m).select()` with automatic SQL generation
- **Raw SQLite**: Manual `SELECT ... LIMIT n OFFSET m` with prepared statements
- **Fair comparison**: Both versions use prepared statement caching and identical query patterns

### LIMIT/OFFSET Performance Characteristics

**Six LIMIT/OFFSET Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **Simple LIMIT** | `SELECT * FROM table LIMIT n` | First N rows |
| **OFFSET only** | `SELECT * FROM table LIMIT -1 OFFSET n` | Skip N rows |
| **LIMIT + OFFSET** | `SELECT * FROM table LIMIT n OFFSET m` | Pagination |
| **WHERE + LIMIT** | `SELECT * FROM table WHERE ... LIMIT n` | Filtered first N |
| **JOIN + LIMIT** | `SELECT ... FROM t JOIN r LIMIT n` | JOIN with limit |
| **JOIN + LIMIT + OFFSET** | `SELECT ... FROM t JOIN r LIMIT n OFFSET m` | JOIN pagination |

**LIMIT Performance (verified 2026-01-13, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **select_limit_10** | ~6.8 M/s | ~7.4 M/s | **~92%** | ✅ Good |
| **select_limit_100** | ~6.6 M/s | ~6.8 M/s | **~96%** | ✅ Near parity |
| **select_limit_500** | ~6.5 M/s | ~6.7 M/s | **~96%** | ✅ Near parity |
| **select_limit_1000** | ~6.3 M/s | ~7.0 M/s | **~89%** | ✅ Good |

**LIMIT + OFFSET Pagination Performance:**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **select_limit_offset_page1** | ~6.6 M/s | ~6.9 M/s | **~95%** | ✅ Near parity |
| **select_limit_offset_page10** | ~6.6 M/s | ~6.7 M/s | **~98%** | ✅ Near parity |
| **select_limit_offset_page50** | ~6.6 M/s | ~6.8 M/s | **~97%** | ✅ Near parity |
| **select_limit_offset_deep** | ~6.5 M/s | ~6.5 M/s | **~99%** | ✅ Near parity |

**JOIN + LIMIT Performance:**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **select_join_limit_100** | ~7.2 M/s | ~6.9 M/s | **~105%** | ✅ Storm FASTER! |
| **select_join_limit_500** | ~7.0 M/s | ~6.8 M/s | **~104%** | ✅ Storm FASTER! |
| **select_join_limit_offset_page1** | ~7.0 M/s | ~6.8 M/s | **~103%** | ✅ Storm FASTER! |
| **select_join_limit_offset_page50** | ~7.0 M/s | ~6.7 M/s | **~104%** | ✅ Storm FASTER! |

**Key Findings:**

1. **Simple LIMIT: 89-96% efficiency**
   - Statement caching eliminates SQL parsing overhead
   - LIMIT clause is a static SQL component (no parameter binding)

2. **LIMIT + OFFSET pagination: 95-99% efficiency**
   - Deep pagination (high OFFSET values) shows better efficiency
   - Consistent performance across different page depths

3. **JOIN + LIMIT: 103-105% efficiency (Storm is FASTER!)**
   - Statement pointer caching avoids connection cache hash lookup
   - SQL string caching avoids repeated SQL concatenation
   - JOIN operations benefit most from Storm's caching architecture

4. **Why JOIN operations with LIMIT show >100% efficiency:**
   - **Statement pointer caching**: Direct pointer reuse vs hash lookup every call
   - **SQL string caching**: Thread-local cache avoids regenerating SQL
   - **Compile-time SQL generation**: Zero runtime SQL string building

### Run ORDER BY Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT with ORDER BY clause:

```bash
# Run all ORDER BY benchmarks
./build/release/benchmarks/storm_bench -c ORDER_BY

# Run ORDER BY with WHERE benchmarks
./build/release/benchmarks/storm_bench -c ORDER_BY_WHERE

# Run ORDER BY with LIMIT (Top-N queries)
./build/release/benchmarks/storm_bench -c ORDER_BY_LIMIT

# Run specific test
./build/release/benchmarks/storm_bench --filter=order_by_single_asc
```

**Available ORDER BY tests:**
- `order_by_single_asc` - Single field ascending (ORDER BY age ASC)
- `order_by_single_desc` - Single field descending (ORDER BY salary DESC)
- `order_by_with_where` - ORDER BY with WHERE clause (WHERE age > 30 ORDER BY salary DESC)
- `order_by_with_limit_10` / `order_by_with_limit_100` - Top-N query pattern (ORDER BY salary DESC LIMIT N)

**What's tested:**
- **Storm ORM**: Uses `QuerySet::order_by<Field>().select()` with automatic SQL generation
- **Raw SQLite**: Manual `SELECT ... ORDER BY field ASC/DESC` with prepared statements
- **Fair comparison**: Both versions use prepared statement caching and identical query patterns

### ORDER BY Performance Characteristics

**Four ORDER BY Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **ORDER BY ASC** | `SELECT * FROM table ORDER BY field ASC` | Sort ascending |
| **ORDER BY DESC** | `SELECT * FROM table ORDER BY field DESC` | Sort descending |
| **ORDER BY + WHERE** | `SELECT * FROM table WHERE ... ORDER BY field` | Filtered sorting |
| **ORDER BY + LIMIT** | `SELECT * FROM table ORDER BY field LIMIT n` | Top-N queries |

**ORDER BY Performance (verified 2026-01-13, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **order_by_single_asc** | ~5.1 M/s | ~5.2 M/s | **~98.7%** | ✅ Near parity |
| **order_by_single_desc** | ~4.2 M/s | ~4.2 M/s | **~99.1%** | ✅ Near parity |
| **order_by_with_where** | ~4.5 M/s | ~4.6 M/s | **~98.4%** | ✅ Near parity |
| **order_by_with_limit_10** | ~10 K/s | ~10 K/s | **~101.5%** | ✅ Storm FASTER! |
| **order_by_with_limit_100** | ~70 K/s | ~73 K/s | **~96.5%** | ✅ Near parity |

**Key Findings:**

1. **ORDER BY ASC/DESC: 98-99% efficiency**
   - Statement caching eliminates SQL parsing overhead
   - ORDER BY clause is a static SQL component
   - Compile-time SQL generation matches raw performance

2. **ORDER BY + WHERE: ~98% efficiency**
   - Parameter binding adds minimal overhead
   - Combined queries show excellent performance

3. **ORDER BY + LIMIT (Top-N): 96-101% efficiency**
   - Top-N queries are highly optimized
   - Small result sets benefit from Storm's caching
   - Statement pointer caching provides performance gains

4. **Why Top-N queries show good efficiency:**
   - **Compile-time ORDER BY direction**: No runtime direction checks
   - **Statement pointer caching**: Direct pointer reuse for repeated queries
   - **Optimized row extraction**: Only fetches limited rows

### Run Multi-Field ORDER BY Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT with multi-field ORDER BY clause:

```bash
# Run all multi-field ORDER BY benchmarks
./build/release/benchmarks/storm_bench -c ORDER_BY_MULTI

# Run specific test
./build/release/benchmarks/storm_bench --filter=order_by_multi_2_asc_10000
```

**Available Multi-Field ORDER BY tests:**
- `order_by_multi_2_asc_1000` - 2-field ORDER BY ASC (name ASC, age ASC, 1K rows)
- `order_by_multi_2_asc_10000` - 2-field ORDER BY ASC (age ASC, salary ASC, 10K rows)
- `order_by_multi_2_desc_10000` - 2-field ORDER BY DESC (age DESC, salary DESC, 10K rows)
- `order_by_multi_2_mixed_10000` - 2-field mixed directions (age ASC, salary DESC, 10K rows)
- `order_by_multi_2_mixed_100000` - 2-field mixed directions (is_active DESC, salary ASC, 100K rows)

**What's tested:**
- **Storm ORM**: Uses variadic `QuerySet::order_by<Field1, Dir1, Field2, Dir2>().select()`
- **Raw SQLite**: Manual `SELECT ... ORDER BY field1 ASC/DESC, field2 ASC/DESC`
- **Fair comparison**: Both versions use prepared statement caching

### Multi-Field ORDER BY Performance Characteristics

**Multi-Field ORDER BY Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **2-field ASC** | `SELECT * FROM table ORDER BY f1 ASC, f2 ASC` | Multi-level sorting |
| **2-field DESC** | `SELECT * FROM table ORDER BY f1 DESC, f2 DESC` | Reverse multi-level sorting |
| **2-field Mixed** | `SELECT * FROM table ORDER BY f1 ASC, f2 DESC` | Complex sorting requirements |

**Multi-Field ORDER BY Performance (verified 2026-01-14, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **order_by_multi_2_asc_1000** | ~5.8 M/s | ~5.9 M/s | **~98%** | ✅ Near parity |
| **order_by_multi_2_asc_10000** | ~4.5 M/s | ~4.6 M/s | **~98%** | ✅ Near parity |
| **order_by_multi_2_desc_10000** | ~3.9 M/s | ~4.0 M/s | **~95%** | ✅ Good efficiency |
| **order_by_multi_2_mixed_10000** | ~4.1 M/s | ~4.1 M/s | **~98%** | ✅ Near parity |
| **order_by_multi_2_mixed_100000** | ~2.6 M/s | ~2.7 M/s | **~99%** | ✅ Near parity |

**Key findings:**

1. **Multi-field ORDER BY: 95-99% efficiency**
   - Multiple ORDER BY fields add minimal overhead
   - Storm ORM uses variadic templates for compile-time SQL generation
   - Direction combinations (ASC/DESC) handled at compile time

2. **Why multi-field ORDER BY shows excellent performance:**
   - **Compile-time SQL generation**: ORDER BY clause built at compile time
   - **Variadic template support**: `order_by<Field1, Dir1, Field2, Dir2>()` expands efficiently
   - **No runtime overhead**: Direction enum converted to bool at compile time

### Run GROUP BY Benchmarks

**✅ NEW FEATURE!** Benchmark SELECT with GROUP BY clause:

```bash
# Run all GROUP BY benchmarks
./build/release/benchmarks/storm_bench -c GROUP_BY

# Run multi-field GROUP BY benchmarks
./build/release/benchmarks/storm_bench -c GROUP_BY_MULTI

# Run GROUP BY + WHERE benchmarks
./build/release/benchmarks/storm_bench -c GROUP_BY_WHERE

# Run specific test
./build/release/benchmarks/storm_bench --filter=group_by_single_age
```

**Available GROUP BY tests:**
- `group_by_single_age` - Single field grouping (GROUP BY age, 10K rows)
- `group_by_single_is_active` - Boolean field grouping (GROUP BY is_active, 10K rows)
- `group_by_single_100k` - Large dataset grouping (GROUP BY age, 100K rows)
- `group_by_with_where_gt` - GROUP BY + WHERE (GROUP BY age WHERE salary > 50000)
- `group_by_with_where_lt` - GROUP BY + WHERE (GROUP BY is_active WHERE age < 40)
- `group_by_multi_2_age_is_active_1000` - 2-field GROUP BY (1K rows)
- `group_by_multi_2_age_is_active_10000` - 2-field GROUP BY (10K rows)
- `group_by_multi_2_is_active_age_10000` - 2-field GROUP BY reversed order (10K rows)

**What's tested:**
- **Storm ORM**: Uses `QuerySet::group_by<Field>().execute()` with automatic SQL generation
- **Raw SQLite**: Manual `SELECT ... GROUP BY field` with prepared statements
- **Fair comparison**: Both versions use prepared statement caching

### GROUP BY Performance Characteristics

**Three GROUP BY Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **Single-Field GROUP BY** | `SELECT * FROM table GROUP BY field` | Simple grouping |
| **GROUP BY + WHERE** | `SELECT * FROM table WHERE ... GROUP BY field` | Filtered grouping |
| **Multi-Field GROUP BY** | `SELECT * FROM table GROUP BY f1, f2` | Complex grouping |

**Single-Field GROUP BY Performance (verified 2026-01-15, Release build):**

| Test | Storm ORM | Raw SQLite | Notes |
|------|-----------|------------|-------|
| **group_by_single_age** | ~8.9 M/s | ~0.05 M/s | High efficiency due to caching |
| **group_by_single_is_active** | ~9.9 M/s | ~0.002 M/s | Boolean grouping (2 groups) |
| **group_by_single_100k** | ~8.7 M/s | ~0.005 M/s | Large dataset |

**GROUP BY + WHERE Performance:**

| Test | Storm ORM | Raw SQLite | Notes |
|------|-----------|------------|-------|
| **group_by_with_where_gt** | ~9.4 M/s | ~0.05 M/s | WHERE salary > 50000 |
| **group_by_with_where_lt** | ~7.8 M/s | ~0.004 M/s | WHERE age < 40 |

**Multi-Field GROUP BY Performance:**

| Test | Storm ORM | Raw SQLite | Notes |
|------|-----------|------------|-------|
| **group_by_multi_2 (1K rows)** | ~9.8 M/s | ~0.41 M/s | 2-field grouping |
| **group_by_multi_2 (10K rows)** | ~9.8 M/s | ~0.04 M/s | Larger dataset |
| **group_by_multi_2 reversed** | ~9.7 M/s | ~0.03 M/s | Field order variation |

**Key Findings:**

1. **GROUP BY shows high efficiency**
   - Storm ORM's statement caching provides significant speedup
   - GROUP BY results are typically much smaller than source data
   - Compile-time SQL generation optimizes query execution

2. **Why GROUP BY shows such high performance ratios:**
   - **Result set reduction**: GROUP BY dramatically reduces output rows
   - **Statement caching**: Prepared statements reused across iterations
   - **Raw SQLite overhead**: Benchmark raw version extracts full row data unnecessarily

3. **Multi-field GROUP BY uses variadic templates:**
   - Single `GroupByMultiFieldBenchmark<Model, Field1, Field2, ...>` class
   - Fold expressions for SQL building and printing
   - Easy to extend to 3+ fields by adding type aliases

### Run GROUP BY + COUNT Benchmarks

**✅ NEW FEATURE!** Benchmark GROUP BY with COUNT aggregate function:

```bash
# Run all GROUP BY + COUNT benchmarks
./build/release/benchmarks/storm_bench -c GROUP_BY_COUNT

# Run specific test
./build/release/benchmarks/storm_bench --filter=group_by_with_count_age_1000
```

**Available GROUP BY + COUNT tests:**
- `group_by_with_count_age_1000` - COUNT per age group (1K rows)
- `group_by_with_count_age_10000` - COUNT per age group (10K rows)
- `group_by_with_count_is_active_10000` - COUNT per boolean group (2 groups)
- `group_by_with_count_age_100000` - COUNT per age group (100K rows)

**What's tested:**
- **Storm ORM**: Uses `qs.group_by<field>().count().execute()` returning `plf::hive<std::tuple<GroupKeyType, int64_t>>`
- **Raw SQLite**: Manual `SELECT field, COUNT(*) FROM table GROUP BY field`
- **Fair comparison**: Both count actual groups returned

**GROUP BY + COUNT Performance (verified 2026-01-15, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **1K rows (age)** | ~0.66 M/s | ~0.67 M/s | **~99.2%** | Near parity |
| **10K rows (age)** | ~0.06 M/s | ~0.06 M/s | **~99.7%** | Scales well |
| **10K rows (is_active, 2 groups)** | ~0.06 M/s | ~0.06 M/s | **~100.1%** | Storm slightly faster |
| **100K rows (age)** | ~0.006 M/s | ~0.006 M/s | **~99.7%** | Large dataset |

**Key Findings:**

1. **GROUP BY + COUNT achieves near-parity (98.9-100.1%)**
   - Proper chaining: `group_by<field>().count().execute()`
   - Both Storm and raw SQLite return same group count

2. **Low throughput is expected:**
   - Each operation scans entire dataset for grouping
   - GROUP BY + COUNT is I/O bound, not CPU bound
   - Efficiency metric (Storm vs raw) is the key measure

### Run GROUP BY + SUM Benchmarks

**✅ NEW FEATURE!** Benchmark GROUP BY with SUM aggregate function:

```bash
# Run all GROUP BY + SUM benchmarks
./build/release/benchmarks/storm_bench -c GROUP_BY_SUM

# Run specific test
./build/release/benchmarks/storm_bench --filter=group_by_with_sum_age_salary_1000
```

**Available GROUP BY + SUM tests:**
- `group_by_with_sum_age_salary_1000` - SUM(salary) per age group (1K rows)
- `group_by_with_sum_age_salary_10000` - SUM(salary) per age group (10K rows)
- `group_by_with_sum_is_active_salary_10000` - SUM(salary) per boolean group (2 groups)
- `group_by_with_sum_age_salary_100000` - SUM(salary) per age group (100K rows)

**What's tested:**
- **Storm ORM**: Uses `qs.group_by<field>().sum<agg_field>().execute()` returning `plf::hive<std::tuple<GroupKeyType, double>>`
- **Raw SQLite**: Manual `SELECT field, SUM(agg_field) FROM table GROUP BY field`
- **Fair comparison**: Both count actual groups returned

**GROUP BY + SUM Performance (verified 2026-01-15, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **1K rows (age, salary)** | ~0.51 M/s | ~0.51 M/s | **~99.4%** | Near parity |
| **10K rows (age, salary)** | ~0.05 M/s | ~0.05 M/s | **~99.6%** | Scales well |
| **10K rows (is_active, 2 groups)** | ~0.05 M/s | ~0.05 M/s | **~99.3%** | Boolean grouping |
| **100K rows (age, salary)** | ~0.005 M/s | ~0.005 M/s | **~98.4%** | Large dataset |

**Key Findings:**

1. **GROUP BY + SUM achieves near-parity (98.4-99.6%)**
   - Proper chaining: `group_by<field>().sum<agg_field>().execute()`
   - Both Storm and raw SQLite return same group count

2. **Low throughput is expected:**
   - Each operation scans entire dataset for grouping and summing
   - GROUP BY + SUM is I/O bound, not CPU bound
   - Efficiency metric (Storm vs raw) is the key measure

### Run WHERE Operator Benchmarks

**✅ NEW FEATURE!** Benchmark advanced WHERE operators (LIKE, BETWEEN, IN, AND/OR):

```bash
# Run LIKE pattern matching benchmarks
./build/release/benchmarks/storm_bench -c WHERE_LIKE

# Run BETWEEN range query benchmarks
./build/release/benchmarks/storm_bench -c WHERE_BETWEEN

# Run IN set membership benchmarks
./build/release/benchmarks/storm_bench -c WHERE_IN

# Run AND/OR complex condition benchmarks
./build/release/benchmarks/storm_bench -c WHERE_AND
./build/release/benchmarks/storm_bench -c WHERE_OR

# Run specific test
./build/release/benchmarks/storm_bench --filter=where_like_prefix
```

**Available WHERE Operator tests:**
- `where_like_prefix` - Pattern matching with prefix (name LIKE 'Person1%')
- `where_like_contains` - Pattern matching with contains (name LIKE '%son%')
- `where_between_int` - Range queries (age BETWEEN 25 AND 45)
- `where_in_small` - Small IN set (age IN (25, 30, 35))
- `where_and_simple` - AND combination (age > 30 AND salary > 50000)
- `where_or_simple` - OR combination (age < 25 OR age > 60)

**What's tested:**
- **Storm ORM**: Uses `field<>().like()`, `field<>().between()`, `field<>().in()`, and `&&`/`||` operators
- **Raw SQLite**: Manual SQL with LIKE, BETWEEN, IN clauses
- **Fair comparison**: Both versions use prepared statement caching

### WHERE Operator Performance Characteristics

**Five WHERE Operator Configurations:**

| Configuration | SQL Pattern | Use Case |
|--------------|-------------|----------|
| **LIKE prefix** | `SELECT * FROM table WHERE name LIKE 'pattern%'` | Fast prefix search |
| **LIKE contains** | `SELECT * FROM table WHERE name LIKE '%pattern%'` | Full text search |
| **BETWEEN** | `SELECT * FROM table WHERE field BETWEEN ? AND ?` | Range queries |
| **IN** | `SELECT * FROM table WHERE field IN (?, ?, ...)` | Set membership |
| **AND/OR** | `SELECT * FROM table WHERE cond1 AND/OR cond2` | Complex conditions |

**WHERE Operator Performance (verified 2026-02-06, Release build):**

| Test | Storm ORM | Raw SQLite | Efficiency | Notes |
|------|-----------|------------|------------|-------|
| **where_like_prefix** | ~2.8 M/s | ~2.9 M/s | **~98%** | ✅ Near parity |
| **where_like_contains** | ~8.0 M/s | ~8.3 M/s | **~96%** | ✅ Near parity |
| **where_between_int** | ~7.8 M/s | ~7.9 M/s | **~99%** | ✅ Near parity |
| **where_in_small** | ~1.7 M/s | ~1.7 M/s | **~100%** | ✅ Parity |
| **where_and_simple** | ~8.3 M/s | ~8.8 M/s | **~94%** | ✅ Good |
| **where_or_simple** | ~6.1 M/s | ~6.3 M/s | **~97%** | ✅ Near parity |

**🎯 What Fixed the Benchmark (2026-02-06 Update)**

**Critical Issue Identified and Resolved:**

**❌ UNFAIR COMPARISON** - Raw SQLite benchmark was NOT extracting column data like Storm ORM!

**The Problem:**
- Storm ORM performed full SELECT with object extraction into `plf::hive<Model>`
- Raw SQLite was just counting rows with `total++` without extracting any data
- This made Storm appear 55-91% efficient - completely misleading!

**The Fix:**
1. ✅ **Added proper column extraction** to raw SQLite - now extracts all 5 columns per row
2. ✅ **Added `extract_row()` helper** to each benchmark class for type-safe extraction
3. ✅ **Results stored in `plf::hive<Model>`** - same container as Storm ORM
4. ✅ **Fair apples-to-apples comparison** - Both Storm and raw SQLite now do identical work

**Result:**
- **Before fix**: 55-91% efficiency (raw SQLite just counted rows) - UNFAIR
- **After fix**: 94-100% efficiency (both extract full row data) - FAIR COMPARISON!

**Key Findings:**

1. **LIKE: 96-98% efficiency**
   - Near parity with raw SQLite for both prefix and contains patterns
   - Statement caching eliminates SQL parsing overhead

2. **BETWEEN: ~99% efficiency**
   - Near parity with raw SQLite
   - Range queries are highly efficient with proper extraction

3. **IN: ~100% efficiency**
   - Parity with raw SQLite
   - Set membership queries show excellent performance

4. **AND/OR: 94-97% efficiency**
   - Good efficiency for complex multi-condition expressions
   - Expression tree traversal adds minimal overhead

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
            ^^^^^^  (bold green = ≥95%, green = 85-95%, yellow = 70-85%, red = <70%)
✅ Benchmark complete!                    (green)

✅ Filtered tests completed!
```

**Performance-Based Color Coding:**

**Throughput:**
- **Bold Green** (≥5M ops/sec): Excellent performance
- **Green** (1-5M ops/sec): Good performance
- **Yellow** (<1M ops/sec): Needs optimization

**Efficiency:**
- **Bold Green** (≥95%): Excellent - near parity with raw SQLite
- **Green** (85-95%): Good - acceptable overhead
- **Yellow** (70-85%): Moderate - needs investigation
- **Red** (<70%): Poor - significant overhead

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
// In parser.cppm (storm_benchmark_parser module)
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
    static void execute(BenchmarkRunner& runner, int iterations, ...) {
        constexpr auto& test = BENCHMARK_TESTS[TestIndex];
        constexpr std::string_view operation = test.operation.view();

        // CRUD operations keep dedicated classes (data-driven, not query-driven).
        if constexpr (operation == "insert") {
            run_insert_operation<Model, test>(runner, iterations);
        } else if constexpr (operation == "update_pk") {
            run_update_pk_operation<Model, test>(runner, iterations);
        } else if constexpr (operation == "delete_pk") {
            run_delete_pk_operation<Model, test>(runner, iterations);
        } else {
            // ALL SELECT-family operations — one unified class, feature dispatch
            // happens inside QueryBenchmark via if constexpr on test.*.enabled.
            registry::with_base_model<test>([&]<typename M>() {
                runner.template dispatch_sized<test, IterKind::Dataset>(
                    iterations, test.dataset_size,
                    [](int size) { return QueryBenchmark<M, test>{size}; });
            });
        }

        if constexpr (TestIndex + 1 < TotalTests)
            TestExecutor<Model, TestIndex + 1, TotalTests>::execute(runner, iterations, ...);
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

### Step 1: Add Test Definition to YAML

Edit `benchmarks/tests/benchmark_tests.yaml` (the human-friendly source of truth):

**Option A: Fixed-size test** (iterations and size specified directly)

```yaml
  - name: where_string_like
    category: WHERE
    description: "WHERE with LIKE pattern (name LIKE 'Person%')"
    model: Person
    operation: where
    where_field: name
    where_op: LIKE
    where_value_string: "Person%"
    iterations: 1000
    dataset_size: 10000
```

**Option B: Size profile test** (auto-iterates over multiple sizes)

```yaml
  - name: select_custom
    category: SELECT_CUSTOM
    description: "Custom SELECT operation with size iteration"
    model: Person
    operation: select_custom
    size_profile: dataset_standard
```

This will generate tests: `select_custom_100`, `select_custom_1000`, `select_custom_10000`, `select_custom_100000`

> **Note:** The YAML file is automatically converted to JSON during build via `scripts/yaml_to_json.py`. The C++ parser reads the generated JSON using `#embed`.

### Step 2: Wire it into the right fixture

If the operation fits the SELECT family (WHERE / JOIN / aggregates / DISTINCT / GROUP BY / etc.), `benchmarks/query_benchmark.cppm` already dispatches via `if constexpr` on the test's flags — you typically only edit YAML and the matching `if constexpr` branch in `QueryBenchmark::build_qs` / `run_once`.

CRUD operations (insert / update_pk / delete_pk) live in `benchmarks/crud_benchmark.cppm`. Adding a new CRUD-shaped operation means a new branch in `CrudBenchmark::run_once` plus a name match in `register.cpp::is_crud`.

For genuinely new operation families, add a fixture module alongside `query_benchmark.cppm` / `crud_benchmark.cppm`, import it from `register.cpp`, and have `register_all<>()` route the right tests to it.

### Step 3: Rebuild and Run

```bash
cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/<category>/<name>'
```

Google Benchmark picks up the new entry automatically — `register.cpp` walks `BENCHMARK_TESTS` at startup and registers a `void(*)()` trampoline per test.

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
  "test_category": "WHERE|INSERT|SELECT|JOIN|DISTINCT|AGGREGATE|SELECT_LIMIT|SELECT_OFFSET|SELECT_LIMIT_OFFSET|ORDER_BY|ORDER_BY_WHERE|ORDER_BY_LIMIT|ORDER_BY_MULTI|WHERE_LIKE|WHERE_BETWEEN|WHERE_IN|WHERE_AND|WHERE_OR",
  "description": "Human-readable description",
  "model": "Person",
  "operation": "where|insert|select|select_limit|select_offset|select_limit_offset|select_where_limit|select_join_limit|select_join_limit_offset|order_by_asc|order_by_desc|order_by_where|order_by_limit|order_by_multi_2_asc|order_by_multi_2_desc|order_by_multi_2_mixed|where_like|where_between|where_in|where_and|where_or",
  "iterations": 1000,
  "dataset_size": 10000,
  "batch_size": 1,  // For batch operations (1 = single, >1 = batch)

  // For WHERE operations
  "where_field": "age",
  "where_op": ">|<|>=|<=|==|!=|LIKE|BETWEEN",
  "where_value_int": 30,
  "where_value_double": 50000.0,
  "where_value_bool": true,
  "where_value_string": "pattern",
  "where_value_int2": 45,            // Second value for BETWEEN
  "where_in_values": [25, 30, 35],   // Array for IN operator

  // For complex AND/OR conditions
  "where_field2": "salary",
  "where_op2": ">",
  "where_value2_int": 50000,

  // For LIMIT/OFFSET operations
  "limit_value": 100,   // Number of rows to return (0 = no LIMIT)
  "offset_value": 0,    // Number of rows to skip (0 = no OFFSET)

  // For ORDER BY operations
  "order_by_field": "salary",      // Field name to order by
  "order_by_direction": "DESC",    // "ASC" or "DESC" (default: ASC)

  // For Multi-Field ORDER BY operations
  "order_by_field2": "name",       // Second field for multi-field ORDER BY
  "order_by_direction2": "ASC"     // Direction for second field
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

See [GitHub Issues (benchmarks)](https://github.com/spiritEcosse/storm/issues?q=is%3Aissue+is%3Aopen+label%3Abenchmarks) for planned improvements.

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

See [GitHub Issues (benchmarks)](https://github.com/spiritEcosse/storm/issues?q=is%3Aissue+is%3Aopen+label%3Abenchmarks) for planned features and improvements.

## 🔍 Troubleshooting

### "No tests executed"

**Cause:** YAML file is empty or malformed, or JSON generation failed.

**Solution:**
1. Check `benchmarks/tests/benchmark_tests.yaml` is valid YAML and contains at least one test definition
2. Verify JSON was generated: `ls benchmarks/tests/benchmark_tests.json`
3. Manually regenerate: `python3 benchmarks/scripts/yaml_to_json.py`

### Compilation error: "field not found"

**Cause:** YAML references a field name that doesn't exist in the model struct.

**Solution:** Ensure `where_field` matches an exact field name in the model (case-sensitive):

```cpp
struct Person {
    int id;
    std::string name;  // Use "name" in YAML, not "Name"
    int age;           // Use "age" in YAML
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

The custom `timing.hpp` / `timing_trace.hpp` macros from the pre-Google-Benchmark runner are gone. Use the tools Google Benchmark and the OS already give you:

- **Per-benchmark counters** — `state.counters["sql_build_ns"] = ...;` inside a fixture body. Renders inline in the report and works under `--benchmark_format=json` for diffs.
- **`perf` integration** — `perf record -- ./build/release/benchmarks/storm_bench --benchmark_filter='Storm/INSERT/insert/249'` then `perf report` for hot-function profiling. Use `--benchmark_min_time=2s` to give `perf` enough samples per fixture.
- **Hardware counters from gbench** — build with `-DBENCHMARK_ENABLE_LIBPFM=ON` then `--benchmark_perf_counters=CYCLES,INSTRUCTIONS,CACHE-MISSES`.
- **Narrowing in on a single op** — combine `--benchmark_filter` with `--benchmark_repetitions=1 --benchmark_min_time=0.05s` to make the run cheap enough for `perf stat` round-trips.

When you need ad-hoc per-call timing inside a Storm code path, drop a local `auto t = std::chrono::steady_clock::now(); ...; std::cerr << ...;` into the file under investigation and remove it before committing — there is no longer a project-wide tracing macro.

