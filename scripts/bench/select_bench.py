#!/usr/bin/env python3
"""
Storm ORM SELECT Performance Benchmark
Benchmarks SELECT operations with LIMIT/OFFSET and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, Colors


class SelectBenchmark(BenchmarkRunner):
    """SELECT performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_storm'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse SELECT benchmark output - extracts latency and throughput"""
        def extract_storm_raw_pair(storm_pattern, raw_pattern):
            # Extract Storm metrics
            storm_match = re.search(storm_pattern, output, re.DOTALL)
            storm_time = float(storm_match.group(1)) if storm_match else 0.0
            storm_rows = int(storm_match.group(2)) if storm_match else 0

            # Extract Raw SQLite metrics
            raw_match = re.search(raw_pattern, output, re.DOTALL)
            raw_time = float(raw_match.group(1)) if raw_match else 0.0
            raw_rows = int(raw_match.group(2)) if raw_match else 0

            return (storm_time, storm_rows, raw_time, raw_rows)

        # Extract basic SELECT throughput
        basic_match = re.search(r'Storm ORM - SELECT \d+ records:.*?Throughput:\s+(\d+)\s+rows/sec', output, re.DOTALL)
        basic_throughput = int(basic_match.group(1)) if basic_match else 0

        return {
            'basic': basic_throughput,
            'limit_100': extract_storm_raw_pair(
                r'Storm ORM SELECT \+ LIMIT 100:.*?Time:\s+([0-9.]+)\s+ms.*?Rows:\s+(\d+)',
                r'Raw SQLite SELECT \+ LIMIT 100:.*?Time:\s+([0-9.]+)\s+ms.*?Rows:\s+(\d+)'
            ),
            'limit_offset': extract_storm_raw_pair(
                r'Storm ORM SELECT \+ LIMIT 100 OFFSET 100:.*?Time:\s+([0-9.]+)\s+ms.*?Rows:\s+(\d+)',
                r'Raw SQLite SELECT \+ LIMIT 100 OFFSET 100:.*?Time:\s+([0-9.]+)\s+ms.*?Rows:\s+(\d+)'
            ),
        }

    def display_results(self, data, records=10000):
        """Display formatted SELECT benchmark results"""
        from common import FlexibleTable

        # Create custom table
        table = FlexibleTable(
            headers=[
                f"{Colors.BOLD}SELECT Operation{Colors.RESET}",
                f"{Colors.BOLD}Latency (ms){Colors.RESET}",
                f"{Colors.BOLD}Rows{Colors.RESET}",
                f"{Colors.BOLD}Efficiency{Colors.RESET}"
            ],
            column_widths=[40, 24, 14, 14]
        )

        def format_row(label, storm_time, storm_rows, raw_time, raw_rows):
            """Format a single row with Storm ORM / Raw SQLite comparison"""
            # Latency comparison (lower is better, so invert for efficiency)
            lat_eff = (raw_time / storm_time * 100) if storm_time > 0 else 0
            lat_str = f"{Colors.CYAN}{storm_time:>7.4f}{Colors.RESET} / {Colors.DIM}{raw_time:>7.4f}{Colors.RESET}"

            # Rows comparison
            rows_str = f"{Colors.CYAN}{storm_rows:>4d}{Colors.RESET} / {Colors.DIM}{raw_rows:>4d}{Colors.RESET}"

            # Overall efficiency (based on latency)
            eff = lat_eff
            eff_str = f"{eff:>11.1f}%"
            if eff >= 70:
                eff_colored = f"{Colors.GREEN}{eff_str}{Colors.RESET}"
            elif eff >= 50:
                eff_colored = f"{Colors.YELLOW}{eff_str}{Colors.RESET}"
            else:
                eff_colored = f"{Colors.RED}{eff_str}{Colors.RESET}"

            return [label, lat_str, rows_str, eff_colored], eff

        table.print_header()

        all_effs = []

        # Basic SELECT (no comparison, just Storm ORM)
        table.print_row([f"{Colors.BOLD}Basic SELECT:{Colors.RESET}", "", "", ""])
        if 'basic' in data and data['basic'] > 0:
            throughput_m = data['basic'] / 1_000_000
            table.print_row([
                f"SELECT all {records:,} records",
                f"{Colors.CYAN}{throughput_m:>6.2f}M rows/sec{Colors.RESET}",
                f"{Colors.CYAN}{records:>4d}{Colors.RESET}",
                ""
            ])

        # LIMIT/OFFSET operations
        table.print_separator()
        table.print_row([f"{Colors.BOLD}SELECT with LIMIT/OFFSET:{Colors.RESET}", "", "", ""])

        if 'limit_100' in data and data['limit_100'][0] > 0:
            storm_time, storm_rows, raw_time, raw_rows = data['limit_100']
            row, eff = format_row('SELECT + LIMIT 100', storm_time, storm_rows, raw_time, raw_rows)
            table.print_row(row)
            all_effs.append(eff)

        if 'limit_offset' in data and data['limit_offset'][0] > 0:
            storm_time, storm_rows, raw_time, raw_rows = data['limit_offset']
            row, eff = format_row('SELECT + LIMIT 100 OFFSET 100', storm_time, storm_rows, raw_time, raw_rows)
            table.print_row(row)
            all_effs.append(eff)

        # Close the table
        table.print_footer()

        # Calculate and print average efficiency
        avg_eff = sum(all_effs) / len(all_effs) if all_effs else 0
        print(f"\n{Colors.DIM}Test configuration: {records:,} records{Colors.RESET}")
        print(f"{Colors.DIM}Format: Storm ORM / Raw SQLite{Colors.RESET}")
        print(f"{Colors.DIM}Latency = primary metric (lower is better){Colors.RESET}")

        if all_effs:
            avg_eff_str = f"{avg_eff:>11.1f}%"
            if avg_eff >= 70:
                avg_eff_colored = f"{Colors.GREEN}{avg_eff_str}{Colors.RESET}"
            elif avg_eff >= 50:
                avg_eff_colored = f"{Colors.YELLOW}{avg_eff_str}{Colors.RESET}"
            else:
                avg_eff_colored = f"{Colors.RED}{avg_eff_str}{Colors.RESET}"
            print(f"\n{Colors.BOLD}Average LIMIT/OFFSET Efficiency:{Colors.RESET} {avg_eff_colored}")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM SELECT Performance Benchmark',
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
    benchmark = SelectBenchmark(args.binary)
    benchmark.run(
        '--mode=select-only',
        f'--test-size={args.records}',
        records=args.records
    )


if __name__ == '__main__':
    main()
