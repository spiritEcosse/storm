#!/usr/bin/env python3
"""
Common utilities for Storm ORM benchmarks
Provides base classes and formatting utilities for benchmark output
"""

import subprocess
import sys
from pathlib import Path
from abc import ABC, abstractmethod


class Colors:
    """ANSI color codes for terminal output"""
    BOLD = '\033[1m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    DIM = '\033[2m'


class BenchmarkTable:
    """Base class for formatted benchmark comparison tables"""

    @staticmethod
    def color_efficiency(eff_str):
        """Color code efficiency string: green >=70%, yellow 50-70%, red <50%"""
        # Extract numeric value from formatted string
        eff = float(eff_str.rstrip('%'))
        if eff >= 70:
            return f"{Colors.GREEN}{eff_str}{Colors.RESET}"
        elif eff >= 50:
            return f"{Colors.YELLOW}{eff_str}{Colors.RESET}"
        else:
            return f"{Colors.RED}{eff_str}{Colors.RESET}"

    @staticmethod
    def print_header(title):
        """Print table header"""
        print(f"{Colors.BOLD}┌─────────────────────────────────┬──────────────┬──────────────┬──────────────┐{Colors.RESET}")
        print(f"{Colors.BOLD}│ {title:31} │ Storm ORM    │ Raw SQLite   │ Efficiency   │{Colors.RESET}")
        print(f"{Colors.BOLD}├─────────────────────────────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")

    @staticmethod
    def print_separator():
        """Print table separator"""
        print(f"{Colors.BOLD}├─────────────────────────────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")

    @staticmethod
    def print_footer():
        """Print table footer"""
        print(f"{Colors.BOLD}└─────────────────────────────────┴──────────────┴──────────────┴──────────────┘{Colors.RESET}")

    @staticmethod
    def print_row(label, storm_value, raw_value, efficiency):
        """
        Print a single table row

        Args:
            label: Operation name (str)
            storm_value: Storm ORM throughput (int, rows/sec)
            raw_value: Raw SQLite throughput (int, rows/sec)
            efficiency: Efficiency percentage (float)
        """
        storm_str = f"{storm_value / 1_000_000:>11.2f}M"
        raw_str = f"{raw_value / 1_000_000:>11.2f}M"
        eff_str = f"{efficiency:>11.1f}%"

        print(f"│ {label:31} │ {Colors.CYAN}{storm_str}{Colors.RESET} │ "
              f"{Colors.DIM}{raw_str}{Colors.RESET} │ {BenchmarkTable.color_efficiency(eff_str)} │")

    @staticmethod
    def print_summary(messages, iterations, avg_efficiency):
        """Print benchmark summary"""
        print(f"\n{Colors.DIM}Test configuration: {messages:,} messages, {iterations} iterations{Colors.RESET}")
        avg_eff_str = f"{avg_efficiency:>11.1f}%"
        print(f"\n{Colors.BOLD}Average Efficiency:{Colors.RESET} {BenchmarkTable.color_efficiency(avg_eff_str)}")


class BenchmarkRunner(ABC):
    """Abstract base class for benchmark runners"""

    def __init__(self, binary_path):
        """
        Initialize benchmark runner

        Args:
            binary_path: Path to benchmark binary
        """
        self.binary_path = Path(binary_path)

    def check_binary_exists(self):
        """Verify benchmark binary exists"""
        if not self.binary_path.exists():
            print(f"{Colors.RED}Error: Benchmark binary not found at {self.binary_path}{Colors.RESET}")
            print(f"{Colors.YELLOW}Hint: Build with 'cmake --build --preset ninja-release'{Colors.RESET}")
            sys.exit(1)

    def run_benchmark(self, *args):
        """
        Run the benchmark binary

        Args:
            *args: Command-line arguments to pass to binary

        Returns:
            str: Benchmark output (stdout)
        """
        print(f"{Colors.CYAN}{Colors.BOLD}Running benchmark...{Colors.RESET}")

        result = subprocess.run(
            [str(self.binary_path)] + list(args),
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print(f"{Colors.RED}Error running benchmark:{Colors.RESET}")
            print(result.stderr)
            sys.exit(1)

        return result.stdout

    @abstractmethod
    def parse_results(self, output):
        """
        Parse benchmark output

        Args:
            output: Benchmark stdout

        Returns:
            dict: Parsed benchmark data
        """
        pass

    @abstractmethod
    def display_results(self, data, **kwargs):
        """
        Display formatted benchmark results

        Args:
            data: Parsed benchmark data
            **kwargs: Additional display parameters
        """
        pass

    def run(self, *args, **kwargs):
        """
        Complete benchmark workflow: check, run, parse, display

        Args:
            *args: Arguments for benchmark binary
            **kwargs: Additional parameters for display
        """
        self.check_binary_exists()
        output = self.run_benchmark(*args)
        data = self.parse_results(output)
        self.display_results(data, **kwargs)
