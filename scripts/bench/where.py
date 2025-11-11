#!/usr/bin/env python3
"""
Storm ORM WHERE Performance Benchmark
Benchmarks WHERE clause operations and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class WhereBenchmark(BenchmarkRunner):
    """WHERE clause performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_where'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse WHERE benchmark output"""
        def extract_throughput_baseline(pattern):
            # Look for the pattern followed by "Throughput: X.XXM rows/sec"
            match = re.search(pattern + r'[^\n]*\n(?:[^\n]*\n)*?\s*Throughput:\s+([\d.]+)M\s+rows/sec', output)
            return float(match.group(1)) * 1_000_000 if match else 0

        def extract_throughput_new(section_pattern):
            # New format: "WHERE (int: age > 30):\n  Storm ORM: X.XXM rows/sec\n  Raw SQLite: X.XXM rows/sec"
            match = re.search(section_pattern + r':\s*\n\s*Storm ORM:\s+([\d.]+)M\s+rows/sec\s*\n\s*Raw SQLite:\s+([\d.]+)M\s+rows/sec', output)
            if match:
                storm = float(match.group(1)) * 1_000_000
                raw = float(match.group(2)) * 1_000_000
                return (storm, raw)
            return (0, 0)

        return {
            'select_no_where': (
                extract_throughput_baseline(r'SELECT \(no WHERE\)'),
                0  # No raw SQLite equivalent for baseline
            ),
            # Data type comparisons
            'int': extract_throughput_new(r'WHERE \(int: age > 30\)'),
            'string': extract_throughput_new(r'WHERE \(string: name LIKE "Person5%"\)'),
            'float': extract_throughput_new(r'WHERE \(float: salary > 35000\.0\)'),
            'bool': extract_throughput_new(r'WHERE \(bool: is_active == true\)'),
            # Special methods
            'like': extract_throughput_new(r'WHERE \(LIKE: name LIKE "Person5%"\)'),
            'between': extract_throughput_new(r'WHERE \(BETWEEN: age BETWEEN 28 AND 35\)'),
            'in_3': extract_throughput_new(r'WHERE \(IN 3 values: id IN \(100, 200, 300\)\)'),
            'in_10': extract_throughput_new(r'WHERE \(IN 10 values: id IN \(100\.\.\.1000\)\)'),
            # Complex queries
            'simple': extract_throughput_new(r'WHERE \(simple: age > 25 AND age < 50\)'),
            'medium': extract_throughput_new(r'WHERE \(medium: 4 conditions with AND/OR\)'),
            'complex': extract_throughput_new(r'WHERE \(complex: 8\+ conditions, nested AND/OR, special methods\)'),
        }

    def display_results(self, data, size=10000, iterations=100):
        """Display formatted WHERE benchmark results"""

        # Add blank line before table for better visual separation
        print()

        # Print header
        BenchmarkTable.print_header("WHERE Operation")

        # Simple SELECT baseline (no WHERE)
        storm_select, _ = data['select_no_where']
        print(f"│ {'SELECT (no WHERE)':<37} │ {storm_select/1_000_000:>11.2f}M │ {'(baseline)':<12} │ {'-':<12} │")
        BenchmarkTable.print_separator()

        # Data type comparisons
        for name, label in [
            ('int', 'WHERE (int: age > 30)'),
            ('string', 'WHERE (string: name LIKE "Person5%")'),
            ('float', 'WHERE (float: salary > 35000.0)'),
            ('bool', 'WHERE (bool: is_active == true)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # Special methods
        for name, label in [
            ('like', 'WHERE (LIKE pattern)'),
            ('between', 'WHERE (BETWEEN range)'),
            ('in_3', 'WHERE (IN 3 values)'),
            ('in_10', 'WHERE (IN 10 values)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # Complex queries
        for name, label in [
            ('simple', 'WHERE (simple: 2 AND)'),
            ('medium', 'WHERE (medium: 4 cond)'),
            ('complex', 'WHERE (complex: 8+ cond)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_footer()

        # Add blank line after table
        print()

        # Calculate and print average efficiency (excluding baseline)
        all_effs = [(data[k][0] / data[k][1] * 100) if data[k][1] > 0 else 0
                    for k in data.keys() if k != 'select_no_where']
        avg_eff = sum(all_effs) / len(all_effs)

        BenchmarkTable.print_summary(size, iterations, avg_eff)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM WHERE Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults (10k rows, 100 iterations)
  %(prog)s --size 50000                   # Test with 50k rows
  %(prog)s --iterations 200               # More iterations for stability
  %(prog)s --binary ./custom/path         # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_where',
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

    args = parser.parse_args()

    # Run benchmark
    benchmark = WhereBenchmark(args.binary)
    benchmark.run(
        f'--size={args.size}',
        f'--iterations={args.iterations}',
        size=args.size,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
