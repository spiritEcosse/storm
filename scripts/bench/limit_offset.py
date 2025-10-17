#!/usr/bin/env python3
"""
Storm ORM LIMIT/OFFSET Performance Benchmark
Benchmarks LIMIT/OFFSET operations and displays color-coded comparison table
"""

import sys
import re
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, BenchmarkTable, Colors


class LimitOffsetBenchmark(BenchmarkRunner):
    """LIMIT/OFFSET performance benchmark runner"""

    def __init__(self, binary_path='./build/release/benchmarks/bench_limit'):
        super().__init__(binary_path)
        self.simple_select_binary = './build/release/benchmarks/bench_simple_select'
        self.simple_select_output = ''  # Store simple select output

    def parse_results(self, output):
        """Parse LIMIT/OFFSET benchmark output"""
        # Combine outputs (simple select + limit/offset)
        combined_output = self.simple_select_output + '\n' + output

        def extract_throughput(pattern):
            # Find the section starting with pattern
            section_match = re.search(pattern + r'[^\n]*\n(.*?)(?===|$)', combined_output, re.DOTALL)
            if section_match:
                section_text = section_match.group(1)
                # Now find throughput within this section only
                throughput_match = re.search(r'Throughput:\s+(\d+\.\d+)\s+M\s+rows/sec', section_text)
                if throughput_match:
                    return int(float(throughput_match.group(1)) * 1_000_000)
            return 0

        return {
            'select': (
                extract_throughput(r'Storm ORM - Simple SELECT'),
                extract_throughput(r'Raw SQLite - Simple SELECT')
            ),
            'limit': (
                extract_throughput(r'Storm ORM - SELECT with LIMIT \d+'),
                extract_throughput(r'Raw SQLite - SELECT with LIMIT \d+')
            ),
            'limit_offset': (
                extract_throughput(r'Storm ORM - SELECT with LIMIT \d+ OFFSET'),
                extract_throughput(r'Raw SQLite - SELECT with LIMIT \d+ OFFSET')
            ),
            'offset': (
                extract_throughput(r'Storm ORM - SELECT with OFFSET \d+'),
                extract_throughput(r'Raw SQLite - SELECT with OFFSET \d+')
            ),
            'join_limit': (
                extract_throughput(r'Storm ORM - JOIN[^\n]*with LIMIT'),
                extract_throughput(r'Raw SQLite - JOIN[^\n]*with LIMIT')
            ),
        }

    def display_results(self, data, messages=10000, limit=100, offset=5000, iterations=100):
        """Display formatted LIMIT/OFFSET benchmark results"""

        # Print header
        BenchmarkTable.print_header("LIMIT/OFFSET Operation")

        # Simple SELECT baseline (no LIMIT/OFFSET)
        storm_select, raw_select = data['select']
        print(f"│ {'Simple SELECT (baseline)':<31} │ {storm_select/1_000_000:>11.2f}M │ {raw_select/1_000_000:>11.2f}M │ {'(baseline)':<12} │")
        BenchmarkTable.print_separator()

        # LIMIT operations
        for name, label in [
            ('limit', f'LIMIT {limit}'),
            ('limit_offset', f'LIMIT {limit} + OFFSET {offset}'),
            ('offset', f'OFFSET {offset} only'),
        ]:
            storm, raw = data[name]
            eff = (storm / raw * 100) if raw > 0 else 0
            BenchmarkTable.print_row(label, storm, raw, eff)

        BenchmarkTable.print_separator()

        # JOIN with LIMIT/OFFSET
        storm, raw = data['join_limit']
        eff = (storm / raw * 100) if raw > 0 else 0
        BenchmarkTable.print_row(f'JOIN + LIMIT {limit} + OFFSET {offset}', storm, raw, eff)

        BenchmarkTable.print_footer()

        # Calculate and print average efficiency (excluding baseline)
        all_effs = [(data[k][0] / data[k][1] * 100) if data[k][1] > 0 else 0
                    for k in data.keys() if k != 'select']
        avg_eff = sum(all_effs) / len(all_effs)

        BenchmarkTable.print_summary(messages, iterations, avg_eff)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM LIMIT/OFFSET Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                                # Run with defaults
  %(prog)s --messages 50000              # Test with 50k messages
  %(prog)s --limit 500 --offset 10000    # Custom pagination settings
  %(prog)s --iterations 200              # More iterations for stability
  %(prog)s --binary ./custom/path        # Custom binary location
        '''
    )

    parser.add_argument(
        '--binary',
        default='./build/release/benchmarks/bench_limit',
        help='Path to benchmark binary (default: %(default)s)'
    )
    parser.add_argument(
        '--messages',
        type=int,
        default=10000,
        help='Number of messages (default: %(default)s)'
    )
    parser.add_argument(
        '--limit',
        type=int,
        default=100,
        help='LIMIT size (default: %(default)s)'
    )
    parser.add_argument(
        '--offset',
        type=int,
        default=None,
        help='OFFSET size (default: 50%% of messages)'
    )
    parser.add_argument(
        '--iterations',
        type=int,
        default=100,
        help='Number of iterations (default: %(default)s)'
    )

    args = parser.parse_args()

    # Calculate sensible default offset (50% of messages) if not specified
    offset = args.offset if args.offset is not None else (args.messages // 2)

    # Run benchmarks
    benchmark = LimitOffsetBenchmark(args.binary)

    # First, run simple SELECT benchmark
    import subprocess
    print(f"{Colors.CYAN}Running Simple SELECT benchmark...{Colors.RESET}")
    try:
        simple_select_result = subprocess.run(
            [benchmark.simple_select_binary,
             f'--iterations={args.iterations}'],
            capture_output=True,
            text=True,
            check=True
        )
        benchmark.simple_select_output = simple_select_result.stdout
        print(benchmark.simple_select_output)
    except subprocess.CalledProcessError as e:
        print(f"{Colors.RED}Simple SELECT benchmark failed: {e}{Colors.RESET}")
        print(f"Error: {e.stderr}")
    except FileNotFoundError:
        print(f"{Colors.YELLOW}Warning: bench_simple_select binary not found at {benchmark.simple_select_binary}{Colors.RESET}")
        print(f"{Colors.YELLOW}Building bench_simple_select...{Colors.RESET}")
        # Try to build it
        try:
            build_result = subprocess.run(
                ['cmake', '--build', str(benchmark.build_dir), '--target', 'bench_simple_select'],
                capture_output=True,
                text=True,
                check=True
            )
            print(f"{Colors.GREEN}Build successful{Colors.RESET}")
            # Try running again
            simple_select_result = subprocess.run(
                [benchmark.simple_select_binary,
                 f'--iterations={args.iterations}'],
                capture_output=True,
                text=True,
                check=True
            )
            benchmark.simple_select_output = simple_select_result.stdout
            print(benchmark.simple_select_output)
        except Exception as build_error:
            print(f"{Colors.RED}Could not build bench_simple_select: {build_error}{Colors.RESET}")

    # Then run LIMIT/OFFSET benchmark
    print(f"\n{Colors.CYAN}Running LIMIT/OFFSET benchmark...{Colors.RESET}")

    # Now run the benchmark (it will use self.simple_select_output in parse_results)
    benchmark.run(
        f'--messages={args.messages}',
        f'--limit={args.limit}',
        f'--offset={offset}',
        f'--iterations={args.iterations}',
        messages=args.messages,
        limit=args.limit,
        offset=offset,
        iterations=args.iterations
    )


if __name__ == '__main__':
    main()
