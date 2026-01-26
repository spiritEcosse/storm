# Performance Testing

Performance testing is **mandatory** for all new Storm ORM features. Target: ≥95% of raw SQLite efficiency.

## Quick Start

```bash
# Recommended: Python benchmark suite with auto-rebuild
python3 bench.py --all                   # All microbenchmarks
python3 bench.py --joins                 # JOIN performance
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --compare               # Full comparison

# Manual benchmark execution
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_storm
```

## Performance Requirements

Every new feature requires:

1. **Benchmark file**: `benchmarks/bench_<feature>.cpp`
2. **Raw SQLite comparison**: Calculate efficiency percentage
3. **Documentation**: Add results to relevant docs
4. **Commit message**: Include performance metrics

**Example**: `feat: add LIMIT support (85% of raw SQLite)`

## Design Principles

### DRY/KISS vs Performance

Storm follows DRY/KISS principles **unless** they cost >10% performance:

| Scenario | DRY/KISS | Performance | Decision |
|----------|----------|-------------|----------|
| Clean abstraction, <5% cost | ✅ Yes | Minimal | Follow DRY/KISS |
| Clean abstraction, >10% cost | ❌ No | Significant | Prioritize performance |
| Complex optimization, >20% gain | ❌ No | Major | Accept complexity |

### Decision Matrix

1. **<5% performance impact**: Use abstraction (follow DRY/KISS)
2. **5-10% performance impact**: Profile carefully, document tradeoff
3. **>10% performance impact**: Duplicate code if needed
4. **>20% performance gain**: Accept complexity, document thoroughly

## Benchmark Workflow

### 1. Implement Feature

```bash
# Example: Add LIMIT support
src/orm/statements/select.cppm
```

### 2. Create Benchmark

```bash
touch benchmarks/bench_limit.cpp
```

Use `benchmarks/common.py` for test infrastructure:

```cpp
#include "common.h"

void benchmark_storm_limit(size_t row_count) {
    // Storm ORM implementation
}

void benchmark_raw_limit(size_t row_count) {
    // Raw SQLite baseline
}
```

### 3. Run Benchmark

```bash
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_limit --size=10000 --iterations=100
```

### 4. Calculate Efficiency

```
Efficiency = (Storm_ORM_performance / Raw_SQLite_performance) * 100%
```

**Example**:
- Storm: 8.5M rows/sec
- Raw: 10M rows/sec
- Efficiency: 85% ⚠️ NEEDS WORK (target: ≥95%)

### 5. Document Results

Update relevant documentation:
- Performance tables
- Optimization techniques
- Benchmark commands

### 6. Commit with Metrics

```bash
git add . && git commit -m "feat: add LIMIT support (85% of raw SQLite)"
```

## Example Benchmarks

### SELECT Benchmark

```cpp
void benchmark_storm_select() {
    QuerySet<Person> qs(conn);
    for (int i = 0; i < iterations; ++i) {
        auto result = qs.select();
    }
}

void benchmark_raw_select() {
    auto stmt = conn.prepare("SELECT id, name, age FROM Person");
    for (int i = 0; i < iterations; ++i) {
        while (stmt->step()) {
            // Extract manually
        }
        stmt->reset();
    }
}
```

### WHERE Benchmark

```cpp
void benchmark_storm_where() {
    QuerySet<Person> qs(conn);
    for (int i = 0; i < iterations; ++i) {
        auto result = qs.where(field<^^Person::age>() > 25).select();
    }
}
```

## Writing Fair Benchmarks

**⚠️ CRITICAL: Unfair benchmarks lead to wrong optimization decisions.**

### 1. Setup Outside Loop, Execute Inside

```cpp
// ❌ UNFAIR: Storm does setup inside, raw SQLite does setup outside
int storm_benchmark(int iterations) {
    for (int i = 0; i < iterations; i++) {
        qs.where(age > 30).select();  // WHERE built every iteration
    }
}

int raw_benchmark(int iterations) {
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);  // Prepared ONCE
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        sqlite3_step(stmt);
    }
}

// ✅ FAIR: Both do setup once, execute in loop
int storm_benchmark(int iterations) {
    auto where_clause = build_where();
    qs.where(where_clause);           // Set WHERE once
    for (int i = 0; i < iterations; i++) {
        qs.select();                  // Only execute in loop
    }
    qs.reset();                       // Cleanup after
}

int raw_benchmark(int iterations) {
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, value); // Bind once
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);          // Reset required for re-execution
        while (sqlite3_step(stmt) == SQLITE_ROW) { ... }
    }
}
```

### 2. Same Algorithm for Both

```cpp
// ❌ UNFAIR: Storm uses chunked bulk SQL, raw uses single inserts
// Storm: INSERT INTO t VALUES (?,?),(?,?),(?,?)  -- 3 rows at once
// Raw:   INSERT INTO t VALUES (?,?) × 3          -- 3 separate statements

// ✅ FAIR: Both use same strategy
// Storm: INSERT INTO t VALUES (?,?),(?,?),(?,?)
// Raw:   INSERT INTO t VALUES (?,?),(?,?),(?,?)
```

### 3. Same Container Types

```cpp
// ❌ UNFAIR: Storm uses plf::hive, raw uses std::vector
plf::hive<Model> storm_results;  // O(1) insert, stable iterators
std::vector<Model> raw_results;  // O(1) amortized, may reallocate

// ✅ FAIR: Both use same container
plf::hive<Model> storm_results;
plf::hive<Model> raw_results;
```

### 4. Measure Same Work

```cpp
// ❌ MISLEADING: Comparing throughput with different result sizes
// DISTINCT + WHERE: 78 rows → 0.13M rows/sec (looks slow)
// DISTINCT + JOIN:  10K rows → 8.75M rows/sec (looks fast)

// ✅ CORRECT: Use latency (ms/query) for queries with different result sizes
// DISTINCT + WHERE: 0.588ms/query (actually FASTEST)
// DISTINCT + JOIN:  1.143ms/query (actually slower)
```

### 5. Runtime vs Compile-Time Fairness

```cpp
// ❌ UNFAIR: Storm uses runtime batch_size, raw uses compile-time
template <int BatchSize>  // Raw gets free compile-time optimization
void raw_benchmark() {
    if constexpr (BatchSize < 100) { ... }  // Compiled away
}

void storm_benchmark(int batch_size) {  // Runtime check every call
    if (batch_size < 100) { ... }
}

// ✅ FAIR: Both use runtime values
void storm_benchmark(int batch_size) { ... }
void raw_benchmark(int batch_size) { ... }  // Same decision logic
```

### Benchmark Checklist

- [ ] **Setup outside loop**: WHERE clauses, statement preparation, parameter binding
- [ ] **Same algorithm**: Both use identical strategies (chunked bulk, single row, etc.)
- [ ] **Same containers**: plf::hive vs plf::hive, not plf::hive vs std::vector
- [ ] **Same decision logic**: Runtime vs runtime, not runtime vs compile-time
- [ ] **Correct metric**: Latency for different result sizes, throughput for same sizes
- [ ] **Multiple runs**: 5+ runs to establish variance, report median not just mean

## Performance Regression Prevention

- **CI integration**: Run benchmarks on every PR
- **Threshold checks**: Fail if performance drops >5%
- **Before commit**: `python3 bench.py --compare`

## See Also

- [Adding Features](ADDING_FEATURES.md) - Development workflow
- [Benchmarks](../../benchmarks/README.md) - Current benchmark results
