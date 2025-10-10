#!/usr/bin/env python3
"""
Storm ORM Performance Benchmark Suite
Builds and runs comprehensive CRUD operation performance tests
"""

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, List, Tuple

# Import shared utilities
sys.path.insert(0, str(Path(__file__).parent))
from common import FlexibleTable, pad_string, get_visible_length


# ANSI Color Codes
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'  # No Color


def print_step(message: str):
    print(f"{Colors.BLUE}[INFO]{Colors.NC} {message}")


def print_success(message: str):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.NC} {message}")


def print_warning(message: str):
    print(f"{Colors.YELLOW}[WARNING]{Colors.NC} {message}")


def print_error(message: str):
    print(f"{Colors.RED}[ERROR]{Colors.NC} {message}")


def format_number(num: int, operation_type: str = "inserts") -> str:
    """Format numbers in human-readable format (K, M, B suffixes)"""
    if num >= 1_000_000_000:
        billions = num / 1_000_000_000
        return f"{billions:.2f}B {operation_type}/sec"
    elif num >= 1_000_000:
        millions = num / 1_000_000
        return f"{millions:.2f}M {operation_type}/sec"
    elif num >= 1_000:
        thousands = num / 1_000
        if thousands >= 1000:
            return f"1.00M {operation_type}/sec"
        return f"{thousands:.1f}K {operation_type}/sec"
    else:
        return f"{num} {operation_type}/sec"


def format_number_with_color(num: int, operation_type: str = "inserts") -> str:
    """Format numbers with color-coded performance indicators"""
    formatted = format_number(num, operation_type)

    if num >= 10_000_000:  # 10M+ = Excellent (Green)
        return f"{Colors.GREEN}{formatted}{Colors.NC}"
    elif num >= 1_000_000:  # 1M+ = Good (Blue)
        return f"{Colors.BLUE}{formatted}{Colors.NC}"
    elif num >= 500_000:  # 500K+ = Acceptable (Yellow)
        return f"{Colors.YELLOW}{formatted}{Colors.NC}"
    else:  # <500K = Poor (Red)
        return f"{Colors.RED}{formatted}{Colors.NC}"


def calculate_percentage(value: Optional[int], baseline: Optional[int]) -> int:
    """Calculate percentage relative to baseline"""
    if value and baseline and baseline != 0:
        return round((value * 100) / baseline)
    return 0


def calculate_overall_percentage(
    single_insert: Optional[int],
    batch_insert: Optional[int],
    single_delete: Optional[int],
    bulk_delete: Optional[int],
    single_update: Optional[int],
    batch_update: Optional[int],
    select: Optional[int],
    baseline_single_insert: Optional[int],
    baseline_batch_insert: Optional[int],
    baseline_single_delete: Optional[int],
    baseline_bulk_delete: Optional[int],
    baseline_single_update: Optional[int],
    baseline_batch_update: Optional[int],
    baseline_select: Optional[int],
) -> int:
    """Calculate overall performance percentage across all metrics"""
    total_percentage = 0
    count = 0

    metrics = [
        (single_insert, baseline_single_insert),
        (batch_insert, baseline_batch_insert),
        (single_delete, baseline_single_delete),
        (bulk_delete, baseline_bulk_delete),
        (single_update, baseline_single_update),
        (batch_update, baseline_batch_update),
        (select, baseline_select),
    ]

    for value, baseline in metrics:
        if value and baseline and baseline != 0:
            pct = calculate_percentage(value, baseline)
            total_percentage += pct
            count += 1

    if count > 0:
        return total_percentage // count
    return 0


@dataclass
class BenchmarkResult:
    name: str
    performance_percentage: int
    single_insert: Optional[int] = None
    batch_insert: Optional[int] = None
    single_delete: Optional[int] = None
    bulk_delete: Optional[int] = None
    single_update: Optional[int] = None
    batch_update: Optional[int] = None
    select: Optional[int] = None


def extract_metric(output: str, pattern: str, metric_name: str = "Throughput:") -> Optional[int]:
    """Extract performance metric from benchmark output"""
    # Find the section matching the pattern
    lines = output.split('\n')
    in_section = False
    section_lines = []

    for line in lines:
        if pattern in line:
            in_section = True
            section_lines = [line]
        elif in_section:
            section_lines.append(line)
            if len(section_lines) >= 8:  # Get enough lines to find throughput
                break

    # Search for throughput in the section
    for line in section_lines:
        if metric_name in line:
            # Extract number from "Throughput: <number> <unit>/sec"
            match = re.search(r'Throughput:\s*(\d+)\s+', line)
            if match:
                return int(match.group(1))

    return None


def run_cmake_command(args: List[str], description: str) -> bool:
    """Run a cmake command and return success status"""
    print_step(description)
    try:
        result = subprocess.run(args, check=True, capture_output=True, text=True)
        return True
    except subprocess.CalledProcessError as e:
        print_error(f"Command failed: {' '.join(args)}")
        if e.stderr:
            print(e.stderr)
        return False


def run_benchmark(executable: Path) -> Optional[str]:
    """Run a benchmark executable and return its output"""
    if not executable.exists() or not executable.is_file():
        return None

    try:
        result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
        return result.stdout
    except subprocess.CalledProcessError as e:
        print_error(f"Benchmark failed: {executable}")
        return None


def performance_comparison():
    """Run comprehensive performance comparison"""
    print("=== STORM ORM PERFORMANCE BENCHMARK SUITE (RELEASE MODE) ===")
    print("Building and running comprehensive CRUD operation performance tests")
    print("Using optimized Release build for accurate production performance measurements")
    print()

    # Get project root (scripts/bench/perf_compare.py -> go up 2 levels to project root)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent

    # Step 1: Configure CMake
    print_step("Step 1: Configuring Release build with benchmarking enabled...")
    if not run_cmake_command(
        ['cmake', '--preset', 'ninja-release', '-DENABLE_TESTS=ON', '-DENABLE_BENCH=ON'],
        "Configuring CMake..."
    ):
        print_error("CMake Release configuration failed")
        sys.exit(1)

    print_success("CMake Release configuration completed")
    print()

    # Step 2: Build benchmarks
    print_step("Step 2: Building optimized benchmark executables...")

    # Build SQLite benchmarks
    print_step("Building SQLite and sqlite_orm benchmarks (Release)...")
    if run_cmake_command(
        ['cmake', '--build', '--preset', 'ninja-release', '--target', 'bench_sqlite', '--target', 'bench_sqlite_orm'],
        "Building SQLite benchmarks..."
    ):
        print_success("SQLite benchmarks built successfully (optimized)")
    else:
        print_warning("Some SQLite benchmarks may have failed to build")

    # Build Storm benchmarks
    print_step("Building Storm ORM benchmarks (Release)...")
    if not run_cmake_command(
        ['cmake', '--build', '--preset', 'ninja-release', '--target', 'bench_storm'],
        "Building Storm benchmarks..."
    ):
        print_warning("Building Storm benchmarks individually due to potential clang-scan-deps issues...")

        # Try building Storm library first
        subprocess.run(['cmake', '--build', '--preset', 'ninja-release', '--target', 'storm'],
                      capture_output=True)

        # Clean temporary files
        import glob
        for tmp_file in glob.glob('build/release/benchmarks/CMakeFiles/bench_storm*/bench_storm*.cpp.o.ddi.tmp'):
            try:
                Path(tmp_file).unlink()
            except:
                pass

        # Build Storm benchmark
        if run_cmake_command(
            ['cmake', '--build', '--preset', 'ninja-release', '--target', 'bench_storm'],
            "Retrying bench_storm build..."
        ):
            print_success("bench_storm built successfully (optimized)")
        else:
            print_error("Failed to build bench_storm")

    print()

    # Step 3: Verify executables
    print_step("Step 3: Verifying Release benchmark executables...")
    executables = {
        'bench_sqlite': project_root / 'build/release/benchmarks/bench_sqlite',
        'bench_sqlite_orm': project_root / 'build/release/benchmarks/bench_sqlite_orm',
        'bench_storm': project_root / 'build/release/benchmarks/bench_storm',
    }

    missing = []
    for name, path in executables.items():
        if path.exists() and path.is_file():
            print_success(f"{name}: Ready (optimized)")
        else:
            print_error(f"{name}: Missing or not executable")
            missing.append(name)

    if missing:
        print_warning("Some executables are missing, performance comparison may be incomplete")

    print()

    # Step 4: Run benchmarks and collect results
    print_step("Step 4: Running performance benchmarks...")
    print()

    print("=== STORM ORM PERFORMANCE COMPARISON RESULTS ===")
    print("Testing database operations with different approaches:")
    print()
    print(f"{Colors.YELLOW}Extracting performance metrics for 10,000 operations...{Colors.NC}")
    print()

    results: List[BenchmarkResult] = []
    baseline = BenchmarkResult(name="sqlite_orm (v1.9.1)", performance_percentage=100)

    # Run Raw SQLite benchmark
    sqlite_output = run_benchmark(executables['bench_sqlite'])
    if sqlite_output:
        sqlite_result = BenchmarkResult(
            name="Raw SQLite (prepared)",
            performance_percentage=0,  # Will calculate after baseline is set
            single_insert=extract_metric(sqlite_output, "Raw SQLite (prepared statements) - Single INSERT 10000 records:"),
            batch_insert=extract_metric(sqlite_output, "Raw SQLite - Batch INSERT 10000 records (batch size 1000)"),
            single_delete=extract_metric(sqlite_output, "Raw SQLite (prepared statements) - Single DELETE 10000 records:"),
            bulk_delete=extract_metric(sqlite_output, "Raw SQLite - Batch DELETE 10000 records"),
            single_update=extract_metric(sqlite_output, "Raw SQLite (prepared statements) - Single UPDATE 1000 records:"),
            batch_update=extract_metric(sqlite_output, "Raw SQLite - Batch UPDATE 1000 records"),
            select=extract_metric(sqlite_output, "Raw SQLite - SELECT 10000 records:"),
        )
        results.append(sqlite_result)
    else:
        results.append(BenchmarkResult(name="Raw SQLite (prepared)", performance_percentage=0))

    # Run sqlite_orm benchmark (baseline)
    sqliteorm_output = run_benchmark(executables['bench_sqlite_orm'])
    if sqliteorm_output:
        baseline.single_insert = extract_metric(sqliteorm_output, "sqlite_orm - Single INSERT 10000 records:")
        baseline.batch_insert = extract_metric(sqliteorm_output, "sqlite_orm - Batch INSERT 10000 records (batch size 100)")
        baseline.single_delete = extract_metric(sqliteorm_output, "sqlite_orm - Single DELETE 10000 records:")
        baseline.bulk_delete = extract_metric(sqliteorm_output, "sqlite_orm - Batch DELETE 10000 records")
        baseline.single_update = extract_metric(sqliteorm_output, "sqlite_orm - Single UPDATE 1000 records:")
        baseline.batch_update = extract_metric(sqliteorm_output, "sqlite_orm - Batch UPDATE 1000 records")
        baseline.select = extract_metric(sqliteorm_output, "sqlite_orm - SELECT 10000 records:")
        results.append(baseline)
    else:
        results.append(baseline)

    # Run Storm ORM benchmark
    storm_output = run_benchmark(executables['bench_storm'])
    if storm_output:
        storm_result = BenchmarkResult(
            name="Storm ORM (Standard)",
            performance_percentage=0,  # Will calculate
            single_insert=extract_metric(storm_output, "Storm ORM - Single INSERT 10000 records:"),
            batch_insert=extract_metric(storm_output, "Storm ORM - Batch INSERT 10000 records (batch size 1000)"),
            single_delete=extract_metric(storm_output, "Storm ORM - Single DELETE 10000 records:"),
            bulk_delete=extract_metric(storm_output, "Storm ORM - Batch DELETE 10000 records"),
            single_update=extract_metric(storm_output, "Storm ORM - Single UPDATE 1000 records:"),
            batch_update=extract_metric(storm_output, "Storm ORM - Batch UPDATE 1000 records"),
            select=extract_metric(storm_output, "Storm ORM - SELECT 10000 records:"),
        )

        # Calculate Storm ORM percentage
        storm_result.performance_percentage = calculate_overall_percentage(
            storm_result.single_insert, storm_result.batch_insert,
            storm_result.single_delete, storm_result.bulk_delete,
            storm_result.single_update, storm_result.batch_update,
            storm_result.select,
            baseline.single_insert, baseline.batch_insert,
            baseline.single_delete, baseline.bulk_delete,
            baseline.single_update, baseline.batch_update,
            baseline.select,
        )

        results.append(storm_result)
    else:
        results.append(BenchmarkResult(name="Storm ORM (Standard)", performance_percentage=0))

    # Calculate Raw SQLite percentage now that baseline is set
    if results[0].name == "Raw SQLite (prepared)":
        results[0].performance_percentage = calculate_overall_percentage(
            results[0].single_insert, results[0].batch_insert,
            results[0].single_delete, results[0].bulk_delete,
            results[0].single_update, results[0].batch_update,
            results[0].select,
            baseline.single_insert, baseline.batch_insert,
            baseline.single_delete, baseline.bulk_delete,
            baseline.single_update, baseline.batch_update,
            baseline.select,
        )

    # Sort results by performance percentage (descending)
    results.sort(key=lambda x: x.performance_percentage, reverse=True)

    # Display results table using FlexibleTable
    table = FlexibleTable(
        headers=[
            'Benchmark',
            'Overall Perf %',
            'Single INSERT',
            'Best Batch INSERT',
            'Single DELETE',
            'Bulk DELETE',
            'Single UPDATE',
            'Best Batch UPDATE',
            'SELECT'
        ],
        column_widths=[35, 16, 18, 19, 18, 19, 18, 19, 19]
    )

    table.print_header()

    for result in results:
        # Format percentage
        if result.performance_percentage > 0:
            if result.performance_percentage == 100 and result.name == "sqlite_orm (v1.9.1)":
                percentage_display = f"{Colors.GREEN}100% (baseline){Colors.NC}"
            else:
                percentage_display = f"{Colors.BLUE}{result.performance_percentage}%{Colors.NC}"
        else:
            percentage_display = "N/A"

        # Format metrics
        def format_metric(value: Optional[int], op_type: str) -> str:
            if value:
                return format_number_with_color(value, op_type)
            return "Not available"

        single_insert_display = format_metric(result.single_insert, "inserts")
        batch_insert_display = format_metric(result.batch_insert, "inserts")
        single_delete_display = format_metric(result.single_delete, "deletes")
        bulk_delete_display = format_metric(result.bulk_delete, "deletes")
        single_update_display = format_metric(result.single_update, "updates")
        batch_update_display = format_metric(result.batch_update, "updates")
        select_display = format_metric(result.select, "rows")

        # Print row
        table.print_row([
            result.name,
            percentage_display,
            single_insert_display,
            batch_insert_display,
            single_delete_display,
            bulk_delete_display,
            single_update_display,
            batch_update_display,
            select_display
        ])

    table.print_footer()
    print()

    print_success("Comprehensive performance benchmark suite completed successfully!")

    # Build information
    print()
    print("=== BUILD INFORMATION ===")
    try:
        compiler_version = subprocess.run(
            ['/home/ihor/projects/storm/clang-p2996/build/bin/clang++', '--version'],
            capture_output=True, text=True, check=True
        )
        print(f"Compiler: {compiler_version.stdout.splitlines()[0]}")
    except:
        print("Compiler: Custom Clang with C++26 reflection")

    print("C++ Standard: C++26 with reflection support")
    print("Build Type: Release (Optimized)")
    print("CMake Preset: ninja-release")
    print("Enabled Features: Tests, Benchmarks")
    print("Optimization Level: -O2 or higher")
    print()
    print("NOTE: These are production-grade performance measurements.")
    print("      For development/debugging, use performance_comparison.sh (Debug build)")
    print()

    print("To re-run individual benchmarks (without rebuilding):")
    print("  ./build/release/benchmarks/bench_sqlite          # Raw SQLite baseline")
    print("  ./build/release/benchmarks/bench_sqlite_orm      # sqlite_orm comparison")
    print("  ./build/release/benchmarks/bench_storm           # Storm ORM standard")
    print()
    print("For detailed optimization analysis:")
    print("  ./build/release/benchmarks/bench_storm --mode=cache-analysis      # SQL cache performance testing")
    print("  ./build/release/benchmarks/bench_storm --mode=optimization-test   # Comprehensive optimization testing")


def main():
    parser = argparse.ArgumentParser(description="Storm ORM Benchmark Suite")
    parser.add_argument('--perf_compare', action='store_true',
                       help='Run comprehensive performance comparison (Release build)')

    args = parser.parse_args()

    if args.perf_compare:
        performance_comparison()
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
