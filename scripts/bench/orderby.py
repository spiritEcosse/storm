#!/usr/bin/env python3
"""
Storm ORM ORDER BY Performance Benchmark
Benchmarks ORDER BY operations and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class OrderByBenchmark(BenchmarkRunner):
    """ORDER BY performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_storm'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse ORDER BY benchmark output - extracts latency and throughput"""
        def extract_metrics(pattern):
            # Extract Storm metrics
            storm_match = re.search(pattern + r'.*?Storm:\s+([0-9.]+)\s+ms\s+\(([0-9.]+)M rows/sec\)', output, re.DOTALL)
            storm_lat = float(storm_match.group(1)) if storm_match else 0.0
            storm_thr = float(storm_match.group(2)) if storm_match else 0.0

            # Extract Raw SQLite metrics
            raw_match = re.search(pattern + r'.*?Raw:\s+([0-9.]+)\s+ms\s+\(([0-9.]+)M rows/sec\)', output, re.DOTALL)
            raw_lat = float(raw_match.group(1)) if raw_match else 0.0
            raw_thr = float(raw_match.group(2)) if raw_match else 0.0

            return (storm_lat, storm_thr, raw_lat, raw_thr)

        return {
            'age_asc': extract_metrics(r'ORDER BY age ASC:'),
            'name_desc': extract_metrics(r'ORDER BY name DESC:'),
            'multi': extract_metrics(r'ORDER BY age, name \(both ASC\):'),
            'limit': extract_metrics(r'ORDER BY age \+ LIMIT 100:'),
        }

    def display_results(self, data, records=10000):
        """Display formatted ORDER BY benchmark results"""
        from common import FlexibleTable

        # Create custom table with latency and throughput columns
        table = FlexibleTable(
            headers=[
                f"{Colors.BOLD}ORDER BY Operation{Colors.RESET}",
                f"{Colors.BOLD}Latency (ms){Colors.RESET}",
                f"{Colors.BOLD}Throughput{Colors.RESET}",
                f"{Colors.BOLD}Efficiency{Colors.RESET}"
            ],
            column_widths=[40, 24, 22, 14]
        )

        def format_row(label, storm_lat, storm_thr, raw_lat, raw_thr):
            """Format a single row with Storm ORM / Raw SQLite comparison"""
            # Latency comparison (lower is better, so invert for efficiency)
            lat_eff = (raw_lat / storm_lat * 100) if storm_lat > 0 else 0
            lat_str = f"{Colors.CYAN}{storm_lat:>7.4f}{Colors.RESET} / {Colors.DIM}{raw_lat:>7.4f}{Colors.RESET}"

            # Throughput comparison (higher is better)
            thr_str = f"{Colors.CYAN}{storm_thr:>6.2f}M{Colors.RESET} / {Colors.DIM}{raw_thr:>6.2f}M{Colors.RESET}"

            # Overall efficiency (based on latency - lower is better for latency)
            eff = lat_eff
            eff_str = f"{eff:>11.1f}%"
            if eff >= 70:
                eff_colored = f"{Colors.GREEN}{eff_str}{Colors.RESET}"
            elif eff >= 50:
                eff_colored = f"{Colors.YELLOW}{eff_str}{Colors.RESET}"
            else:
                eff_colored = f"{Colors.RED}{eff_str}{Colors.RESET}"

            return [label, lat_str, thr_str, eff_colored], eff

        table.print_header()

        all_effs = []

        # Single-field ORDER BY operations
        table.print_row([f"{Colors.BOLD}Single-Field ORDER BY:{Colors.RESET}", "", "", ""])
        for name, label in [
            ('age_asc', 'ORDER BY age ASC (INTEGER)'),
            ('name_desc', 'ORDER BY name DESC (TEXT)'),
        ]:
            if name in data and data[name][0] > 0:  # Check storm_latency exists
                storm_lat, storm_thr, raw_lat, raw_thr = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, raw_lat, raw_thr)
                table.print_row(row)
                all_effs.append(eff)

        # Multi-field ORDER BY operations
        table.print_separator()
        table.print_row([f"{Colors.BOLD}Multi-Field ORDER BY:{Colors.RESET}", "", "", ""])
        for name, label in [
            ('multi', 'ORDER BY age, name (both ASC)'),
        ]:
            if name in data and data[name][0] > 0:
                storm_lat, storm_thr, raw_lat, raw_thr = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, raw_lat, raw_thr)
                table.print_row(row)
                all_effs.append(eff)

        # ORDER BY with LIMIT operations
        table.print_separator()
        table.print_row([f"{Colors.BOLD}ORDER BY with LIMIT:{Colors.RESET}", "", "", ""])
        for name, label in [
            ('limit', 'ORDER BY age + LIMIT 100'),
        ]:
            if name in data and data[name][0] > 0:
                storm_lat, storm_thr, raw_lat, raw_thr = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, raw_lat, raw_thr)
                table.print_row(row)
                all_effs.append(eff)

        # Close the table
        table.print_footer()

        # Calculate and print average efficiency for all operations
        avg_eff = sum(all_effs) / len(all_effs) if all_effs else 0
        print(f"\n{Colors.DIM}Test configuration: {records:,} records{Colors.RESET}")
        print(f"{Colors.DIM}Format: Storm ORM / Raw SQLite{Colors.RESET}")
        print(f"{Colors.DIM}Latency = primary metric (lower is better){Colors.RESET}")
        avg_eff_str = f"{avg_eff:>11.1f}%"
        if avg_eff >= 70:
            avg_eff_colored = f"{Colors.GREEN}{avg_eff_str}{Colors.RESET}"
        elif avg_eff >= 50:
            avg_eff_colored = f"{Colors.YELLOW}{avg_eff_str}{Colors.RESET}"
        else:
            avg_eff_colored = f"{Colors.RED}{avg_eff_str}{Colors.RESET}"
        print(f"\n{Colors.BOLD}Average Efficiency:{Colors.RESET} {avg_eff_colored}")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM ORDER BY Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults
  %(prog)s --records 50000               # Test with 50k records
  %(prog)s --binary ./custom/path        # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_storm',
        help='Path to benchmark binary (default: %(default)s)'
    )
    parser.add_argument(
        '--records',
        type=int,
        default=10000,
        help='Number of records (default: %(default)s)'
    )

    args = parser.parse_args()

    # Run benchmark
    benchmark = OrderByBenchmark(args.binary)
    benchmark.run(
        '--mode=order-by',
        f'--test-size={args.records}',
        records=args.records
    )


if __name__ == '__main__':
    main()
