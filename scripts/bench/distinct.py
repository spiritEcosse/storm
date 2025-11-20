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
        """Parse DISTINCT benchmark output - extracts both latency and throughput"""
        def extract_metrics(pattern):
            # Extract latency (ms/query)
            latency_match = re.search(pattern + r'.*?Latency:\s+([\d.]+)\s+ms/query', output, re.DOTALL)
            latency = float(latency_match.group(1)) if latency_match else 0.0

            # Extract throughput (rows/sec) - now with decimal support
            throughput_match = re.search(pattern + r'.*?Throughput:\s+([\d.]+)\s+rows/sec', output, re.DOTALL)
            throughput = float(throughput_match.group(1)) if throughput_match else 0.0

            # Extract avg results per query
            results_match = re.search(pattern + r'.*?Avg results:\s+([\d.]+)\s+rows/query', output, re.DOTALL)
            avg_results = float(results_match.group(1)) if results_match else 0.0

            return (latency, throughput, avg_results)

        return {
            # Single-field DISTINCT: (storm_latency, storm_throughput, storm_results, raw_latency, raw_throughput, raw_results)
            'name': (
                *extract_metrics(r'Storm ORM DISTINCT \(name\)'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name\)')
            ),
            'age': (
                *extract_metrics(r'Storm ORM DISTINCT \(age\)'),
                *extract_metrics(r'Raw SQLite DISTINCT \(age\)')
            ),
            'id': (
                *extract_metrics(r'Storm ORM DISTINCT \(id/PK\)'),
                *extract_metrics(r'Raw SQLite DISTINCT \(id\)')
            ),
            # Multi-field DISTINCT
            'name_age': (
                *extract_metrics(r'Storm ORM DISTINCT \(name, age\)'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name, age\)')
            ),
            'id_name_age': (
                *extract_metrics(r'Storm ORM DISTINCT \(id, name, age\)'),
                *extract_metrics(r'Raw SQLite DISTINCT \(id, name, age\)')
            ),
            # DISTINCT with WHERE
            'where': (
                *extract_metrics(r'Storm ORM DISTINCT \(name\) \+ WHERE'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name\) \+ WHERE')
            ),
            # DISTINCT with JOIN
            'join': (
                *extract_metrics(r'Storm ORM DISTINCT \(content\) \+ JOIN'),
                *extract_metrics(r'Raw SQLite DISTINCT \(content\) \+ JOIN')
            ),
            # DISTINCT with WHERE + JOIN
            'where_join': (
                *extract_metrics(r'Storm ORM DISTINCT \(content\) \+ WHERE \+ JOIN'),
                *extract_metrics(r'Raw SQLite DISTINCT \(content\) \+ WHERE \+ JOIN')
            ),
            # DISTINCT with LIMIT/OFFSET (Storm vs Raw comparison)
            'limit_100': (
                *extract_metrics(r'Storm ORM DISTINCT \(name\) \+ LIMIT 100'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name\) \+ LIMIT 100')
            ),
            'limit_offset': (
                *extract_metrics(r'Storm ORM DISTINCT \(name\) \+ LIMIT 50 OFFSET 50'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name\) \+ LIMIT 50 OFFSET 50')
            ),
            'multi_limit': (
                *extract_metrics(r'Storm ORM DISTINCT \(name, age\) \+ LIMIT 100'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name, age\) \+ LIMIT 100')
            ),
            'offset_50': (
                *extract_metrics(r'Storm ORM DISTINCT \(name\) \+ OFFSET 50'),
                *extract_metrics(r'Raw SQLite DISTINCT \(name\) \+ OFFSET 50')
            ),
        }

    def display_results(self, data, records=10000, iterations=100):
        """Display formatted DISTINCT benchmark results with both latency and throughput"""
        from common import FlexibleTable

        # Create custom table with both latency and throughput columns
        table = FlexibleTable(
            headers=[
                f"{Colors.BOLD}DISTINCT Operation{Colors.RESET}",
                f"{Colors.BOLD}Latency (ms){Colors.RESET}",
                f"{Colors.BOLD}Throughput{Colors.RESET}",
                f"{Colors.BOLD}Avg Results{Colors.RESET}",
                f"{Colors.BOLD}Efficiency{Colors.RESET}"
            ],
            column_widths=[50, 24, 22, 14, 14]
        )

        def format_row(label, storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res):
            """Format a single row with Storm ORM / Raw SQLite comparison"""
            # Latency comparison (lower is better, so invert for efficiency)
            lat_eff = (raw_lat / storm_lat * 100) if storm_lat > 0 else 0
            lat_str = f"{Colors.CYAN}{storm_lat:>7.4f}{Colors.RESET} / {Colors.DIM}{raw_lat:>7.4f}{Colors.RESET}"

            # Throughput comparison (higher is better)
            thr_eff = (storm_thr / raw_thr * 100) if raw_thr > 0 else 0
            thr_storm_m = storm_thr / 1_000_000
            thr_raw_m = raw_thr / 1_000_000
            thr_str = f"{Colors.CYAN}{thr_storm_m:>6.2f}M{Colors.RESET} / {Colors.DIM}{thr_raw_m:>6.2f}M{Colors.RESET}"

            # Results comparison
            res_str = f"{Colors.CYAN}{storm_res:>4.0f}{Colors.RESET} / {Colors.DIM}{raw_res:>4.0f}{Colors.RESET}"

            # Overall efficiency (based on latency - lower is better for latency)
            eff = lat_eff
            eff_str = f"{eff:>11.1f}%"
            if eff >= 70:
                eff_colored = f"{Colors.GREEN}{eff_str}{Colors.RESET}"
            elif eff >= 50:
                eff_colored = f"{Colors.YELLOW}{eff_str}{Colors.RESET}"
            else:
                eff_colored = f"{Colors.RED}{eff_str}{Colors.RESET}"

            return [label, lat_str, thr_str, res_str, eff_colored], eff

        table.print_header()

        all_effs = []

        # Single-field DISTINCT operations
        # Add section header as empty/separator row
        table.print_row([f"{Colors.BOLD}Single-Field DISTINCT:{Colors.RESET}", "", "", "", ""])
        for name, label in [
            ('name', 'DISTINCT (name - TEXT)'),
            ('age', 'DISTINCT (age - INTEGER)'),
            ('id', 'DISTINCT (id/PK - INTEGER)'),
        ]:
            if name in data and data[name][0] > 0:  # Check storm_latency exists
                storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res)
                table.print_row(row)
                all_effs.append(eff)

        # Multi-field DISTINCT operations
        table.print_separator()
        table.print_row([f"{Colors.BOLD}Multi-Field DISTINCT:{Colors.RESET}", "", "", "", ""])
        for name, label in [
            ('name_age', 'DISTINCT (name, age)'),
            ('id_name_age', 'DISTINCT (id, name, age)'),
        ]:
            if name in data and data[name][0] > 0:
                storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res)
                table.print_row(row)
                all_effs.append(eff)

        # DISTINCT with WHERE/JOIN operations
        table.print_separator()
        table.print_row([f"{Colors.BOLD}DISTINCT with WHERE/JOIN:{Colors.RESET}", "", "", "", ""])
        for name, label in [
            ('where', 'DISTINCT + WHERE'),
            ('join', 'DISTINCT + JOIN'),
            ('where_join', 'DISTINCT + WHERE + JOIN'),
        ]:
            if name in data and data[name][0] > 0:
                storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res)
                table.print_row(row)
                all_effs.append(eff)

        # DISTINCT with LIMIT/OFFSET operations (Storm vs Raw comparison)
        table.print_separator()
        table.print_row([f"{Colors.BOLD}DISTINCT with LIMIT/OFFSET:{Colors.RESET}", "", "", "", ""])
        for name, label in [
            ('limit_100', 'DISTINCT (name) + LIMIT 100'),
            ('limit_offset', 'DISTINCT (name) + LIMIT 50 OFFSET 50'),
            ('multi_limit', 'DISTINCT (name, age) + LIMIT 100'),
            ('offset_50', 'DISTINCT (name) + OFFSET 50'),
        ]:
            if name in data and data[name][0] > 0:
                storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res = data[name]
                row, eff = format_row(label, storm_lat, storm_thr, storm_res, raw_lat, raw_thr, raw_res)
                table.print_row(row)
                all_effs.append(eff)

        # Close the table
        table.print_footer()

        # Calculate and print average efficiency for all operations
        avg_eff = sum(all_effs) / len(all_effs) if all_effs else 0
        print(f"\n{Colors.DIM}Test configuration: {records:,} records, {iterations} iterations{Colors.RESET}")
        print(f"{Colors.DIM}Format: Storm ORM / Raw SQLite{Colors.RESET}")
        print(f"{Colors.DIM}Latency = primary metric (lower is better){Colors.RESET}")
        print(f"{Colors.DIM}Throughput = output rows/sec (misleading for different result sizes){Colors.RESET}")
        print(f"{Colors.DIM}⚠️  OFFSET without LIMIT requires full table scan (finds all DISTINCT values){Colors.RESET}")
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
