#!/usr/bin/env python3
"""
Storm ORM Performance Benchmark Suite
Unified entry point for all benchmark scripts
"""

import sys
import argparse
from pathlib import Path

# Add scripts/bench directory to path for imports
bench_dir = Path(__file__).parent / 'scripts' / 'bench'
sys.path.insert(0, str(bench_dir))
from common import Colors


def print_header():
    """Print benchmark suite header"""
    print(f"{Colors.BOLD}{Colors.CYAN}")
    print("╔════════════════════════════════════════════════════════════════════════════════╗")
    print("║                                                                                ║")
    print("║                   Storm ORM Performance Benchmark Suite                        ║")
    print("║                                                                                ║")
    print("╚════════════════════════════════════════════════════════════════════════════════╝")
    print(Colors.RESET)


def run_join_benchmark(args):
    """Run JOIN performance benchmark"""
    from join import JoinBenchmark

    print_header()
    print(f"{Colors.GREEN}Running JOIN Performance Analysis...{Colors.RESET}\n")

    benchmark = JoinBenchmark(args.binary)
    benchmark.run(
        f'--size={args.size or 10000}',
        f'--iterations={args.iterations or 100}',
        messages=args.size or 10000,
        iterations=args.iterations or 100
    )


def run_sql_gen_benchmark(args):
    """Run SQL generation benchmark"""
    from sql_gen import SQLGenerationBenchmark

    print_header()
    print(f"{Colors.GREEN}Running SQL Generation Analysis...{Colors.RESET}\n")

    try:
        benchmark = SQLGenerationBenchmark(args.binary if hasattr(args, 'binary') else './build/debug/benchmarks/sql_generation_microbench')
        benchmark.run()
    except Exception as e:
        print(f"{Colors.RED}SQL generation benchmark failed: {e}{Colors.RESET}")


def run_all_benchmarks(args):
    """Run all benchmarks"""
    from all import main as run_all_main

    print_header()
    print(f"{Colors.GREEN}Running All Microbenchmarks...{Colors.RESET}\n")

    # Run comprehensive benchmark suite
    try:
        run_all_main(
            messages=args.size or 1000,
            iterations=args.iterations or 50
        )
    except Exception as e:
        print(f"{Colors.RED}Benchmark suite failed: {e}{Colors.RESET}")
        import traceback
        traceback.print_exc()


def run_perf_comparison(args):
    """Run comprehensive performance comparison"""
    from compare import performance_comparison

    # Run the performance comparison
    try:
        performance_comparison()
    except Exception as e:
        print(f"{Colors.RED}Performance comparison failed: {e}{Colors.RESET}")
        import traceback
        traceback.print_exc()


def run_limit_offset_benchmark(args):
    """Run LIMIT/OFFSET performance benchmark"""
    from limit_offset import LimitOffsetBenchmark
    import subprocess

    print_header()
    print(f"{Colors.GREEN}Running LIMIT/OFFSET Performance Analysis...{Colors.RESET}\n")

    # Calculate sensible default offset (50% of messages) if not specified
    messages = args.messages or 10000
    offset = args.offset if args.offset is not None else (messages // 2)
    iterations = args.iterations or 100

    # Use bench_limit binary (not the default bench_join)
    binary = './build/release/benchmarks/bench_limit'

    benchmark = LimitOffsetBenchmark(binary)

    # First, run simple SELECT benchmark
    print(f"{Colors.CYAN}Running Simple SELECT benchmark...{Colors.RESET}")
    try:
        simple_select_result = subprocess.run(
            [benchmark.simple_select_binary,
             f'--iterations={iterations}'],
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
                 f'--iterations={iterations}'],
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

    benchmark.run(
        f'--messages={messages}',
        f'--limit={args.limit or 100}',
        f'--offset={offset}',
        f'--iterations={iterations}',
        messages=messages,
        limit=args.limit or 100,
        offset=offset,
        iterations=iterations
    )


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM Performance Benchmark Suite',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Available Benchmarks:
  📊 JOIN Performance       - Compare Storm ORM vs Raw SQLite JOIN operations
  📊 LIMIT/OFFSET           - Analyze pagination and result limiting performance
  📊 SQL Generation         - Analyze compile-time SQL generation performance
  📊 All Microbenchmarks    - Run complete benchmark suite
  📊 Performance Comparison - Comprehensive Storm vs sqlite_orm vs Raw SQLite comparison

Examples:
  %(prog)s --joins                        # Run JOIN benchmarks
  %(prog)s --joins --size=50000           # Run JOIN with 50K messages
  %(prog)s --limit-offset                 # Run LIMIT/OFFSET benchmarks
  %(prog)s --limit-offset --messages=50000 --limit=500  # Custom pagination settings
  %(prog)s --all                          # Run all benchmarks
  %(prog)s --compare                      # Run comprehensive performance comparison
        '''
    )

    # Commands
    commands = parser.add_mutually_exclusive_group(required=True)
    commands.add_argument('--joins', action='store_true',
                         help='Run JOIN performance analysis (Storm vs Raw SQLite)')
    commands.add_argument('--limit-offset', action='store_true',
                         help='Run LIMIT/OFFSET pagination performance analysis')
    commands.add_argument('--sql-gen', action='store_true',
                         help='Run SQL generation performance analysis')
    commands.add_argument('--all', action='store_true',
                         help='Run all microbenchmarks')
    commands.add_argument('--compare', action='store_true',
                         help='Run comprehensive performance comparison (Storm vs sqlite_orm vs Raw SQLite)')

    # Options
    parser.add_argument('--size', type=int,
                       help='Set dataset size (default varies by benchmark)')
    parser.add_argument('--iterations', type=int,
                       help='Set number of iterations (default varies by benchmark)')
    parser.add_argument('--binary', default='./build/release/benchmarks/bench_join',
                       help='Path to benchmark binary (default: %(default)s)')

    # LIMIT/OFFSET specific options
    parser.add_argument('--messages', type=int,
                       help='Number of messages for LIMIT/OFFSET benchmark (default: 10000)')
    parser.add_argument('--limit', type=int,
                       help='LIMIT size for pagination (default: 100)')
    parser.add_argument('--offset', type=int,
                       help='OFFSET size for pagination (default: 50%% of messages)')

    args = parser.parse_args()

    # Execute command
    try:
        if args.joins:
            run_join_benchmark(args)
        elif args.limit_offset:
            run_limit_offset_benchmark(args)
        elif args.sql_gen:
            run_sql_gen_benchmark(args)
        elif args.all:
            run_all_benchmarks(args)
        elif args.compare:
            run_perf_comparison(args)
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Benchmark interrupted by user{Colors.RESET}")
        sys.exit(130)
    except Exception as e:
        print(f"{Colors.RED}Error: {e}{Colors.RESET}")
        sys.exit(1)


if __name__ == '__main__':
    main()
