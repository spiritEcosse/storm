# JOIN Performance Analysis & Implementation

> **Note (2026-04-18, #194):** The QueryBenchmark collapse unified the raw-SQLite
> side to generate SQL via `QuerySet::sql()` instead of hand-written strings.
> Raw JOINs now carry Storm's `t1`/`t2` table aliases instead of the previous
> hand-written `fm`/`u` aliases. This keeps the comparison fair (one source of
> truth for both sides) but may shift JOIN latency numbers by a few percent vs.
> the values recorded below. Numbers in the table predate the change and will
> be refreshed after the smoke test regression is resolved and `--thorough`
> re-runs land.

## Performance Results

All benchmarks performed on 10,000 rows with 100 iterations in Release builds.

| JOIN Operation | Storm ORM | Raw SQLite | Efficiency |
|----------------|-----------|------------|------------|
| Simple SELECT (no JOIN) | 8.4M rows/sec | - | - |
| LEFT JOIN (single FK) | 6.1M rows/sec | 6.8M rows/sec | 90% |
| LEFT JOIN (multi FK) | 3.9M rows/sec | 5.2M rows/sec | 75% |
| INNER JOIN (single FK) | 4.4M rows/sec | 7.4M rows/sec | 59% |
| INNER JOIN (multi FK) | 3.7M rows/sec | 6.0M rows/sec | 62% |

**Average Efficiency: ~77%** - Storm ORM achieves 77% of raw SQLite performance for JOIN operations.

## Key Observations

- **LEFT JOINs**: 75-90% efficiency (excellent)
- **INNER JOINs**: 59-62% efficiency (good, lower due to more complex object construction)
- **Single FK vs Multi FK**: Multi-FK JOINs slower due to more column extraction and object population

## Architecture

See [Design Decisions](../architecture/DESIGN_DECISIONS.md#11-join-architecture-type-erased-sql-builder-pattern) for detailed architecture.

**Key Components:**
- Abstract base class (`IJoinStatement`) for type erasure
- Variadic template for single and multi-FK support
- Compile-time SQL generation with fold expressions
- JOIN and simple SELECT share the single Connection-level statement cache (distinct SQL text → distinct cache entries)

## Optimization Attempt: Template-Based Compile-Time JOIN

We attempted to eliminate function pointer overhead by using templates to preserve FK field pointer information through the entire call chain, allowing perfect inlining:

```cpp
// Attempted approach: Template parameters preserve FK info
template <JoinType Type, auto... FKFieldPtrs>
auto execute_optimized_join() noexcept -> std::expected<std::vector<T>, Error> {
    using JS = JoinStatement<T, ConnType, Type, FKFieldPtrs...>;
    // Direct static call: JS::extract_joined_row(stmt, obj)
    // Should enable perfect inlining without function pointers
}
```

### Results: Templates Made It WORSE

- **Function pointer approach (current)**: 6.9M rows/sec (70% of raw) ✅ **BEST**
- **Template approach (attempted)**: 4.9M rows/sec (49% of raw) ❌ **28% SLOWER**

### Why Templates Failed

1. **Code bloat**: Template instantiation for each JOIN configuration created more code → worse instruction cache locality
2. **Compiler optimization surprise**: Modern compilers (Clang 21) optimize indirect function calls better than expected with profile-guided optimization
3. **Inlining limits**: Even with templates, deep call chains with complex reflection operations hit compiler inlining budget limits
4. **Register pressure**: Fully inlined template code increased register spilling in the hot loop

## Real Performance Bottlenecks (NOT Function Pointers!)

### Measured Overhead Breakdown (profiling data)

- **SQL execution**: ~20% (unavoidable, same as raw SQLite)
- **Row stepping**: ~5% (minimal overhead)
- **Column extraction**: ~35% (type checks + conversion)
- **String allocation**: ~30% (heap allocations for TEXT fields)
- **Object construction**: ~10% (calling constructors, field assignment)

### Identified Bottlenecks

1. **String allocations**: Each `std::string` field requires heap allocation (~30-40% of runtime)
2. **Object construction**: Creating and populating complex objects with multiple fields
3. **Vector management**: Resizing, copying, moving objects in result vectors
4. **Multi-column extraction**: 5-8 columns for JOIN vs 2-3 for simple SELECT
5. **Type dispatch overhead**: Runtime `if constexpr` type checks for each field

## Recommendations for Future Optimization

Current 50-70% of raw SQLite performance is **respectable for a full ORM** with reflection-based mapping. Further gains require architectural changes:

### 1. String Handling (Biggest Potential Win)

- Use `std::string_view` for read-only operations (eliminate allocation)
- Implement move semantics throughout extraction chain
- Consider string interning for repeated values
- Arena allocator for temporary strings

### 2. Memory Management

- Object pooling to reuse allocated objects
- Custom allocator optimized for ORM access patterns
- Better size estimation (currently pre-allocates 10K, may overshoot)

### 3. Column Extraction Optimization

- Batch extraction by type (extract all ints, then all strings)
- SIMD for type checking and conversion
- Specialized fast paths for common type combinations

### 4. Caching Improvements

- Cache field offset calculations
- Reuse extraction buffers across queries
- Pre-build type dispatch tables at compile-time

## Key Lesson

**Don't assume eliminating indirect calls will improve performance. Profile first, optimize second.**

The current function-pointer based implementation strikes a good balance between flexibility and performance. Modern compilers are surprisingly good at optimizing indirect calls, and template-based "perfect inlining" can actually hurt performance due to code bloat and instruction cache pressure.

## Running JOIN Benchmarks

```bash
# Python-based (recommended)
python3 bench.py --joins                 # Default: 1000 messages
python3 bench.py --joins --messages=10000  # Custom size

# C++ benchmarks with flags
./build/release/benchmarks/bench_join --help
./build/release/benchmarks/bench_join --size=10000 --iterations=100
./build/release/benchmarks/bench_join --storm-join-1 --size=10000
./build/release/benchmarks/bench_join --storm-join-multi --size=10000
./build/release/benchmarks/bench_join --storm-join-1 --raw-join-1 --size=10000
```

See [benchmarks/README.md](https://github.com/spiritEcosse/storm/blob/develop/benchmarks/README.md) for comprehensive guide.
