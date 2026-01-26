# Code Coverage Guide

This guide explains how to generate and analyze code coverage for Storm ORM.

## Quick Start

### Console Summary (Fastest)

```bash
# Configure coverage build (one-time)
cmake -S . -B build/coverage -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=ON \
    -DENABLE_COVERAGE=ON

# Build and run tests with coverage
cmake --build build/coverage
cmake --build build/coverage --target coverage-filtered
```

Output shows filtered coverage with `LCOV_EXCL_*` markers applied:
```
Summary coverage rate:
  source files: 14
  lines.......: 98.0% (3462 of 3531 lines)
  functions...: 78.3% (3969 of 5071 functions)
  branches....: 89.5% (342 of 382 branches)
```

### HTML Report (Detailed)

```bash
cmake --build build/coverage --target coverage-filtered-html
```

Open `build/coverage/coverage/html-filtered/index.html` in browser.

## Available Targets

| Target | Description |
|--------|-------------|
| `coverage` | Run ALL tests and generate HTML report |
| `coverage-quick` | Run ALL tests and show console summary |
| `coverage-filtered` | **Recommended** - Apply `LCOV_EXCL_*` markers and show summary |
| `coverage-filtered-html` | Generate HTML with exclusions applied |
| `coverage-run` | Run ALL tests (main + mock) |
| `coverage-run-main` | Run main tests only |
| `coverage-run-mock` | Run mock tests only (uses LD_PRELOAD) |
| `coverage-report` | Generate HTML report (after coverage-run) |
| `coverage-summary` | Show text summary (after coverage-run) |
| `coverage-lcov` | Export LCOV format (for CI) |
| `coverage-clean` | Clean all coverage data |

## Recommended Workflow

### Development (Quick Check)

```bash
# After making changes, run filtered coverage
cmake --build build/coverage --target coverage-filtered
```

### Before Commit (Detailed Review)

```bash
# Generate HTML to review uncovered lines
cmake --build build/coverage --target coverage-filtered-html

# Open in browser
xdg-open build/coverage/coverage/html-filtered/index.html  # Linux
open build/coverage/coverage/html-filtered/index.html      # macOS
```

### CI Pipeline

```bash
# Export LCOV format for coverage services (Codecov, Coveralls, etc.)
cmake --build build/coverage --target coverage-lcov

# The filtered lcov file is at:
# build/coverage/coverage/coverage-filtered.lcov
```

## Excluding Code from Coverage

Use `LCOV_EXCL_*` markers to exclude compile-time only code:

```cpp
// LCOV_EXCL_START - compile-time only
consteval auto build_sql() {
    // This code runs at compile-time, not runtime
    return ConstexprString{"SELECT * FROM table"};
}
// LCOV_EXCL_STOP

// Single line exclusion
constexpr auto value = compute_value(); // LCOV_EXCL_LINE
```

### Supported Markers

| Marker | Description |
|--------|-------------|
| `LCOV_EXCL_START` | Begin excluded block |
| `LCOV_EXCL_STOP` | End excluded block |
| `LCOV_EXCL_LINE` | Exclude single line |
| `LCOV_EXCL_BR_START` | Begin branch exclusion |
| `LCOV_EXCL_BR_STOP` | End branch exclusion |
| `LCOV_EXCL_BR_LINE` | Exclude branch on single line |

### Why Custom Filtering?

Storm uses **llvm-cov** (not gcov) for coverage. llvm-cov doesn't process `LCOV_EXCL_*` markers natively - they're a gcov/lcov convention.

We use a custom script (`scripts/filter_lcov_excl.py`) to:
1. Parse source files for `LCOV_EXCL_*` markers
2. Remove those lines from the coverage data
3. Recalculate totals

## Coverage Output Files

```
build/coverage/coverage/
├── coverage.lcov              # Raw coverage (before filtering)
├── coverage-temp.lcov         # After removing test/third-party files
├── coverage-filtered.lcov     # Final (with LCOV_EXCL applied)
├── html/                      # Raw HTML report
└── html-filtered/             # Filtered HTML report
    └── index.html             # Open this in browser
```

## Interpreting Results

### Line Coverage

- **98%+ is excellent** - Most code paths tested
- **95-98% is good** - Some error paths may be untested
- **<95%** - Review uncovered code for missing tests

### Uncovered Code Categories

1. **Error paths** (`[[unlikely]]`) - Test via mock tests
2. **Compile-time code** (`consteval`) - Exclude with `LCOV_EXCL_*`
3. **Dead code** - Remove it
4. **Missing tests** - Add tests

### Mock Tests

Error handling paths are tested in `tests/mock_sqlite/test_orm_mock_errors.cpp` using `LD_PRELOAD` to inject SQLite failures.

```bash
# Run mock tests separately
cmake --build build/coverage --target coverage-run-mock
```

## Troubleshooting

### Coverage Not Updating

```bash
# Clean and rebuild
cmake --build build/coverage --target coverage-clean
cmake --build build/coverage
cmake --build build/coverage --target coverage-filtered
```

### Module Cache Issues

If you get compiler crashes, the module cache may be corrupted:

```bash
rm -rf build/coverage
# Reconfigure and rebuild
```

### LCOV_EXCL Not Working

Ensure you're using `coverage-filtered` target, not raw `coverage-lcov`.

The raw lcov export from llvm-cov doesn't process exclusion markers.
