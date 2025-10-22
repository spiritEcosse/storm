#!/usr/bin/env python3
"""
Storm ORM DISTINCT Performance Benchmark
Benchmarks DISTINCT operations and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class DistinctBenchmark(BenchmarkRunner):
    """DISTINCT performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_distinct'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse DISTINCT benchmark output"""
        def extract_throughput(pattern):
            match = re.search(pattern + r'.*?Throughput:\s+(\d+)\s+rows/sec', output, re.DOTALL)
            return int(match.group(1)) if match else 0

        return {
            # Single-field DISTINCT
            'name': (
                extract_throughput(r'Storm ORM DISTINCT \(name\)'),
                extract_throughput(r'Raw SQLite DISTINCT \(name\)')
            ),
            'age': (
                extract_throughput(r'Storm ORM DISTINCT \(age\)'),
                extract_throughput(r'Raw SQLite DISTINCT \(age\)')
            ),
            'id': (
                extract_throughput(r'Storm ORM DISTINCT \(id/PK\)'),
                extract_throughput(r'Raw SQLite DISTINCT \(id\)')
            ),
            # Multi-field DISTINCT
            'name_age': (
                extract_throughput(r'Storm ORM DISTINCT \(name, age\)'),
                extract_throughput(r'Raw SQLite DISTINCT \(name, age\)')
            ),
            'id_name_age': (
                extract_throughput(r'Storm ORM DISTINCT \(id, name, age\)'),
                extract_throughput(r'Raw SQLite DISTINCT \(id, name, age\)')
            ),
        }

    def display_results(self, data, records=10000, iterations=100):
        """Display formatted DISTINCT benchmark results"""

        # Print header
        BenchmarkTable.print_header("DISTINCT Operation")

        # Single-field DISTINCT operations
        print(f"\n{Colors.BOLD}Single-Field DISTINCT:{Colors.RESET}")
        for name, label in [
            ('name', 'DISTINCT (name - TEXT)'),
            ('age', 'DISTINCT (age - INTEGER)'),
            ('id', 'DISTINCT (id/PK - INTEGER)'),
        ]:
            if name in data and data[name][0] > 0:  # Only show if data exists
                storm, raw = data[name]
                eff = (storm / raw * 100) if raw > 0 else 0
                BenchmarkTable.print_row(label, storm, raw, eff)

        # Multi-field DISTINCT operations
        print(f"\n{Colors.BOLD}Multi-Field DISTINCT:{Colors.RESET}")
        for name, label in [
            ('name_age', 'DISTINCT (name, age)'),
            ('id_name_age', 'DISTINCT (id, name, age)'),
        ]:
            if name in data and data[name][0] > 0:  # Only show if data exists
                storm, raw = data[name]
                eff = (storm / raw * 100) if raw > 0 else 0
                BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_footer()

        # Calculate and print average efficiency
        all_effs = [(data[k][0] / data[k][1] * 100) if data[k][1] > 0 and data[k][0] > 0 else 0
                    for k in data.keys() if k in data and data[k][0] > 0]
        avg_eff = sum(all_effs) / len(all_effs) if all_effs else 0

        BenchmarkTable.print_summary(records, iterations, avg_eff)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM DISTINCT Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults
  %(prog)s --records 50000               # Test with 50k records
  %(prog)s --iterations 200              # More iterations for stability
  %(prog)s --binary ./custom/path        # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_distinct',
        help='Path to benchmark binary (default: %(default)s)'
    )
    parser.add_argument(
        '--records',
        type=int,
        default=10000,
        help='Number of records (default: %(default)s)'
    )
    parser.add_argument(
        '--iterations',
        type=int,
        default=100,
        help='Number of iterations (default: %(default)s)'
    )

    args = parser.parse_args()

    # Run benchmark
    benchmark = DistinctBenchmark(args.binary)
    benchmark.run(
        f'--size={args.records}',
        f'--iterations={args.iterations}',
        records=args.records,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
