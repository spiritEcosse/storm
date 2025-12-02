# Storm ORM Benchmarking Guide

Complete guide to running and understanding Storm ORM performance benchmarks.

## Quick Reference

**⚠️ CRITICAL: ALWAYS use Release builds for benchmarks!**

```bash
# Build release first (MANDATORY)
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Run unified benchmark system
./build/release/benchmarks/storm_bench                           # All tests
./build/release/benchmarks/storm_bench --list                    # List available tests
./build/release/benchmarks/storm_bench --filter=insert_batch_100 # Run specific test (exact match)
./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test  # Test performance degradation
./build/release/benchmarks/storm_bench --iterations=10000        # Custom iterations

# See benchmarks/README.md for detailed unified benchmark documentation
```

**Why Release-Only:**
- Debug builds: 10-100x slower (meaningless results)
- No `-O3` optimization = no inlining, no loop unrolling
- Cannot detect real-world performance characteristics

## Available Benchmarks

### 1. JOIN Performance (`--joins`)

**Purpose:** Compare Storm ORM vs Raw SQLite JOIN operations

**What it measures:**
- INNER JOIN (single FK & multi FK)
- LEFT JOIN (single FK & multi FK)
- RIGHT JOIN (single FK & multi FK)
- Efficiency relative to raw SQLite

**Usage:**
```bash
./bench.py --joins                    # Default: 10K messages, 100 iterations
./bench.py --joins --size=50000      # Custom dataset
./bench.py --joins --iterations=200  # More iterations for stability
```

**Output Example:**
```
┌─────────────────────────────────┬──────────────┬──────────────┬──────────────┐
│ JOIN Operation                  │ Storm ORM    │ Raw SQLite   │ Efficiency   │
├─────────────────────────────────┼──────────────┼──────────────┼──────────────┤
│ INNER JOIN (single FK)          │       6.63M │       9.68M │       68.4% │
│ INNER JOIN (multi FK)           │       4.27M │       6.42M │       66.4% │
└─────────────────────────────────┴──────────────┴──────────────┴──────────────┘
```

**Color Coding:**
- 🟢 Green (≥70%) - Excellent performance
- 🟡 Yellow (50-70%) - Acceptable performance
- 🔴 Red (<50%) - Needs optimization

---

### 2. CRUD Operations (`--compare`)

**Purpose:** Comprehensive CRUD performance comparison

**What it measures:**
- Single INSERT (10K operations)
- Batch INSERT (10K operations, batch size 1000)
- Single DELETE (10K operations)
- Bulk DELETE (10K operations)
- Single UPDATE (1K operations)
- Batch UPDATE (1K operations)
- SELECT (10K rows)

**Compares against:**
- Raw SQLite (prepared statements)
- sqlite_orm (v1.9.1)
- Storm ORM

**Usage:**
```bash
./bench.py --compare               # Run CRUD suite (builds benchmarks automatically)
```

**Performance Tiers:**
- 🟢 ≥10M ops/sec - Excellent
- 🔵 ≥1M ops/sec - Good
- 🟡 ≥500K ops/sec - Acceptable
- 🔴 <500K ops/sec - Needs improvement

---

### 3. SQL Generation (`--sql-gen`)

**Purpose:** Analyze compile-time SQL generation and caching

**What it measures:**
- SQL generation time for various batch sizes (1-1000)
- Cache hit/miss patterns
- Thread-local cache effectiveness
- Speedup from caching

**Usage:**
```bash
./bench.py --sql-gen               # Run analysis
```

**Key Metrics:**
- Batch sizes 1, 10, 25, 50 (common sizes, optimized for cache)
- Cache hit: <50μs (excellent)
- Cache miss: 50-200μs (acceptable for small batches)
- Cache miss: >200μs (expected for large batches)

**Optimizations Analyzed:**
- ✓ Compile-time SQL prefix generation
- ✓ Pre-computed field names and placeholders
- ✓ Thread-local 8-entry cache
- ✓ Memory pre-allocation
- ✓ Value template reuse

---

### 4. All Benchmarks (`--all`)

**Purpose:** Run complete benchmark suite

**Includes:**
1. JOIN performance benchmarks
2. SQL generation microbenchmarks
3. JOIN detailed microbenchmarks
4. SELECT performance comparison

**Usage:**
```bash
./bench.py --all                   # Default settings
./bench.py --all --size=5000       # Custom dataset size
```

---

## Understanding Results

### JOIN Efficiency

**Formula:** `(Storm ORM throughput / Raw SQLite throughput) * 100`

**Interpretation:**
- **≥70%:** Excellent - minimal ORM overhead
- **50-70%:** Good - acceptable overhead for ORM benefits
- **<50%:** Investigate - possible optimization opportunities

**Typical Results:**
- INNER JOIN: 65-70% (compile-time SQL optimization working)
- LEFT JOIN: 72-76% (good efficiency)
- RIGHT JOIN: 75-85% (excellent efficiency)

### CRUD Performance

**Baseline:** sqlite_orm is set as 100% (ORM comparison baseline)

**Storm ORM Target:**
- Overall performance: ≥200% of sqlite_orm
- Single INSERT: ≥2x faster
- Batch INSERT: ≥6x faster
- DELETE: ≥35x faster
- SELECT: ≥1.5x faster

**Key Metrics:**
- **Single operations:** Tests prepared statement caching
- **Batch operations:** Tests bulk optimization and transactions
- **Overall %:** Average across all 7 metrics

### SQL Generation

**Cache Hit Patterns:**
- Batch sizes 1, 10, 25, 50: Nearly 100% cache hits
- Other sizes: May miss initially, then cached
- Large batches (>100): Unlikely to be cached (less common)

**Performance Expectations:**
- Cached: <50μs (excellent)
- First generation: 50-200μs (acceptable)
- Large batches (1000+): 500μs+ (expected)

**Speedup Analysis:**
- High speedup (>10x): Excellent cache effectiveness
- Medium speedup (2-10x): Good caching
- Low speedup (<2x): May indicate cache thrashing

---

## Best Practices

### Routine Testing

For day-to-day development:
```bash
./bench.py --joins                 # Quick JOIN performance check
```

### Pre-Commit Verification

Before committing performance-critical changes:
```bash
./bench.py --all --size=10000      # Comprehensive suite
```

### Release Validation

For release builds:
```bash
# Build release benchmarks (bench.py builds automatically)
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Run full suite
./bench.py --all --size=10000 --iterations=100
```

### Performance Regression Detection

Compare results across commits:
```bash
# Baseline (develop branch)
./bench.py --joins > baseline.txt

# After changes
./bench.py --joins > optimized.txt

# Compare
diff baseline.txt optimized.txt
```

---

## Troubleshooting

### Missing Benchmarks

**Error:** `Benchmark binary not found`

**Solution:**
```bash
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
```

### Low Performance

**INNER JOIN <50% efficiency:**
1. Check if using Release build
2. Verify compile-time SQL optimizations are enabled
3. Check for statement caching issues

**CRUD operations slower than expected:**
1. Ensure Release build (`-O2` or higher)
2. Check SQLite is using `SQLITE_OPEN_FULLMUTEX`
3. Verify thread-local caching is working

### Build Issues

**Module cache corruption:**
```bash
# Simply retry the build
cmake --build --preset ninja-release
```

**Clang-scan-deps issues:**
```bash
# Build Storm library first
cmake --build --preset ninja-release --target storm
# Then build benchmarks
cmake --build --preset ninja-release --target bench_storm
```

---

## Advanced Usage

### Custom Binary Paths

```bash
./bench.py --joins --binary=/custom/path/to/bench_join_performance
```

### Benchmark-Specific Options

**JOIN benchmark:**
```bash
./scripts/bench/join.py --size=100000 --iterations=500
```

**CRUD benchmark:**
```bash
./scripts/bench/compare.py --compare  # Builds automatically
```

**SQL generation:**
```bash
./scripts/bench/sql_gen.py       # Uses debug build by default
```

---

## Performance Goals

### Current Achievements (as of Oct 2025)

**CRUD Operations:**
- Single INSERT: 992K/sec (2.0x vs sqlite_orm)
- Batch INSERT: 2.7M/sec (6.4x vs sqlite_orm)
- Single DELETE: 21.6M/sec (36.6x vs sqlite_orm, 73% of raw SQLite)
- SELECT: 13.07M rows/sec (1.51x vs sqlite_orm, 74% of raw SQLite)

**JOIN Operations:**
- INNER JOIN efficiency: 65-68%
- LEFT JOIN efficiency: 72-76%
- RIGHT JOIN efficiency: 75-85%
- Average overall: 71-74%

**SQL Generation:**
- Cached (common sizes): <50μs
- Cache miss (small batch): 50-200μs
- Cache effectiveness: 90%+ for common patterns

### Future Targets

- JOIN INNER efficiency: ≥75%
- CRUD overall: ≥250% of sqlite_orm
- SQL generation: 95%+ cache hit rate
- SELECT: ≥80% of raw SQLite

---

## Contributing

When adding new benchmarks:

1. Inherit from `BenchmarkRunner` (see `scripts/bench/common.py`)
2. Implement `parse_results()` and `display_results()`
3. Use `BenchmarkTable` or `FlexibleTable` for consistent formatting
4. Add dispatcher function to `bench.py`
5. Update this documentation

Example:
```python
from common import BenchmarkRunner, BenchmarkTable

class MyBenchmark(BenchmarkRunner):
    def parse_results(self, output):
        # Extract metrics from output
        return {'metric': value}

    def display_results(self, data, **kwargs):
        # Use BenchmarkTable for formatting
        BenchmarkTable.print_header("My Operation")
        # ...
```

---

## See Also

- `scripts/bench/README.md` - Technical implementation details
- `CLAUDE.md` - Storm ORM architecture and patterns
- Individual benchmark source files for implementation details
