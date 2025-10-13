#!/usr/bin/env python3
"""
Common utilities for Storm ORM benchmarks
Provides base classes and formatting utilities for benchmark output
"""

import re
import subprocess
import sys
from pathlib import Path
from abc import ABC, abstractmethod
from typing import List, Optional


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
    NC = '\033[0m'  # No Color (alias for RESET)


def get_visible_length(text: str) -> int:
    """Get visible length of string without ANSI escape codes"""
    ansi_escape = re.compile(r'\x1b\[[0-9;]*m')
    clean_text = ansi_escape.sub('', text)
    return len(clean_text)


def pad_string(text: str, width: int) -> str:
    """Pad string to specific width, accounting for ANSI escape codes"""
    visible_length = get_visible_length(text)
    padding_needed = width - visible_length

    if padding_needed > 0:
        return text + (' ' * padding_needed)
    return text


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
    def print_header(title, include_db_hits=False):
        """Print table header"""
        if include_db_hits:
            print(f"{Colors.BOLD}┌─────────────────────────────────┬──────────┬──────────────┬──────────────┬──────────────┐{Colors.RESET}")
            print(f"{Colors.BOLD}│ {title:31} │ DB Hits  │ Storm ORM    │ Raw SQLite   │ Efficiency   │{Colors.RESET}")
            print(f"{Colors.BOLD}├─────────────────────────────────┼──────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")
        else:
            print(f"{Colors.BOLD}┌─────────────────────────────────┬──────────────┬──────────────┬──────────────┐{Colors.RESET}")
            print(f"{Colors.BOLD}│ {title:31} │ Storm ORM    │ Raw SQLite   │ Efficiency   │{Colors.RESET}")
            print(f"{Colors.BOLD}├─────────────────────────────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")

    @staticmethod
    def print_separator(include_db_hits=False):
        """Print table separator"""
        if include_db_hits:
            print(f"{Colors.BOLD}├─────────────────────────────────┼──────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")
        else:
            print(f"{Colors.BOLD}├─────────────────────────────────┼──────────────┼──────────────┼──────────────┤{Colors.RESET}")

    @staticmethod
    def print_footer(include_db_hits=False):
        """Print table footer"""
        if include_db_hits:
            print(f"{Colors.BOLD}└─────────────────────────────────┴──────────┴──────────────┴──────────────┴──────────────┘{Colors.RESET}")
        else:
            print(f"{Colors.BOLD}└─────────────────────────────────┴──────────────┴──────────────┴──────────────┘{Colors.RESET}")

    @staticmethod
    def print_row(label, storm_value, raw_value, efficiency, db_hits=None):
        """
        Print a single table row

        Args:
            label: Operation name (str)
            storm_value: Storm ORM throughput (int, rows/sec)
            raw_value: Raw SQLite throughput (int, rows/sec)
            efficiency: Efficiency percentage (float)
            db_hits: Optional number of database queries executed (int)
        """
        storm_str = f"{storm_value / 1_000_000:>11.2f}M"
        raw_str = f"{raw_value / 1_000_000:>11.2f}M"
        eff_str = f"{efficiency:>11.1f}%"

        if db_hits is not None:
            db_hits_str = f"{db_hits:>8}"
            print(f"│ {label:31} │ {Colors.GREEN}{db_hits_str}{Colors.RESET} │ {Colors.CYAN}{storm_str}{Colors.RESET} │ "
                  f"{Colors.DIM}{raw_str}{Colors.RESET} │ {BenchmarkTable.color_efficiency(eff_str)} │")
        else:
            print(f"│ {label:31} │ {Colors.CYAN}{storm_str}{Colors.RESET} │ "
                  f"{Colors.DIM}{raw_str}{Colors.RESET} │ {BenchmarkTable.color_efficiency(eff_str)} │")

    @staticmethod
    def print_summary(messages, iterations, avg_efficiency):
        """Print benchmark summary"""
        print(f"\n{Colors.DIM}Test configuration: {messages:,} messages, {iterations} iterations{Colors.RESET}")
        avg_eff_str = f"{avg_efficiency:>11.1f}%"
        print(f"\n{Colors.BOLD}Average Efficiency:{Colors.RESET} {BenchmarkTable.color_efficiency(avg_eff_str)}")


class FlexibleTable:
    """Flexible table class for rendering tables with variable columns"""

    def __init__(self, headers: List[str], column_widths: List[int]):
        """
        Initialize flexible table

        Args:
            headers: List of column header strings
            column_widths: List of column widths (in characters)
        """
        self.headers = headers
        self.column_widths = column_widths

        if len(headers) != len(column_widths):
            raise ValueError("Number of headers must match number of column widths")

    def _build_separator(self, left='├', mid='┼', right='┤') -> str:
        """Build a separator line"""
        parts = [left]
        for i, width in enumerate(self.column_widths):
            parts.append('─' * (width + 2))
            if i < len(self.column_widths) - 1:
                parts.append(mid)
        parts.append(right)
        return ''.join(parts)

    def print_header(self):
        """Print table header"""
        # Top border
        print(self._build_separator('┌', '┬', '┐'))

        # Header row
        header_cells = []
        for i, (header, width) in enumerate(zip(self.headers, self.column_widths)):
            padded = pad_string(header, width)
            header_cells.append(f" {padded} ")

        print(f"│{'│'.join(header_cells)}│")

        # Header separator
        print(self._build_separator('├', '┼', '┤'))

    def print_row(self, cells: List[str]):
        """
        Print a table row

        Args:
            cells: List of cell contents (may contain ANSI color codes)
        """
        if len(cells) != len(self.column_widths):
            raise ValueError(f"Expected {len(self.column_widths)} cells, got {len(cells)}")

        row_cells = []
        for cell, width in zip(cells, self.column_widths):
            padded = pad_string(cell, width)
            row_cells.append(f" {padded} ")

        print(f"│{'│'.join(row_cells)}│")

    def print_separator(self):
        """Print a separator between rows"""
        print(self._build_separator('├', '┼', '┤'))

    def print_footer(self):
        """Print table footer"""
        print(self._build_separator('└', '┴', '┘'))


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
