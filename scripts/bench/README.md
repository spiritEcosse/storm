# Storm ORM Benchmark Suite

Modern Python-based benchmark infrastructure for Storm ORM performance testing.

## Structure

```
scripts/bench/
├── common.py         # Base classes and utilities
│   ├── Colors            # ANSI color codes
│   ├── BenchmarkTable    # Reusable table formatting
│   └── BenchmarkRunner   # Abstract benchmark runner
├── main.py           # Unified entry point
├── all.py            # Comprehensive benchmark suite
├── join.py           # JOIN performance benchmarks
├── crud.py           # CRUD operations benchmark
├── sql_gen.py        # SQL generation analysis
└── README.md         # This file
```

## Quick Start

From project root:

```bash
# Convenient wrapper (recommended)
./bench --joins                    # Run JOIN benchmarks
./bench --crud                     # Run CRUD benchmarks
./bench --sql-gen                  # Run SQL generation analysis
./bench --all                      # Run all benchmarks

# Custom parameters
./bench --joins --size=50000       # Custom dataset size
./bench --joins --iterations=200   # More iterations
./bench --all --size=5000          # All benchmarks with 5K dataset

# Direct script execution
./scripts/bench/join.py            # JOIN benchmark
./scripts/bench/crud.py            # CRUD benchmark
./scripts/bench/sql_gen.py         # SQL generation analysis
./scripts/bench/main.py --joins    # Via main entry point
```

## Usage

### JOIN Benchmarks

```bash
./bench --joins                            # Default: 10k messages, 100 iterations
./bench --joins --size=50000              # Custom message count
./bench --joins --iterations=200          # More iterations
./bench --joins --size=10000 --iterations=100
```

### CRUD Benchmarks

```bash
./bench --crud                             # Default CRUD suite
./scripts/bench/crud.py --build           # Build first, then run
```

**Measures:**
- Single INSERT (10K operations)
- Batch INSERT (10K operations, batch size 1000)
- Single DELETE (10K operations)
- Bulk DELETE (10K operations)
- Single UPDATE (1K operations)
- Batch UPDATE (1K operations)
- SELECT (10K rows)

**Compares:** Raw SQLite, sqlite_orm, Storm ORM

### SQL Generation Benchmarks

```bash
./bench --sql-gen                          # SQL generation analysis
./scripts/bench/sql_gen.py                # Direct execution
```

**Analyzes:**
- Compile-time SQL generation performance
- Thread-local cache effectiveness
- Batch size impact (1-1000)
- Cache hit/miss patterns

### All Benchmarks

```bash
./bench --all                              # Run complete suite
./bench --all --size=5000                  # Custom dataset
```

## Adding New Benchmarks

### 1. Create Benchmark Class

Inherit from `BenchmarkRunner`:

```python
from common import BenchmarkRunner, BenchmarkTable

class MyBenchmark(BenchmarkRunner):
    def parse_results(self, output):
        # Parse benchmark output
        return {...}

    def display_results(self, data, **kwargs):
        # Use BenchmarkTable for formatting
        BenchmarkTable.print_header("My Operation")
        # ...
        BenchmarkTable.print_footer()
```

### 2. Add to Main Entry Point

Edit `main.py`:

```python
def run_my_benchmark(args):
    from my_benchmark import MyBenchmark
    benchmark = MyBenchmark(args.binary)
    benchmark.run(...)
```

### 3. Register Command

Add argument in `main()`:

```python
commands.add_argument('--my-bench', action='store_true',
                     help='Run my benchmark')
```

## Reusable Components

### BenchmarkTable

Provides consistent table formatting across all benchmarks:

```python
from common import BenchmarkTable

# Print header
BenchmarkTable.print_header("Operation Name")

# Print data rows
BenchmarkTable.print_row("Test 1", storm_value, raw_value, efficiency)

# Print separator
BenchmarkTable.print_separator()

# Print footer
BenchmarkTable.print_footer()

# Print summary
BenchmarkTable.print_summary(messages, iterations, avg_efficiency)
```

### BenchmarkRunner

Abstract base class for all benchmarks:

```python
from common import BenchmarkRunner

class MyBench(BenchmarkRunner):
    def __init__(self, binary_path='./path/to/binary'):
        super().__init__(binary_path)

    def parse_results(self, output):
        # Required: parse benchmark output
        pass

    def display_results(self, data, **kwargs):
        # Required: display formatted results
        pass
```

## Color Coding

Efficiency percentages are automatically color-coded:
- 🟢 **Green** (≥70%) - Excellent performance
- 🟡 **Yellow** (50-70%) - Acceptable performance
- 🔴 **Red** (<50%) - Needs optimization

## Development

### Testing

```bash
# Test JOIN benchmark
./scripts/bench/join.py --help
./scripts/bench/join.py

# Test main entry point
./scripts/bench/main.py --help
./scripts/bench/main.py --joins
```

### Extending

All benchmark scripts follow the same pattern:
1. Inherit from `BenchmarkRunner`
2. Implement `parse_results()` and `display_results()`
3. Use `BenchmarkTable` for consistent formatting
4. Register in `main.py`
