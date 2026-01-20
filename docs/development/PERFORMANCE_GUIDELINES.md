# Performance-Driven Development Guidelines

## Core Philosophy

When deciding which implementation approach is "good", **performance testing is mandatory**. Storm ORM's primary goal is achieving near-raw SQLite performance, so all implementation decisions must be validated with benchmarks.

## Performance Evaluation Requirements

### 1. Benchmark Before Accepting

- Every new feature (WHERE clauses, LIMIT, ORDER BY, etc.) **requires** performance testing
- Compare Storm ORM against raw SQLite to measure efficiency percentage
- **Target**: ≥95% of raw SQLite performance for acceptable implementations
- Document results in docs with operation details and efficiency metrics

### 2. Use `common.py` for Test Infrastructure

- When adding new features, create dedicated performance tests
- Use `benchmarks/common.py` utilities for consistent test setup and data generation
- If test infrastructure doesn't exist, create it following existing patterns (see `bench_join.cpp`)
- Example structure:
  ```cpp
  // benchmarks/bench_limit.cpp
  #include "common.h"
  // Benchmark Storm ORM LIMIT vs Raw SQLite LIMIT
  ```

### 3. Separate Performance Test Files

- Each major feature should have its own benchmark file for detailed analysis
- Structure: `benchmarks/bench_<feature>.cpp` (e.g., `bench_limit.cpp`, `bench_where.cpp`)
- Compare Storm ORM implementation against equivalent raw SQL
- Use command-line flags for flexible testing (see `bench_join.cpp --help` as example)

### 4. Raw SQL Comparison is Required

- Every benchmark must include raw SQLite baseline for efficiency calculation
- Report efficiency percentage: `(Storm_ORM_performance / Raw_SQLite_performance) * 100`
- Include in commit message: "feat: add LIMIT support (85% of raw SQLite efficiency)"

## Design Principles Balance

Storm ORM follows **DRY (Don't Repeat Yourself)** and **KISS (Keep It Simple, Stupid)** principles, but **performance takes precedence** when there's a conflict.

### DRY Principle

- ✅ **Apply DRY**: Extract common patterns into BaseStatement utilities, use templates for reusable code
- ✅ **Apply DRY**: Compile-time SQL generation with ConstexprString (eliminates runtime duplication)
- ❌ **Break DRY if needed**: If abstraction costs >10% performance, duplicate code instead
- **Example**: Statement-level caching duplicates caching logic across Update/Delete/Select, but achieves 20x+ speedup

### KISS Principle

- ✅ **Apply KISS**: Prefer simple, readable implementations when performance is equivalent
- ✅ **Apply KISS**: Use standard library features (`std::index_sequence`) over complex custom solutions
- ❌ **Break KISS if needed**: Complex optimizations justified if they improve performance significantly
- **Example**: Thread-local SQL caching adds complexity but provides 94% performance improvement

## Decision Matrix

| Scenario | DRY/KISS Compliant? | Performance Impact | Decision |
|----------|---------------------|-------------------|----------|
| Clean abstraction, no perf cost | ✅ Yes | None | Use abstraction (follow DRY/KISS) |
| Clean abstraction, <5% perf cost | ✅ Yes | Minimal | Use abstraction (follow DRY/KISS) |
| Clean abstraction, 5-10% perf cost | ⚠️ Maybe | Moderate | Profile carefully, document tradeoff |
| Clean abstraction, >10% perf cost | ❌ No | Significant | Duplicate code, prioritize performance |
| Complex optimization, >20% perf gain | ❌ No | Major improvement | Accept complexity, document thoroughly |

## Performance Testing Workflow

```bash
# 1. Implement feature (e.g., LIMIT support)
# 2. Create benchmark file using common.py patterns
touch benchmarks/bench_limit.cpp

# 3. Build and run performance tests
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_limit --size=10000 --iterations=100

# 4. Compare with raw SQLite
# If Storm: 8.5M rows/sec, Raw: 10M rows/sec → 85% efficiency ✅ GOOD
# If Storm: 5M rows/sec, Raw: 10M rows/sec → 50% efficiency ❌ NEEDS WORK

# 5. Document in docs/benchmarks/results.md
# Add to performance results table with efficiency percentage

# 6. Commit with performance metrics
git add . && git commit -m "feat: add LIMIT support (85% of raw SQLite)"
```

## Mandatory Performance Documentation

When adding new features, update documentation with:

1. **Performance Results Table**: Add row with Storm vs Raw SQLite comparison
2. **Key Design Decisions**: Document optimization techniques used
3. **Benchmark Commands**: Show how to run performance tests for this feature

## Feature Implementation Checklist

- [ ] Implement feature in `src/orm/statements/`
- [ ] Add comprehensive tests in `tests/test_<feature>.cpp`
- [ ] Create performance benchmark in `benchmarks/bench_<feature>.cpp`
- [ ] Run benchmark and measure efficiency vs raw SQLite
- [ ] If efficiency <95%, optimize or document reasons
- [ ] Update `docs/benchmarks/results.md` with performance tables
- [ ] Document any DRY/KISS tradeoffs made for performance
- [ ] Commit with performance metrics in message

## Performance Regression Prevention

- All PRs with ORM changes must include benchmark results
- CI should fail if performance drops >5% without justification
- Use `python3 bench.py --compare` before committing to catch regressions

## Profiling Tips

1. **Always use Release builds** for accurate profiling
2. **Use perf/valgrind** for hotspot analysis
3. **Profile with realistic data** (10K+ rows)
4. **Measure multiple iterations** for statistical significance
5. **Compare against raw SQLite** as baseline
6. **Document bottlenecks** in design decision docs

## Statement Pointer Caching (MANDATORY for Hot Paths)

**For single-row operations called in loops, cache the statement pointer locally.** Benchmarks show ~23% improvement.

### The Problem

`prepare_cached()` returns a pointer to a cached statement, but it performs a hash lookup every call:

```cpp
// Inside Connection::prepare_cached()
auto it = statement_cache_.find(sql);  // Hash lookup using SQL string
if (it != statement_cache_.end()) {
    it->second.reset();
    return &it->second;
}
```

### The Solution

Cache the returned pointer locally to skip hash lookups after the first call:

```cpp
// ❌ SLOW: Hash lookup every call (78% efficiency)
auto execute_one(const T& obj) {
    auto stmt = conn_->prepare_cached(sql);  // Hash lookup EVERY call
    stmt->reset();
    bind(...);
    stmt->execute();
}

// ✅ FAST: Cache pointer, skip hash lookup (96% efficiency)
auto execute_one(const T& obj) {
    if (!cached_stmt_) {                              // First call only
        cached_stmt_ = conn_->prepare_cached(sql);   // Hash lookup once
    }
    cached_stmt_->reset();                           // Direct pointer access
    bind(...);
    cached_stmt_->execute();
}
```

### Benchmark Evidence (Single DELETE)

| Pattern | Ops/sec | Efficiency |
|---------|---------|------------|
| Without pointer cache | 5.02 M | 78.2% |
| With pointer cache | 6.18 M | 96.3% |

**Result: 23% performance improvement (6.18/5.02 = 1.23x faster)**

### When to Use

| Use Pointer Cache | Don't Need Pointer Cache |
|-------------------|--------------------------|
| Single-row operations (`execute_one`) | Batch operations (hash amortized) |
| Methods called in tight loops | Cold paths (setup, config) |
| Hot paths with millions of calls | Operations called once per request |

### Implementation Pattern

```cpp
class SomeStatement {
    // Member to cache statement pointer
    mutable Statement* cached_stmt_ = nullptr;

    auto execute_one(const T& obj) {
        if (!cached_stmt_) {
            cached_stmt_ = conn_->prepare_cached(get_sql());
        }
        // Use cached_stmt_ directly...
    }
};
```

## Performance Goals

- **CRUD Operations**: ≥95% of raw SQLite efficiency
- **JOIN Operations**: ≥95% of raw SQLite efficiency
- **DISTINCT Operations**: ≥95% of raw SQLite efficiency
- **Batch Operations**: Match or exceed sqlite_orm by 2x+
- **Statement Caching**: 20x+ speedup for repeated operations
