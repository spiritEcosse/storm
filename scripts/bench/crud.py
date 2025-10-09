#!/usr/bin/env python3
"""
Storm ORM CRUD Performance Benchmark
Comprehensive CRUD operation performance testing (INSERT/SELECT/UPDATE/DELETE)
"""

import sys
import re
import subprocess
from pathlib import Path

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import Colors


def format_throughput(value, operation_type="ops"):
    """Format throughput value in human-readable format"""
    if not value or value == 0:
        return "N/A"

    if value >= 1_000_000_000:
        return f"{value / 1_000_000_000:.2f}B {operation_type}/sec"
    elif value >= 1_000_000:
        return f"{value / 1_000_000:.2f}M {operation_type}/sec"
    elif value >= 1_000:
        return f"{value / 1_000:.1f}K {operation_type}/sec"
    else:
        return f"{value} {operation_type}/sec"


def color_throughput(value):
    """Color code throughput based on performance tiers"""
    if not value or value == 0:
        return f"{Colors.DIM}N/A{Colors.RESET}"

    formatted = format_throughput(value)

    if value >= 10_000_000:  # 10M+ = Excellent
        return f"{Colors.GREEN}{formatted}{Colors.RESET}"
    elif value >= 1_000_000:  # 1M+ = Good
        return f"{Colors.BLUE}{formatted}{Colors.RESET}"
    elif value >= 500_000:  # 500K+ = Acceptable
        return f"{Colors.YELLOW}{formatted}{Colors.RESET}"
    else:  # <500K = Poor
        return f"{Colors.RED}{formatted}{Colors.RESET}"


def run_benchmark(binary_path):
    """Run benchmark and capture output"""
    if not Path(binary_path).exists():
        return None

    try:
        result = subprocess.run([binary_path], capture_output=True, text=True, timeout=120)
        return result.stdout if result.returncode == 0 else None
    except Exception:
        return None


def extract_metric(output, pattern, metric_type="inserts"):
    """Extract throughput metric from benchmark output"""
    match = re.search(pattern, output, re.DOTALL)
    if match:
        # Extract number from line like "Throughput: 12345 inserts/sec"
        throughput_line = match.group(0).split('\n')
        for line in throughput_line:
            if 'Throughput:' in line:
                numbers = re.findall(r'(\d+)\s+' + metric_type, line)
                if numbers:
                    return int(numbers[0])
    return 0


def parse_benchmark_results():
    """Parse all benchmark results"""
    results = {}

    # Raw SQLite
    print(f"{Colors.CYAN}Running Raw SQLite benchmark...{Colors.RESET}")
    sqlite_output = run_benchmark('./build/release/benchmarks/bench_sqlite')
    if sqlite_output:
        results['raw_sqlite'] = {
            'name': 'Raw SQLite (prepared)',
            'single_insert': extract_metric(sqlite_output, r'Raw SQLite.*Single INSERT 10000.*?Throughput:', 'inserts'),
            'batch_insert': extract_metric(sqlite_output, r'Raw SQLite.*Batch INSERT 10000.*?Throughput:', 'inserts'),
            'single_delete': extract_metric(sqlite_output, r'Raw SQLite.*Single DELETE 10000.*?Throughput:', 'deletes'),
            'bulk_delete': extract_metric(sqlite_output, r'Raw SQLite.*Batch DELETE 10000.*?Throughput:', 'deletes'),
            'single_update': extract_metric(sqlite_output, r'Raw SQLite.*Single UPDATE 1000.*?Throughput:', 'updates'),
            'batch_update': extract_metric(sqlite_output, r'Raw SQLite.*Batch UPDATE 1000.*?Throughput:', 'updates'),
            'select': extract_metric(sqlite_output, r'Raw SQLite.*SELECT 10000.*?Throughput:', 'rows'),
        }

    # sqlite_orm
    print(f"{Colors.CYAN}Running sqlite_orm benchmark...{Colors.RESET}")
    sqliteorm_output = run_benchmark('./build/release/benchmarks/bench_sqlite_orm')
    if sqliteorm_output:
        results['sqlite_orm'] = {
            'name': 'sqlite_orm (v1.9.1)',
            'single_insert': extract_metric(sqliteorm_output, r'sqlite_orm.*Single INSERT 10000.*?Throughput:', 'inserts'),
            'batch_insert': extract_metric(sqliteorm_output, r'sqlite_orm.*Batch INSERT 10000.*?Throughput:', 'inserts'),
            'single_delete': extract_metric(sqliteorm_output, r'sqlite_orm.*Single DELETE 10000.*?Throughput:', 'deletes'),
            'bulk_delete': extract_metric(sqliteorm_output, r'sqlite_orm.*Batch DELETE 10000.*?Throughput:', 'deletes'),
            'single_update': extract_metric(sqliteorm_output, r'sqlite_orm.*Single UPDATE 1000.*?Throughput:', 'updates'),
            'batch_update': extract_metric(sqliteorm_output, r'sqlite_orm.*Batch UPDATE 1000.*?Throughput:', 'updates'),
            'select': extract_metric(sqliteorm_output, r'sqlite_orm.*SELECT 10000.*?Throughput:', 'rows'),
        }

    # Storm ORM
    print(f"{Colors.CYAN}Running Storm ORM benchmark...{Colors.RESET}")
    storm_output = run_benchmark('./build/release/benchmarks/bench_storm')
    if storm_output:
        results['storm_orm'] = {
            'name': 'Storm ORM (Standard)',
            'single_insert': extract_metric(storm_output, r'Storm ORM.*Single INSERT 10000.*?Throughput:', 'inserts'),
            'batch_insert': extract_metric(storm_output, r'Storm ORM.*Batch INSERT 10000.*?Throughput:', 'inserts'),
            'single_delete': extract_metric(storm_output, r'Storm ORM.*Single DELETE 10000.*?Throughput:', 'deletes'),
            'bulk_delete': extract_metric(storm_output, r'Storm ORM.*Batch DELETE 10000.*?Throughput:', 'deletes'),
            'single_update': extract_metric(storm_output, r'Storm ORM.*Single UPDATE 1000.*?Throughput:', 'updates'),
            'batch_update': extract_metric(storm_output, r'Storm ORM.*Batch UPDATE 1000.*?Throughput:', 'updates'),
            'select': extract_metric(storm_output, r'Storm ORM.*SELECT 10000.*?Throughput:', 'rows'),
        }

    return results


def calculate_overall_percentage(benchmark, baseline):
    """Calculate overall performance percentage across all metrics"""
    if not baseline:
        return 0

    metrics = ['single_insert', 'batch_insert', 'single_delete', 'bulk_delete',
               'single_update', 'batch_update', 'select']

    total = 0
    count = 0

    for metric in metrics:
        bench_val = benchmark.get(metric, 0)
        base_val = baseline.get(metric, 0)

        if bench_val and base_val and base_val > 0:
            percentage = (bench_val / base_val) * 100
            total += percentage
            count += 1

    return int(total / count) if count > 0 else 0


def display_results(results):
    """Display formatted CRUD benchmark results"""
    print(f"\n{Colors.BOLD}{'=' * 100}{Colors.RESET}")
    print(f"{Colors.BOLD}STORM ORM PERFORMANCE COMPARISON RESULTS{Colors.RESET}")
    print(f"{Colors.BOLD}{'=' * 100}{Colors.RESET}\n")

    if 'sqlite_orm' not in results:
        print(f"{Colors.RED}Error: sqlite_orm benchmark required for baseline{Colors.RESET}")
        return

    baseline = results['sqlite_orm']

    # Calculate percentages and prepare sorted data
    benchmark_data = []
    for key, data in results.items():
        percentage = calculate_overall_percentage(data, baseline)
        benchmark_data.append({
            'key': key,
            'data': data,
            'percentage': percentage
        })

    # Sort by percentage (descending)
    benchmark_data.sort(key=lambda x: x['percentage'], reverse=True)

    # Print table header
    print("┌─────────────────────────────────┬────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐")
    print("│ Benchmark                       │ Overall %  │ Single INS   │ Batch INS    │ Single DEL   │ Bulk DEL     │ Single UPD   │ Batch UPD    │ SELECT       │")
    print("├─────────────────────────────────┼────────────┼──────────────┼──────────────┼──────────────┼──────────────┼──────────────┼──────────────┼──────────────┤")

    # Print data rows
    for item in benchmark_data:
        data = item['data']
        percentage = item['percentage']

        # Format percentage
        if percentage == 100 and item['key'] == 'sqlite_orm':
            perc_str = f"{Colors.GREEN}100% (base){Colors.RESET}"
        else:
            perc_str = f"{Colors.BLUE}{percentage}%{Colors.RESET}" if percentage > 0 else "N/A"

        # Format metrics
        single_ins = color_throughput(data.get('single_insert', 0))
        batch_ins = color_throughput(data.get('batch_insert', 0))
        single_del = color_throughput(data.get('single_delete', 0))
        bulk_del = color_throughput(data.get('bulk_delete', 0))
        single_upd = color_throughput(data.get('single_update', 0))
        batch_upd = color_throughput(data.get('batch_update', 0))
        select_val = color_throughput(data.get('select', 0))

        # Print row (need to account for ANSI codes in width)
        name = data['name']
        print(f"│ {name:31} │ {perc_str:>10} │ {single_ins:>12} │ {batch_ins:>12} │ {single_del:>12} │ {bulk_del:>12} │ {single_upd:>12} │ {batch_upd:>12} │ {select_val:>12} │")

    print("└─────────────────────────────────┴────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘\n")

    # Performance legend
    print(f"{Colors.BOLD}Performance Tiers:{Colors.RESET}")
    print(f"  {Colors.GREEN}█{Colors.RESET} ≥10M ops/sec  : Excellent")
    print(f"  {Colors.BLUE}█{Colors.RESET} ≥1M ops/sec   : Good")
    print(f"  {Colors.YELLOW}█{Colors.RESET} ≥500K ops/sec : Acceptable")
    print(f"  {Colors.RED}█{Colors.RESET} <500K ops/sec : Needs improvement")
    print()


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(
        description='Storm ORM CRUD Performance Benchmark Suite',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument(
        '--build',
        action='store_true',
        help='Build release benchmarks before running'
    )

    args = parser.parse_args()

    # Build if requested
    if args.build:
        print(f"{Colors.YELLOW}Building release benchmarks...{Colors.RESET}\n")
        try:
            subprocess.run(['cmake', '--preset', 'ninja-release', '-DENABLE_BENCH=ON'], check=True)
            subprocess.run(['cmake', '--build', '--preset', 'ninja-release'], check=True)
            print(f"{Colors.GREEN}Build completed!{Colors.RESET}\n")
        except subprocess.CalledProcessError as e:
            print(f"{Colors.RED}Build failed: {e}{Colors.RESET}")
            sys.exit(1)

    # Check if benchmarks exist
    required_benchmarks = [
        './build/release/benchmarks/bench_sqlite',
        './build/release/benchmarks/bench_sqlite_orm',
        './build/release/benchmarks/bench_storm'
    ]

    missing = [b for b in required_benchmarks if not Path(b).exists()]
    if missing:
        print(f"{Colors.RED}Missing benchmarks:{Colors.RESET}")
        for b in missing:
            print(f"  - {b}")
        print(f"\n{Colors.YELLOW}Run with --build flag to build them{Colors.RESET}")
        sys.exit(1)

    # Run benchmarks
    print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 100}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}STORM ORM COMPREHENSIVE CRUD BENCHMARK SUITE (RELEASE MODE){Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 100}{Colors.RESET}\n")

    results = parse_benchmark_results()

    # Display results
    display_results(results)

    print(f"{Colors.GREEN}✓ Comprehensive CRUD benchmark suite completed successfully!{Colors.RESET}\n")


if __name__ == '__main__':
    main()
