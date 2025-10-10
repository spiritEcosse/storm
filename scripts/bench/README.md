# Storm ORM Benchmark Suite

Modern Python-based benchmark infrastructure for Storm ORM performance testing.

## Structure

```
bench.py (root)       # Unified entry point for all benchmarks
scripts/bench/
├── common.py         # Base classes and utilities
│   ├── Colors            # ANSI color codes
│   ├── BenchmarkTable    # 4-column table formatting
│   ├── FlexibleTable     # N-column table formatting
│   ├── BenchmarkRunner   # Abstract benchmark runner
│   ├── get_visible_length()  # ANSI-aware string utilities
│   └── pad_string()
├── all.py            # Comprehensive benchmark suite
├── join.py           # JOIN performance benchmarks
├── bench_compare.py  # CRUD operations comparison
├── sql_gen.py        # SQL generation analysis
├── test_tables.py    # Table rendering tests
└── README.md         # This file
```

## Quick Start

From project root:

```bash
# Unified entry point (recommended)
./bench.py --joins                 # Run JOIN benchmarks
./bench.py --perf_compare          # Run CRUD performance comparison
./bench.py --sql-gen               # Run SQL generation analysis
./bench.py --all                   # Run all benchmarks

# Custom parameters
./bench.py --joins --size=50000       # Custom dataset size
./bench.py --joins --iterations=200   # More iterations
./bench.py --all --size=5000          # All benchmarks with 5K dataset

# Direct script execution
./scripts/bench/join.py               # JOIN benchmark
./scripts/bench/bench_compare.py --perf_compare  # CRUD comparison
./scripts/bench/sql_gen.py            # SQL generation analysis
```

## Usage

### JOIN Benchmarks

```bash
./bench.py --joins                            # Default: 10k messages, 100 iterations
./bench.py --joins --size=50000              # Custom message count
./bench.py --joins --iterations=200          # More iterations
./bench.py --joins --size=10000 --iterations=100
```

### CRUD Benchmarks

```bash
./bench.py --perf_compare                     # Full CRUD comparison (builds automatically)
./scripts/bench/bench_compare.py --perf_compare   # Direct execution
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
./bench.py --sql-gen                       # SQL generation analysis
./scripts/bench/sql_gen.py                 # Direct execution
```

**Analyzes:**
- Compile-time SQL generation performance
- Thread-local cache effectiveness
- Batch size impact (1-1000)
- Cache hit/miss patterns

### All Benchmarks

```bash
./bench.py --all                           # Run complete suite
./bench.py --all --size=5000               # Custom dataset
```

## Adding New Benchmarks

### 1. Create Benchmark Class

Inherit from `BenchmarkRunner`:

```python
from common import BenchmarkRunner, BenchmarkTable, FlexibleTable

class MyBenchmark(BenchmarkRunner):
    def parse_results(self, output):
        # Parse benchmark output
        return {...}

    def display_results(self, data, **kwargs):
        # Use BenchmarkTable (4-column) or FlexibleTable (N-column)
        BenchmarkTable.print_header("My Operation")
        # ...
        BenchmarkTable.print_footer()
```

### 2. Add to Main Entry Point

Edit `bench.py` in project root:

```python
def run_my_benchmark(args):
    from my_benchmark import MyBenchmark
    benchmark = MyBenchmark(args.binary)
    benchmark.run(...)
```

### 3. Register Command

Add argument in `main()` in `bench.py`:

```python
commands.add_argument('--my-bench', action='store_true',
                     help='Run my benchmark')
```

## Reusable Components

### BenchmarkTable (4-column format)

Provides consistent 4-column table formatting for JOIN benchmarks:

```python
from common import BenchmarkTable

# Print header
BenchmarkTable.print_header("Operation Name")

# Print data rows (operation, storm_throughput, raw_throughput, efficiency)
BenchmarkTable.print_row("Test 1", storm_value, raw_value, efficiency)

# Print separator
BenchmarkTable.print_separator()

# Print footer
BenchmarkTable.print_footer()

# Print summary
BenchmarkTable.print_summary(messages, iterations, avg_efficiency)
```

### FlexibleTable (N-column format)

Provides flexible N-column table formatting for performance comparison:

```python
from common import FlexibleTable

# Create table with custom headers and widths
table = FlexibleTable(
    headers=['Benchmark', 'INSERT', 'DELETE', 'SELECT'],
    column_widths=[35, 18, 18, 18]
)

# Print header
table.print_header()

# Print rows (automatically handles ANSI color codes)
table.print_row(['Storm ORM', '992K/sec', '21.6M/sec', '13.1M/sec'])

# Print footer
table.print_footer()
```

### ANSI-Aware String Utilities

```python
from common import get_visible_length, pad_string

# Get visible length without ANSI codes
colored_text = f"{Colors.GREEN}Success{Colors.NC}"
length = get_visible_length(colored_text)  # Returns 7, not 17

# Pad string accounting for ANSI codes
padded = pad_string(colored_text, 20)  # Pads to exactly 20 visible chars
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
# Test table rendering
./scripts/bench/test_tables.py

# Test individual benchmarks
./scripts/bench/join.py --help
./scripts/bench/join.py

# Test unified entry point
./bench.py --help
./bench.py --joins
```

### Extending

All benchmark scripts follow the same pattern:
1. Inherit from `BenchmarkRunner`
2. Implement `parse_results()` and `display_results()`
3. Use `BenchmarkTable` (4-column) or `FlexibleTable` (N-column) for formatting
4. Register dispatcher function in `bench.py`
