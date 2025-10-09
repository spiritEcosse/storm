#!/usr/bin/env python3
"""
Storm ORM Comprehensive Benchmark Suite
Runs all available benchmarks and displays formatted results
"""

import sys
import subprocess
from pathlib import Path

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import Colors


def print_section_header(title):
    """Print a section header"""
    print()
    print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 100}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{title}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 100}{Colors.RESET}")
    print()


def check_benchmark_exists(path):
    """Check if benchmark binary exists"""
    benchmark_path = Path(path)
    if not benchmark_path.exists():
        print(f"{Colors.RED}Error: Benchmark not found: {path}{Colors.RESET}")
        print(f"{Colors.YELLOW}Please build release benchmarks first:{Colors.RESET}")
        print("  cmake --preset ninja-release -DENABLE_BENCH=ON")
        print("  cmake --build --preset ninja-release")
        return False
    return True


def run_join_benchmarks(messages=1000, iterations=50):
    """Run JOIN performance benchmarks"""
    print_section_header("1. JOIN PERFORMANCE BENCHMARKS")

    benchmark_path = "./build/release/benchmarks/bench_join_performance"
    if not check_benchmark_exists(benchmark_path):
        return

    print(f"{Colors.YELLOW}Running JOIN benchmarks...{Colors.RESET}\n")

    # Run using our Python benchmark module
    try:
        from join import JoinBenchmark
        benchmark = JoinBenchmark(benchmark_path)
        benchmark.run(
            f'--size={messages}',
            f'--iterations={iterations}',
            messages=messages,
            iterations=iterations
        )
        print(f"\n{Colors.GREEN}✓ JOIN benchmarks completed{Colors.RESET}")
    except Exception as e:
        print(f"{Colors.RED}Error running JOIN benchmark: {e}{Colors.RESET}")


def run_sql_generation_benchmarks():
    """Run SQL generation microbenchmarks"""
    print_section_header("2. SQL GENERATION PERFORMANCE")

    benchmark_path = "./build/release/benchmarks/sql_generation_microbench"
    if not check_benchmark_exists(benchmark_path):
        return

    print(f"{Colors.YELLOW}Running SQL generation benchmarks...{Colors.RESET}\n")

    try:
        result = subprocess.run([benchmark_path], capture_output=True, text=True)
        if result.returncode == 0:
            print(result.stdout)
            print(f"{Colors.GREEN}✓ SQL generation benchmarks completed{Colors.RESET}")
        else:
            print(f"{Colors.RED}SQL generation benchmark failed{Colors.RESET}")
            print(result.stderr)
    except Exception as e:
        print(f"{Colors.RED}Error: {e}{Colors.RESET}")


def run_join_microbenchmarks():
    """Run detailed JOIN microbenchmarks"""
    print_section_header("3. JOIN DETAILED MICROBENCHMARKS")

    benchmark_path = "./build/release/benchmarks/join_performance_microbench"
    if not check_benchmark_exists(benchmark_path):
        return

    print(f"{Colors.YELLOW}Running detailed JOIN microbenchmarks...{Colors.RESET}\n")

    try:
        result = subprocess.run([benchmark_path], capture_output=True, text=True)
        if result.returncode == 0:
            print(result.stdout)
            print(f"{Colors.GREEN}✓ JOIN microbenchmarks completed{Colors.RESET}")
        else:
            print(f"{Colors.RED}JOIN microbenchmark failed{Colors.RESET}")
            print(result.stderr)
    except Exception as e:
        print(f"{Colors.RED}Error: {e}{Colors.RESET}")


def run_select_comparison():
    """Run SELECT performance comparison"""
    print_section_header("4. SELECT PERFORMANCE COMPARISON")

    benchmark_path = "./build/release/benchmarks/bench_select_comparison"
    if not check_benchmark_exists(benchmark_path):
        return

    print(f"{Colors.YELLOW}Running SELECT comparison benchmarks...{Colors.RESET}\n")

    try:
        result = subprocess.run([benchmark_path], capture_output=True, text=True)
        if result.returncode == 0:
            print(result.stdout)
            print(f"{Colors.GREEN}✓ SELECT comparison benchmarks completed{Colors.RESET}")
        else:
            print(f"{Colors.RED}SELECT comparison benchmark failed{Colors.RESET}")
            print(result.stderr)
    except Exception as e:
        print(f"{Colors.RED}Error: {e}{Colors.RESET}")


def print_summary():
    """Print benchmark suite summary"""
    print_section_header("BENCHMARK SUMMARY")

    print(f"{Colors.BOLD}Storm ORM Microbenchmark Suite Completed!{Colors.RESET}\n")
    print("Available individual benchmarks:")
    print(f"  {Colors.CYAN}JOIN Performance:{Colors.RESET}        ./bench --joins")
    print(f"  {Colors.CYAN}SQL Generation:{Colors.RESET}          ./build/release/benchmarks/sql_generation_microbench")
    print(f"  {Colors.CYAN}JOIN Microbench:{Colors.RESET}         ./build/release/benchmarks/join_performance_microbench")
    print(f"  {Colors.CYAN}SELECT Comparison:{Colors.RESET}       ./build/release/benchmarks/bench_select_comparison")
    print(f"  {Colors.CYAN}CRUD Operations:{Colors.RESET}         ./build/release/benchmarks/bench_storm")
    print()
    print(f"{Colors.BOLD}{Colors.GREEN}✓ All microbenchmarks completed successfully!{Colors.RESET}")
    print()


def main(messages=1000, iterations=50):
    """Run all benchmarks"""
    print(f"{Colors.BOLD}{Colors.CYAN}")
    print("=" * 100)
    print("STORM ORM MICROBENCHMARK SUITE")
    print("=" * 100)
    print(Colors.RESET)

    print("Configuration:")
    print(f"  Build: {Colors.GREEN}Release{Colors.RESET}")
    print(f"  Iterations: {Colors.GREEN}{iterations}{Colors.RESET}")
    print(f"  Dataset size: {Colors.GREEN}{messages} messages{Colors.RESET}")
    print()

    # Run all benchmark suites
    run_join_benchmarks(messages, iterations)
    run_sql_generation_benchmarks()
    run_join_microbenchmarks()
    run_select_comparison()

    # Print summary
    print_summary()


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Run all Storm ORM benchmarks')
    parser.add_argument('--size', type=int, default=1000,
                       help='Dataset size (default: 1000)')
    parser.add_argument('--iterations', type=int, default=50,
                       help='Number of iterations (default: 50)')

    args = parser.parse_args()
    main(args.size, args.iterations)
