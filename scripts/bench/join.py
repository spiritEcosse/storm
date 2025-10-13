#!/usr/bin/env python3
"""
Storm ORM JOIN Performance Benchmark
Benchmarks JOIN operations and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class JoinBenchmark(BenchmarkRunner):
    """JOIN performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_join_performance'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse JOIN benchmark output"""
        def extract_throughput(pattern):
            match = re.search(pattern + r'.*?Throughput:\s+(\d+)\s+rows/sec', output, re.DOTALL)
            return int(match.group(1)) if match else 0

        return {
            'select_no_join': (
                extract_throughput(r'SELECT \(no JOIN\)'),
                0  # No raw SQLite equivalent for simple SELECT
            ),
            'inner_single': (
                extract_throughput(r'INNER JOIN \(single FK: sender\)'),
                extract_throughput(r'Raw SQLite INNER JOIN \(single FK\)')
            ),
            'inner_multi': (
                extract_throughput(r'INNER JOIN \(multi FK: sender'),
                extract_throughput(r'Raw SQLite INNER JOIN \(multi FK\)')
            ),
            'left_single': (
                extract_throughput(r'LEFT JOIN \(single FK: sender\)'),
                extract_throughput(r'Raw SQLite LEFT JOIN \(single FK\)')
            ),
            'left_multi': (
                extract_throughput(r'LEFT JOIN \(multi FK: sender'),
                extract_throughput(r'Raw SQLite LEFT JOIN \(multi FK\)')
            ),
            'right_single': (
                extract_throughput(r'RIGHT JOIN \(single FK: sender\)'),
                extract_throughput(r'Raw SQLite RIGHT JOIN \(single FK\)')
            ),
            'right_multi': (
                extract_throughput(r'RIGHT JOIN \(multi FK: sender'),
                extract_throughput(r'Raw SQLite RIGHT JOIN \(multi FK\)')
            ),
        }

    def display_results(self, data, messages=10000, iterations=100):
        """Display formatted JOIN benchmark results"""

        # Print header
        BenchmarkTable.print_header("JOIN Operation")

        # Simple SELECT baseline (no JOIN)
        storm_select, _ = data['select_no_join']
        print(f"│ {'Simple SELECT (no JOIN)':<31} │ {storm_select/1_000_000:>11.2f}M │ {'(baseline)':<12} │ {'-':<12} │")
        BenchmarkTable.print_separator()

        # RIGHT JOIN operations (most efficient)
        for name, label in [
            ('right_single', 'RIGHT JOIN (single FK)'),
            ('right_multi', 'RIGHT JOIN (multi FK)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # LEFT JOIN operations
        for name, label in [
            ('left_single', 'LEFT JOIN (single FK)'),
            ('left_multi', 'LEFT JOIN (multi FK)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # INNER JOIN operations
        for name, label in [
            ('inner_single', 'INNER JOIN (single FK)'),
            ('inner_multi', 'INNER JOIN (multi FK)'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_footer()

        # Calculate and print average efficiency (excluding simple SELECT baseline)
        all_effs = [(data[k][0] / data[k][1] * 100) if data[k][1] > 0 else 0
                    for k in data.keys() if k != 'select_no_join']
        avg_eff = sum(all_effs) / len(all_effs)

        BenchmarkTable.print_summary(messages, iterations, avg_eff)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM JOIN Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults
  %(prog)s --messages 50000              # Test with 50k messages
  %(prog)s --iterations 200              # More iterations for stability
  %(prog)s --binary ./custom/path        # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_join_performance',
        help='Path to benchmark binary (default: %(default)s)'
    )
    parser.add_argument(
        '--messages',
        type=int,
        default=10000,
        help='Number of messages (default: %(default)s)'
    )
    parser.add_argument(
        '--iterations',
        type=int,
        default=100,
        help='Number of iterations (default: %(default)s)'
    )

    args = parser.parse_args()

    # Run benchmark
    benchmark = JoinBenchmark(args.binary)
    benchmark.run(
        f'--size={args.messages}',
        f'--iterations={args.iterations}',
        messages=args.messages,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
