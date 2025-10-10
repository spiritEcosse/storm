#!/usr/bin/env python3
"""
Storm ORM Microbenchmark Suite
Comprehensive performance testing for JOIN, SQL generation, and SELECT operations
"""

import subprocess
import sys
import re
from pathlib import Path
from typing import Optional, Dict, List, Tuple


class Colors:
    """ANSI color codes for terminal output"""
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    NC = '\033[0m'  # No Color
    BOLD = '\033[1m'


class Config:
    """Benchmark configuration"""
    BUILD_DIR = Path("build/release")
    BENCHMARK_DIR = BUILD_DIR / "benchmarks"
    ITERATIONS = 50
    MESSAGE_COUNT = 1000


class BenchmarkRunner:
    """Main benchmark runner class"""

    def __init__(self):
        self.config = Config()

    def print_header(self, text: str) -> None:
        """Print a section header"""
        print()
        print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 104}{Colors.NC}")
        print(f"{Colors.BOLD}{Colors.CYAN}{text}{Colors.NC}")
        print(f"{Colors.BOLD}{Colors.CYAN}{'=' * 104}{Colors.NC}")
        print()

    def print_table_header(self, title: str) -> None:
        """Print a table header"""
        print(f"{Colors.BOLD}{Colors.BLUE}{title}{Colors.NC}")
        print(f"{Colors.BOLD}{'Operation':<50} {'Avg Time':>15} {'Throughput':>20} {'vs Baseline':>15}{Colors.NC}")
        print('-' * 105)

    def print_table_row(self, operation: str, avg_time: str, throughput: str, vs_baseline: str) -> None:
        """Print a table row"""
        print(f"{operation:<50} {avg_time:>15} {throughput:>20} {vs_baseline:>15}")

    def check_benchmark(self, bench_path: Path) -> bool:
        """Check if benchmark executable exists"""
        if not bench_path.exists():
            print(f"{Colors.RED}Error: Benchmark not found: {bench_path}{Colors.NC}")
            print(f"{Colors.YELLOW}Please build release benchmarks first:{Colors.NC}")
            print("  cmake --preset ninja-release -DENABLE_BENCH=ON")
            print("  cmake --build --preset ninja-release")
            return False
        return True

    def build_benchmarks(self) -> bool:
        """Build benchmarks if they don't exist"""
        bench_join = self.config.BENCHMARK_DIR / "bench_join_performance"

        if not bench_join.exists():
            print(f"{Colors.RED}Benchmarks not found. Building release benchmarks...{Colors.NC}")

            # Configure
            result = subprocess.run(
                ["cmake", "--preset", "ninja-release", "-DENABLE_BENCH=ON"],
                capture_output=True,
                text=True
            )
            if result.returncode != 0:
                print(f"{Colors.RED}Failed to configure benchmarks{Colors.NC}")
                print(result.stderr)
                return False

            # Build
            result = subprocess.run(
                ["cmake", "--build", "--preset", "ninja-release"],
                capture_output=True,
                text=True
            )
            if result.returncode != 0:
                print(f"{Colors.RED}Failed to build benchmarks{Colors.NC}")
                print(result.stderr)
                return False

            print(f"{Colors.GREEN}✓ Benchmarks built successfully{Colors.NC}")

        return True

    def run_benchmark(self, bench_path: Path, args: List[str] = None) -> Optional[str]:
        """Run a benchmark and return its output"""
        if not self.check_benchmark(bench_path):
            return None

        cmd = [str(bench_path)]
        if args:
            cmd.extend(args)

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            return result.stdout + result.stderr
        except subprocess.CalledProcessError as e:
            print(f"{Colors.RED}Error running benchmark: {e}{Colors.NC}")
            return None

    def parse_throughput(self, output: str, operation: str) -> Optional[str]:
        """Parse throughput from benchmark output"""
        pattern = rf"{re.escape(operation)}.*?Throughput:\s+([\d.]+\s+\S+)"
        match = re.search(pattern, output, re.DOTALL)
        return match.group(1) if match else None

    def parse_avg_time(self, output: str, operation: str) -> Optional[str]:
        """Parse average time from benchmark output"""
        pattern = rf"{re.escape(operation)}.*?Avg per iteration:\s+([\d.]+\s+\S+)"
        match = re.search(pattern, output, re.DOTALL)
        return match.group(1) if match else None

    def calculate_percentage(self, current: str, baseline: str) -> str:
        """Calculate percentage relative to baseline"""
        try:
            current_val = float(current.split()[0])
            baseline_val = float(baseline.split()[0])
            percent = int((current_val / baseline_val) * 100)
            return f"{percent}%"
        except (ValueError, ZeroDivisionError, IndexError):
            return "N/A"

    def run_join_benchmarks(self) -> None:
        """Run JOIN performance benchmarks"""
        self.print_header("1. JOIN PERFORMANCE BENCHMARKS")

        bench_path = self.config.BENCHMARK_DIR / "bench_join_performance"
        args = [f"--size={self.config.MESSAGE_COUNT}", f"--iterations={self.config.ITERATIONS}"]

        print(f"{Colors.YELLOW}Running JOIN benchmarks...{Colors.NC}")
        output = self.run_benchmark(bench_path, args)

        if not output:
            return

        self.print_table_header(f"JOIN Operations ({self.config.MESSAGE_COUNT} messages)")

        # Get baseline
        baseline_throughput = self.parse_throughput(output, "SELECT (no JOIN)")
        baseline_time = self.parse_avg_time(output, "SELECT (no JOIN)")

        # Storm ORM operations
        print(f"{Colors.GREEN}Storm ORM Operations:{Colors.NC}")
        storm_operations = [
            "SELECT (no JOIN)",
            "INNER JOIN (single FK)",
            "INNER JOIN (multi FK)",
            "LEFT JOIN (single FK)",
            "LEFT JOIN (multi FK)",
            "RIGHT JOIN (single FK)",
            "RIGHT JOIN (multi FK)"
        ]

        for op in storm_operations:
            if op in output:
                avg_time = self.parse_avg_time(output, op) or "N/A"
                throughput = self.parse_throughput(output, op) or "N/A"

                if op == "SELECT (no JOIN)":
                    vs_baseline = "100% (baseline)"
                elif baseline_throughput and throughput != "N/A":
                    vs_baseline = self.calculate_percentage(throughput, baseline_throughput)
                else:
                    vs_baseline = "N/A"

                self.print_table_row(op, avg_time, throughput, vs_baseline)

        print()
        print(f"{Colors.YELLOW}Raw SQLite Operations (comparison baseline):{Colors.NC}")

        raw_operations = [
            "Raw SQLite SELECT (no JOIN)",
            "Raw SQLite INNER JOIN"
        ]

        for op in raw_operations:
            if op in output:
                avg_time = self.parse_avg_time(output, op) or "N/A"
                throughput = self.parse_throughput(output, op) or "N/A"

                if baseline_throughput and throughput != "N/A":
                    vs_baseline = self.calculate_percentage(throughput, baseline_throughput)
                else:
                    vs_baseline = "-"

                self.print_table_row(op, avg_time, throughput, vs_baseline)

        print()
        print(f"{Colors.GREEN}✓ JOIN benchmarks completed{Colors.NC}")

    def run_sql_generation_benchmarks(self) -> None:
        """Run SQL generation performance benchmarks"""
        self.print_header("2. SQL GENERATION PERFORMANCE")

        bench_path = self.config.BENCHMARK_DIR / "sql_generation_microbench"

        print(f"{Colors.YELLOW}Running SQL generation benchmarks...{Colors.NC}")
        output = self.run_benchmark(bench_path)

        if not output:
            return

        self.print_table_header("SQL Generation & Caching")

        # Parse SQL generation results
        operations = {
            "Compile-time SQL (static)": r"Compile-time.*?(\d+\.\d+)",
            "Runtime SQL (no cache)": r"Runtime.*?no cache.*?(\d+\.\d+)",
            "Runtime SQL (cache hit)": r"Cache hit.*?(\d+\.\d+)",
            "Runtime SQL (cache miss)": r"Cache miss.*?(\d+\.\d+)"
        }

        for op_name, pattern in operations.items():
            match = re.search(pattern, output, re.IGNORECASE)
            if match:
                time_ns = match.group(1)
                vs_baseline = "baseline" if "Compile-time" in op_name else "-"
                self.print_table_row(op_name, f"{time_ns} ns", "N/A", vs_baseline)

        print()
        print(f"{Colors.GREEN}✓ SQL generation benchmarks completed{Colors.NC}")

    def run_join_microbenchmarks(self) -> None:
        """Run detailed JOIN microbenchmarks"""
        self.print_header("3. JOIN DETAILED MICROBENCHMARKS")

        bench_path = self.config.BENCHMARK_DIR / "join_performance_microbench"

        print(f"{Colors.YELLOW}Running detailed JOIN microbenchmarks...{Colors.NC}")
        output = self.run_benchmark(bench_path)

        if not output:
            return

        self.print_table_header("JOIN Implementation Details")

        # Parse detailed results
        operations = [
            "SQL Generation",
            "Column Offset",
            "Field Extraction",
            "Full JOIN"
        ]

        for op in operations:
            pattern = rf"^{re.escape(op)}.*?(\d+\.\d+)\s*(ns|µs|ms)"
            match = re.search(pattern, output, re.MULTILINE)
            if match:
                time_val = match.group(1)
                time_unit = match.group(2)
                self.print_table_row(op, f"{time_val} {time_unit}", "N/A", "-")

        print()
        print(f"{Colors.GREEN}✓ JOIN microbenchmarks completed{Colors.NC}")

    def run_select_comparison_benchmarks(self) -> None:
        """Run SELECT comparison benchmarks"""
        self.print_header("4. SELECT PERFORMANCE COMPARISON")

        bench_path = self.config.BENCHMARK_DIR / "bench_select_comparison"

        print(f"{Colors.YELLOW}Running SELECT comparison benchmarks...{Colors.NC}")
        output = self.run_benchmark(bench_path)

        if not output:
            return

        self.print_table_header("SELECT Implementation Comparison")

        # Parse results
        raw_sqlite_throughput = self.parse_throughput(output, "Raw SQLite")
        raw_sqlite_time = self.parse_avg_time(output, "Raw SQLite")

        if raw_sqlite_time and raw_sqlite_throughput:
            self.print_table_row(
                "Raw SQLite SELECT",
                raw_sqlite_time,
                raw_sqlite_throughput,
                "100% (baseline)"
            )

        storm_throughput = self.parse_throughput(output, "Storm ORM")
        storm_time = self.parse_avg_time(output, "Storm ORM")

        if storm_time and storm_throughput:
            vs_baseline = self.calculate_percentage(storm_throughput, raw_sqlite_throughput) if raw_sqlite_throughput else "-"
            self.print_table_row(
                "Storm ORM SELECT",
                storm_time,
                storm_throughput,
                vs_baseline
            )

        print()
        print(f"{Colors.GREEN}✓ SELECT comparison benchmarks completed{Colors.NC}")

    def print_summary(self) -> None:
        """Print benchmark summary"""
        self.print_header("BENCHMARK SUMMARY")

        print(f"{Colors.BOLD}Key Performance Metrics:{Colors.NC}")
        print()

        # JOIN Performance Summary
        bench_path = self.config.BENCHMARK_DIR / "bench_join_performance"
        if bench_path.exists():
            args = [f"--size={self.config.MESSAGE_COUNT}", f"--iterations={self.config.ITERATIONS}"]
            output = self.run_benchmark(bench_path, args)

            if output:
                baseline = self.parse_throughput(output, "SELECT (no JOIN)")
                inner_single = self.parse_throughput(output, "INNER JOIN (single FK)")
                inner_multi = self.parse_throughput(output, "INNER JOIN (multi FK)")

                if baseline:
                    print(f"  {Colors.CYAN}JOIN Performance:{Colors.NC}")
                    print(f"    • Baseline SELECT:      {Colors.GREEN}{baseline} rows/sec{Colors.NC}")
                    if inner_single:
                        print(f"    • INNER JOIN (1 FK):    {Colors.GREEN}{inner_single} rows/sec{Colors.NC}")
                    if inner_multi:
                        print(f"    • INNER JOIN (2 FK):    {Colors.GREEN}{inner_multi} rows/sec{Colors.NC}")
                    print()

        # SQL Generation Summary
        bench_path = self.config.BENCHMARK_DIR / "sql_generation_microbench"
        if bench_path.exists():
            output = self.run_benchmark(bench_path)

            if output:
                compile_match = re.search(r"Compile-time.*?(\d+\.\d+)", output, re.IGNORECASE)
                cache_match = re.search(r"Cache hit.*?(\d+\.\d+)", output, re.IGNORECASE)

                if compile_match and cache_match:
                    compile_time = float(compile_match.group(1))
                    cache_time = float(cache_match.group(1))

                    if cache_time > 0:
                        speedup = compile_time / cache_time
                        print(f"  {Colors.CYAN}SQL Generation:{Colors.NC}")
                        print(f"    • Compile-time:         {Colors.GREEN}{compile_time:.2f} ns{Colors.NC}")
                        print(f"    • Cache hit:            {Colors.GREEN}{cache_time:.2f} ns{Colors.NC}")
                        print(f"    • Cache speedup:        {Colors.GREEN}{speedup:.1f}x{Colors.NC}")
                        print()

        print(f"{Colors.BOLD}{Colors.GREEN}✓ All microbenchmarks completed successfully!{Colors.NC}")
        print()

    def run_all(self) -> int:
        """Run all benchmarks"""
        print(f"{Colors.YELLOW}Checking if benchmarks are built...{Colors.NC}")

        if not self.build_benchmarks():
            return 1

        self.print_header("STORM ORM MICROBENCHMARK SUITE")
        print("Configuration:")
        print(f"  Build: {Colors.GREEN}Release{Colors.NC}")
        print(f"  Iterations: {Colors.GREEN}{self.config.ITERATIONS}{Colors.NC}")
        print(f"  Dataset size: {Colors.GREEN}{self.config.MESSAGE_COUNT} messages{Colors.NC}")
        print()

        # Run benchmark suites
        self.run_join_benchmarks()
        self.run_sql_generation_benchmarks()
        self.run_join_microbenchmarks()
        self.run_select_comparison_benchmarks()

        # Print summary
        self.print_summary()

        return 0


def main() -> int:
    """Main entry point"""
    runner = BenchmarkRunner()
    return runner.run_all()


if __name__ == "__main__":
    sys.exit(main())
