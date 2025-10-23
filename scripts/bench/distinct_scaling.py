#!/usr/bin/env python3
"""
Storm ORM DISTINCT Scaling Performance Benchmark
Shows how DISTINCT performance scales with different result sizes
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, Colors


class DistinctScalingBenchmark(BenchmarkRunner):
    """DISTINCT scaling performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_distinct_scaling'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse scaling benchmark output into structured data"""
        results = {}
        current_size = None

        for line in output.split('\n'):
            # Match result size section headers
            size_match = re.search(r'Result Size: ~(\d+) unique rows', line)
            if size_match:
                current_size = int(size_match.group(1))
                results[current_size] = {}
                continue

            # Match Storm ORM lines with throughput and efficiency (raw SQLite)
            # Format: Storm: name | Avg: 0.97 ms | Results: 100 | Throughput: 0.10 M/s | Efficiency: 0.11 M/s
            storm_match = re.search(r'Storm:\s+(.+?)\s+\|\s+Avg:\s+[\d.]+\s+ms\s+\|\s+Results:\s+\d+\s+\|\s+Throughput:\s+([\d.]+)\s+M/s\s+\|\s+Efficiency:\s+([\d.]+)\s+M/s', line)
            if storm_match and current_size is not None:
                operation = storm_match.group(1).strip()
                storm_throughput = float(storm_match.group(2)) * 1_000_000  # Convert M/s to rows/s
                raw_throughput = float(storm_match.group(3)) * 1_000_000    # Efficiency column is actually raw SQLite

                if operation not in results[current_size]:
                    results[current_size][operation] = {}
                results[current_size][operation]['storm'] = storm_throughput
                results[current_size][operation]['raw'] = raw_throughput
                continue

        return results

    def display_results(self, data, records=10000, iterations=100):
        """Display formatted table with efficiency percentages"""
        from common import BenchmarkTable, Colors

        if not data:
            print(f"{Colors.RED}No results to display{Colors.RESET}")
            return

        print(f"\n{Colors.BOLD}DISTINCT Scaling Performance Analysis{Colors.RESET}")
        print(f"{Colors.DIM}Total records: {records:,}, Iterations: {iterations}{Colors.RESET}\n")

        # Process each result size tier
        for size in sorted(data.keys()):
            print(f"\n{Colors.BOLD}{Colors.CYAN}Result Size: ~{size:,} unique rows{Colors.RESET}")

            # Calculate efficiency for each operation
            operations = data[size]
            if not operations:
                continue

            BenchmarkTable.print_header("Operation", include_db_hits=False)

            for op_name in sorted(operations.keys()):
                op_data = operations[op_name]
                if 'storm' in op_data and 'raw' in op_data:
                    storm_val = op_data['storm']
                    raw_val = op_data['raw']
                    efficiency = (storm_val / raw_val * 100) if raw_val > 0 else 0

                    # Format operation name
                    if op_name in ['name', 'age', 'id']:
                        label = f"DISTINCT {op_name}"
                    elif ', ' in op_name:
                        label = f"DISTINCT ({op_name})"
                    else:
                        label = op_name

                    BenchmarkTable.print_row(
                        label,
                        storm_val,
                        raw_val,
                        efficiency
                    )

            BenchmarkTable.print_footer(include_db_hits=False)

            # Calculate average efficiency for this size tier
            efficiencies = []
            for op_data in operations.values():
                if 'storm' in op_data and 'raw' in op_data:
                    eff = (op_data['storm'] / op_data['raw'] * 100) if op_data['raw'] > 0 else 0
                    efficiencies.append(eff)

            if efficiencies:
                avg_eff = sum(efficiencies) / len(efficiencies)
                avg_eff_str = f"{avg_eff:.1f}%"
                print(f"\n{Colors.BOLD}Average Efficiency ({size:,} rows):{Colors.RESET} {BenchmarkTable.color_efficiency(avg_eff_str)}")

        # Calculate overall average efficiency
        all_efficiencies = []
        for size_data in data.values():
            for op_data in size_data.values():
                if 'storm' in op_data and 'raw' in op_data:
                    eff = (op_data['storm'] / op_data['raw'] * 100) if op_data['raw'] > 0 else 0
                    all_efficiencies.append(eff)

        if all_efficiencies:
            overall_avg = sum(all_efficiencies) / len(all_efficiencies)
            overall_str = f"{overall_avg:.1f}%"
            print(f"\n{Colors.BOLD}Overall Average Efficiency:{Colors.RESET} {BenchmarkTable.color_efficiency(overall_str)}")
            print()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM DISTINCT Scaling Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
This benchmark tests DISTINCT operations with different result sizes:
  - ~100 unique rows
  - ~1,000 unique rows
  - ~10,000 unique rows (all unique)

This shows how Storm ORM scales from small to large result sets.

Examples:
  %(prog)s                                # Run with defaults (10K records, 100 iterations)
  %(prog)s --records=50000                # Run with 50K records
  %(prog)s --iterations=200               # Run with 200 iterations per test
        '''
    )

    parser.add_argument('--records', type=int, default=10000,
                       help='Number of records to insert (default: 10000)')
    parser.add_argument('--iterations', type=int, default=100,
                       help='Number of iterations per test (default: 100)')
    parser.add_argument('--binary',
                       default='./build/release/benchmarks/bench_distinct_scaling',
                       help='Path to benchmark binary')

    args = parser.parse_args()

    print(f"{Colors.BOLD}{Colors.CYAN}")
    print("╔════════════════════════════════════════════════════════════════════════════════╗")
    print("║                                                                                ║")
    print("║              Storm ORM DISTINCT Scaling Performance Benchmark                  ║")
    print("║                                                                                ║")
    print("╚════════════════════════════════════════════════════════════════════════════════╝")
    print(Colors.RESET)

    benchmark = DistinctScalingBenchmark(args.binary)
    benchmark.run(
        f'--records={args.records}',
        f'--iterations={args.iterations}',
        records=args.records,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
