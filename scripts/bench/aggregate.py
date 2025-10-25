#!/usr/bin/env python3
"""
Storm ORM Aggregate Functions Performance Benchmark
Benchmarks aggregate operations (SUM, COUNT, AVG, MIN, MAX) and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class AggregateBenchmark(BenchmarkRunner):
    """Aggregate functions performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_aggregate'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse aggregate benchmark output"""
        def extract_throughput(pattern):
            match = re.search(pattern + r'.*?Throughput:\s+(\d+)\s+rows/sec', output, re.DOTALL)
            return int(match.group(1)) if match else 0

        return {
            'sum': (
                extract_throughput(r'Storm SUM\(age\)'),
                extract_throughput(r'Raw SQLite SUM\(age\)')
            ),
            'count': (
                extract_throughput(r'Storm COUNT\(\*\)'),
                extract_throughput(r'Raw SQLite COUNT\(\*\)')
            ),
            'avg': (
                extract_throughput(r'Storm AVG\(salary\)'),
                extract_throughput(r'Raw SQLite AVG\(salary\)')
            ),
            'min': (
                extract_throughput(r'Storm MIN\(age\)'),
                extract_throughput(r'Raw SQLite MIN\(age\)')
            ),
            'max': (
                extract_throughput(r'Storm MAX\(salary\)'),
                extract_throughput(r'Raw SQLite MAX\(salary\)')
            ),
            'multi_aggregate': (
                extract_throughput(r'Storm Multi-Aggregate'),
                extract_throughput(r'Raw SQLite Multi-Aggregate')
            ),
            'sum_multi': (
                extract_throughput(r'Storm SUM\(age\+years\)'),
                extract_throughput(r'Raw SQLite SUM\(age\+years\)')
            ),
        }

    def display_results(self, data, rows=10000, iterations=100):
        """Display formatted aggregate benchmark results"""

        # Print header
        BenchmarkTable.print_header("Aggregate Operation")

        # Single-function aggregates
        for name, label in [
            ('sum', 'SUM(age)'),
            ('count', 'COUNT(*)'),
            ('avg', 'AVG(salary)'),
            ('min', 'MIN(age)'),
            ('max', 'MAX(salary)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # Multi-function aggregates
        for name, label in [
            ('multi_aggregate', 'Multi-Aggregate (SUM+COUNT+AVG)'),
            ('sum_multi', 'SUM(age+years_experience)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_footer()

        # Calculate and print average efficiency
        all_effs = [(data[k][0] / data[k][1] * 100) if data[k][1] > 0 else 0
                    for k in data.keys()]
        avg_eff = sum(all_effs) / len(all_effs)

        BenchmarkTable.print_summary(rows, iterations, avg_eff)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM Aggregate Functions Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults (10K rows)
  %(prog)s --size 50000                   # Test with 50k rows
  %(prog)s --iterations 200               # More iterations for stability
  %(prog)s --binary ./custom/path         # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_aggregate',
        help='Path to benchmark binary (default: %(default)s)'
    )
    parser.add_argument(
        '--size',
        type=int,
        default=10000,
        help='Number of rows (default: %(default)s)'
    )
    parser.add_argument(
        '--iterations',
        type=int,
        default=100,
        help='Number of iterations (default: %(default)s)'
    )
    parser.add_argument(
        '--report',
        action='store_true',
        help='Show raw benchmark output instead of formatted table'
    )

    args = parser.parse_args()

    # Run benchmark
    benchmark = AggregateBenchmark(args.binary)
    benchmark.run(
        f'--size={args.size}',
        f'--iterations={args.iterations}',
        show_raw_output=args.report,
        rows=args.size,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
