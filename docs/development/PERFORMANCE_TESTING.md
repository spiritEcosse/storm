# Performance Testing

Performance testing is **mandatory** for all new Storm ORM features. Target: ≥70% of raw SQLite efficiency.

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
- Efficiency: 85% ✅ GOOD (target: ≥70%)

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

## Performance Regression Prevention

- **CI integration**: Run benchmarks on every PR
- **Threshold checks**: Fail if performance drops >5%
- **Before commit**: `python3 bench.py --compare`

## See Also

- [Adding Features](ADDING_FEATURES.md) - Development workflow
- [Benchmarks](../../benchmarks/README.md) - Current benchmark results
