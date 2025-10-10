# Storm ORM Benchmark Suite - Developer Guide

Technical documentation for extending and maintaining the Storm ORM benchmark infrastructure.

> **For users:** See [BENCHMARKS.md](../../BENCHMARKS.md) for how to run benchmarks and interpret results.

## Architecture

### Structure

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
├── compare.py        # CRUD operations comparison
├── sql_gen.py        # SQL generation analysis
├── test_tables.py    # Table rendering tests
└── README.md         # This file (developer guide)
```

### Design Principles

1. **DRY (Don't Repeat Yourself):** All benchmarks share common code via `BenchmarkRunner` base class
2. **Consistent Formatting:** Use `BenchmarkTable` (4-column) or `FlexibleTable` (N-column) for uniform output
3. **ANSI-Aware:** Handle color codes properly in all string operations
4. **Unified Entry Point:** `bench.py` dispatches to individual benchmark scripts

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

## Testing

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

## Implementation Checklist

When adding a new benchmark:

1. ✅ Create class inheriting from `BenchmarkRunner`
2. ✅ Implement `parse_results()` - extract metrics from binary output
3. ✅ Implement `display_results()` - format using `BenchmarkTable` or `FlexibleTable`
4. ✅ Add dispatcher function to `bench.py`
5. ✅ Add command-line argument in `bench.py` main()
6. ✅ Update `BENCHMARKS.md` with user-facing documentation
7. ✅ Test with `test_tables.py` and manual runs

## Color Coding Reference

Use `Colors` class from `common.py` for consistent color coding:

```python
from common import Colors

# Efficiency thresholds
if efficiency >= 70:
    color = Colors.GREEN    # Excellent
elif efficiency >= 50:
    color = Colors.YELLOW   # Acceptable
else:
    color = Colors.RED      # Needs optimization
```

**Color Constants:**
- `Colors.GREEN` - ≥70% efficiency (excellent)
- `Colors.YELLOW` - 50-70% efficiency (acceptable)
- `Colors.RED` - <50% efficiency (needs optimization)
- `Colors.BLUE` - ≥1M ops/sec (good performance tier)
- `Colors.CYAN` - ≥500K ops/sec (acceptable performance tier)
- `Colors.NC` - Reset to default (always use after colored text)
