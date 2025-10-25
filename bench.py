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


def run_distinct_benchmark(args):
    """Run DISTINCT performance benchmark"""
    from distinct import DistinctBenchmark

    print_header()
    print(f"{Colors.GREEN}Running DISTINCT Performance Analysis...{Colors.RESET}\n")

    # Use specific binary path for distinct benchmark
    binary_path = './build/release/benchmarks/bench_distinct'
    benchmark = DistinctBenchmark(binary_path)
    benchmark.run(
        f'--size={args.size or 10000}',
        f'--iterations={args.iterations or 100}',
        records=args.size or 10000,
        iterations=args.iterations or 100
    )


def run_distinct_scaling_benchmark(args):
    """Run DISTINCT scaling performance benchmark"""
    from distinct_scaling import DistinctScalingBenchmark

    print_header()
    print(f"{Colors.GREEN}Running DISTINCT Scaling Performance Analysis...{Colors.RESET}\n")

    binary_path = './build/release/benchmarks/bench_distinct_scaling'
    benchmark = DistinctScalingBenchmark(binary_path)
    benchmark.run(
        f'--records={args.size or 10000}',
        f'--iterations={args.iterations or 100}',
        records=args.size or 10000,
        iterations=args.iterations or 100
    )


def run_aggregate_benchmark(args):
    """Run aggregate functions performance benchmark"""
    from aggregate import AggregateBenchmark

    print_header()
    print(f"{Colors.GREEN}Running Aggregate Functions Performance Analysis...{Colors.RESET}\n")

    binary_path = './build/release/benchmarks/bench_aggregate'
    benchmark = AggregateBenchmark(binary_path)
    benchmark.run(
        f'--size={args.size or 10000}',
        f'--iterations={args.iterations or 100}',
        show_raw_output=args.report if hasattr(args, 'report') else False,
        rows=args.size or 10000,
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


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Storm ORM Performance Benchmark Suite',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Available Benchmarks:
  📊 JOIN Performance          - Compare Storm ORM vs Raw SQLite JOIN operations
  📊 DISTINCT Performance      - Compare Storm ORM vs Raw SQLite DISTINCT operations
  📊 DISTINCT Scaling          - Show DISTINCT performance across different result sizes
  📊 Aggregate Functions       - Compare Storm ORM vs Raw SQLite aggregate operations (SUM, COUNT, AVG, MIN, MAX)
  📊 SQL Generation            - Analyze compile-time SQL generation performance
  📊 All Microbenchmarks       - Run complete benchmark suite
  📊 Performance Comparison    - Comprehensive Storm vs sqlite_orm vs Raw SQLite comparison

Examples:
  %(prog)s --joins                        # Run JOIN benchmarks
  %(prog)s --joins --size=50000           # Run JOIN with 50K messages
  %(prog)s --distinct                     # Run DISTINCT benchmarks
  %(prog)s --distinct --size=50000        # Run DISTINCT with 50K records
  %(prog)s --distinct-scaling             # Run DISTINCT scaling analysis
  %(prog)s --distinct-scaling --size=50000 # DISTINCT scaling with 50K records
  %(prog)s --aggregate                    # Run aggregate function benchmarks
  %(prog)s --aggregate --size=50000       # Run aggregate with 50K rows
  %(prog)s --all                          # Run all benchmarks
  %(prog)s --compare                      # Run comprehensive performance comparison
        '''
    )

    # Commands
    commands = parser.add_mutually_exclusive_group(required=True)
    commands.add_argument('--joins', action='store_true',
                         help='Run JOIN performance analysis (Storm vs Raw SQLite)')
    commands.add_argument('--distinct', action='store_true',
                         help='Run DISTINCT performance analysis (Storm vs Raw SQLite)')
    commands.add_argument('--distinct-scaling', action='store_true',
                         help='Run DISTINCT scaling analysis (100, 1K, 10K unique results)')
    commands.add_argument('--aggregate', action='store_true',
                         help='Run aggregate functions performance analysis (SUM, COUNT, AVG, MIN, MAX)')
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
    parser.add_argument('--report', action='store_true',
                       help='Show raw benchmark output instead of formatted table')

    args = parser.parse_args()

    # Execute command
    try:
        if args.joins:
            run_join_benchmark(args)
        elif args.distinct:
            run_distinct_benchmark(args)
        elif args.distinct_scaling:
            run_distinct_scaling_benchmark(args)
        elif args.aggregate:
            run_aggregate_benchmark(args)
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
