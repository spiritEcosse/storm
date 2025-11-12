# Getting Started with Storm ORM Development

## Prerequisites

- **Custom Clang with C++26 reflection support** (located at `../clang-p2996/`)
- **SQLite3 development libraries**
- **CMake 3.30+**
- **Ninja build system**

## Standard Development Build

```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release

# Code formatting
cmake --build --preset ninja-debug --target format
cmake --build --preset ninja-debug --target format-check
```

## Sanitizer Builds

```bash
# Address + Leak sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug

# Thread sanitizer
cmake --preset ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread"
cmake --build --preset ninja-debug
```

## Running Tests

```bash
# Run all tests
ctest --test-dir build/debug --output-on-failure

# Run specific test
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"

# Run with verbose output
./build/debug/tests/storm_tests --gtest_verbose
```

## Benchmarking

**⚠️ IMPORTANT: Always use Release builds for accurate performance measurements!**

Debug builds are 2-3x slower than Release builds.

### Quick Benchmarking with Python

```bash
# Python-based benchmark suite (RECOMMENDED - with auto-rebuild)
python3 bench.py --joins                # JOIN performance analysis
python3 bench.py --joins --messages=10000  # Custom dataset size
python3 bench.py --all                  # All microbenchmarks
python3 bench.py --compare              # Comprehensive Storm vs sqlite_orm vs Raw SQLite
```

**Python Benchmark Features:**
- ✅ **Auto-rebuild**: Detects source changes and rebuilds automatically
- ✅ **Formatted output**: Color-coded tables with efficiency percentages
- ✅ **Flexible**: Control dataset size and iteration count
- ✅ **Fast**: Skips rebuild when nothing changed

### Manual Benchmarking

```bash
# Build benchmarks
cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Run specific benchmarks
./build/release/benchmarks/bench_storm
./build/release/benchmarks/bench_sqlite_orm
./build/release/benchmarks/bench_sqlite

# JOIN benchmarks with flags
./build/release/benchmarks/bench_join --help
./build/release/benchmarks/bench_join --size=10000 --iterations=100
```

See [BENCHMARKS.md](../../BENCHMARKS.md) for comprehensive benchmarking guide.

## Common Build Issues

### Module Cache Corruption

**Symptom**: Build fails with error: `module '_Builtin_stdint' is defined in both [same_path] and [same_path]`

**Solution**: Simply run the build command again - second attempt usually succeeds.

```bash
# If build fails with module cache error:
ninja storm_tests  # Will fail
ninja storm_tests  # Will succeed on second try
```

**Nuclear option** (if repeated attempts fail):
```bash
rm -rf ~/.cache/clang/ModuleCache
ninja storm_tests
```

See [Compiler Issues Reference](../reference/compiler-issues.md) for more known issues and workarounds.

## Git Workflow

```bash
# Update feature branch
git fetch origin && git merge origin/develop

# Resolve conflicts if needed
git add . && git commit

# Test before pushing
cmake --build --preset ninja-debug && ctest --test-dir build/debug
git push
```

**Best practices:**
- Clean working directory before starting
- Short-lived feature branches
- Thorough testing before commits
- Descriptive commit messages

## Next Steps

- Read [Common Development Tasks](common-tasks.md) for implementation patterns
- Review [Performance Guidelines](performance-guidelines.md) for optimization strategies
- Check [Testing Strategy](testing.md) for comprehensive testing approaches
