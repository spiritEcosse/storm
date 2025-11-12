# Storm ORM Performance Results

All benchmarks performed on 10,000 operations with 100 iterations in Release builds using **warm statement cache** (production-realistic scenario).

## CRUD Operations

| Operation | Storm ORM | Raw SQLite | sqlite_orm | Storm vs Raw | Storm vs sqlite_orm |
|-----------|-----------|------------|------------|--------------|---------------------|
| INSERT (single) | 23.67M/sec | 49M/sec | 979K/sec | 48% | 24x faster |
| INSERT (batch) | 3.04M/sec | - | 810K/sec | - | 3.75x faster |
| SELECT (all rows) | 13.28M/sec | 17.67M/sec | 9.21M/sec | 75% | 1.44x faster |
| UPDATE (single) | 16.23M/sec | 1.09M/sec | 909K/sec | 1489% | 18x faster |
| UPDATE (batch) | 15.15M/sec | - | 490K/sec | - | 31x faster |
| DELETE (single) | 31.51M/sec | 29.4M/sec | 945K/sec | 107% | 33x faster |
| DELETE (batch) | 32.26M/sec | - | 957K/sec | - | 34x faster |

**Key Observations:**
- **INSERT**: 24x faster than sqlite_orm, statement caching provides 24x speedup over cold starts
- **SELECT**: 75% of raw SQLite efficiency, 1.44x faster than sqlite_orm
- **UPDATE**: 18x faster than sqlite_orm, exceeds raw SQLite due to optimized caching
- **DELETE**: Exceeds raw SQLite performance (107%), 33x faster than sqlite_orm
- **Warm Cache**: All measurements include 10-operation warmup to reflect production usage

## JOIN Operations

| JOIN Operation | Storm ORM | Raw SQLite | Efficiency |
|----------------|-----------|------------|------------|
| Simple SELECT (no JOIN) | 8.4M rows/sec | - | - |
| RIGHT JOIN (single FK) | 4.5M rows/sec | 5.0M rows/sec | 90% |
| RIGHT JOIN (multi FK) | 2.7M rows/sec | 3.2M rows/sec | 84% |
| LEFT JOIN (single FK) | 6.1M rows/sec | 6.8M rows/sec | 90% |
| LEFT JOIN (multi FK) | 3.9M rows/sec | 5.2M rows/sec | 75% |
| INNER JOIN (single FK) | 4.4M rows/sec | 7.4M rows/sec | 59% |
| INNER JOIN (multi FK) | 3.7M rows/sec | 6.0M rows/sec | 62% |

**Average Efficiency: ~77%** - Storm ORM achieves 77% of raw SQLite performance for JOIN operations.

**Key Observations:**
- **LEFT/RIGHT JOINs**: 75-90% efficiency (excellent)
- **INNER JOINs**: 59-62% efficiency (good, lower due to more complex object construction)
- **Single FK vs Multi FK**: Multi-FK JOINs slower due to more column extraction

See [JOIN Analysis](join-analysis.md) for detailed performance breakdown.

## DISTINCT Operations

| Operation | Storm ORM | Raw SQLite | Efficiency |
|-----------|-----------|------------|------------|
| DISTINCT name (string) | 140,507 rows/sec | 147,083 rows/sec | 95% |
| DISTINCT age (int) | 148,841 rows/sec | 141,179 rows/sec | 105% |
| DISTINCT id/PK (int) | 35.8M rows/sec | 36.2M rows/sec | 99% |
| DISTINCT (name, age) | 132,336 rows/sec | 131,125 rows/sec | 101% |

**Average Efficiency: ~100%** - Storm ORM achieves parity with raw SQLite for DISTINCT operations!

See [DISTINCT Analysis](distinct-analysis.md) for implementation details.

## Performance by Feature

### Statement-Level Caching

Unified caching pattern across UPDATE/DELETE/SELECT operations:

| Operation | Cold Start (First Op) | Warm Cache (Subsequent Ops) | Speedup |
|-----------|----------------------|----------------------------|---------|
| DELETE | 947K/sec | 31.51M/sec | 33x |
| UPDATE | ~1M/sec | 16.23M/sec | 16x |
| INSERT | ~1M/sec | 23.67M/sec | 24x |
| SELECT | ~2M/sec | 13.28M/sec | 6.6x |

**Note**: Cold start includes statement preparation overhead. Warm cache represents typical production performance.

### Thread-Local SQL Caching

Bulk INSERT SQL generation optimization:

| Batch Size | Without Cache | With Cache | Improvement |
|------------|--------------|------------|-------------|
| 1 (cached) | 0.253µs | 0.016µs | 94% |
| 10 (cached) | 1.2µs | 0.045µs | 96% |
| 50 (cached) | 5.8µs | 0.180µs | 97% |

**Cache hit rate**: >90% for common batch sizes (1, 10, 25, 50)

### Compile-Time Optimizations

Impact of compile-time SQL generation and index sequence optimization:

- **Compile-time SQL**: Zero runtime SQL generation overhead
- **Index sequences**: Reduced template depth, faster compilation
- **Field binding**: Fully inlined with `if constexpr` type dispatch

## Running Benchmarks

See [BENCHMARKS.md](../../BENCHMARKS.md) for comprehensive benchmarking guide.

### Quick Commands

```bash
# Python-based (recommended)
python3 bench.py --compare       # All CRUD operations
python3 bench.py --joins         # JOIN performance
python3 bench.py --all           # Complete suite

# Manual C++ benchmarks
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_join --help
```

## Performance Goals

- ✅ **CRUD Operations (warm cache)**: 48-107% of raw SQLite efficiency
- ✅ **JOIN Operations**: 77% average efficiency
- ✅ **DISTINCT Operations**: 100% efficiency (parity)
- ✅ **Statement Caching**: 6-33x speedup for repeated operations
- ✅ **vs sqlite_orm**: 1.44-34x faster across all operations (warm cache)

## Key Performance Innovations

1. **Compile-time SQL generation** - Zero runtime overhead
2. **Statement-level caching** - 6-33x speedup (warm cache vs cold start)
3. **Thread-local SQL caching** - 94% improvement for bulk operations
4. **Index sequence optimization** - Reduced template depth, faster compilation
5. **Optimized row extraction** - resize() pre-allocation, direct string construction
6. **Fully inlined field binding** - `if constexpr` type dispatch with zero overhead
7. **Warm cache benchmarking** - Production-realistic measurements with 10-op warmup
