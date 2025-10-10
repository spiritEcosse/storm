#!/usr/bin/env python3
"""
Quick test to verify table rendering works correctly
"""

import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import FlexibleTable, BenchmarkTable, Colors, pad_string, get_visible_length

def test_flexible_table():
    """Test FlexibleTable with color-coded cells"""
    print("=== Testing FlexibleTable (9-column bench_compare style) ===\n")

    table = FlexibleTable(
        headers=[
            'Benchmark',
            'Overall Perf %',
            'Single INSERT',
            'Best Batch INSERT',
            'Single DELETE',
            'Bulk DELETE',
            'Single UPDATE',
            'Best Batch UPDATE',
            'SELECT'
        ],
        column_widths=[35, 16, 18, 19, 18, 19, 18, 19, 19]
    )

    table.print_header()

    # Test row 1 - with colors
    table.print_row([
        'Raw SQLite (prepared)',
        f'{Colors.BLUE}450%{Colors.NC}',
        f'{Colors.GREEN}49.0M inserts/sec{Colors.NC}',
        'Not available',
        f'{Colors.GREEN}29.4M deletes/sec{Colors.NC}',
        'Not available',
        f'{Colors.BLUE}1.1M updates/sec{Colors.NC}',
        'Not available',
        f'{Colors.GREEN}17.7M rows/sec{Colors.NC}'
    ])

    # Test row 2 - baseline
    table.print_row([
        'sqlite_orm (v1.9.1)',
        f'{Colors.GREEN}100% (baseline){Colors.NC}',
        f'{Colors.YELLOW}492.3K inserts/sec{Colors.NC}',
        f'{Colors.YELLOW}422.5K inserts/sec{Colors.NC}',
        f'{Colors.YELLOW}589.1K deletes/sec{Colors.NC}',
        f'{Colors.YELLOW}589.1K deletes/sec{Colors.NC}',
        f'{Colors.RED}333.3K updates/sec{Colors.NC}',
        f'{Colors.RED}333.3K updates/sec{Colors.NC}',
        f'{Colors.BLUE}8.7M rows/sec{Colors.NC}'
    ])

    # Test row 3 - Storm ORM
    table.print_row([
        'Storm ORM (Standard)',
        f'{Colors.BLUE}287%{Colors.NC}',
        f'{Colors.BLUE}992.1K inserts/sec{Colors.NC}',
        f'{Colors.BLUE}2.7M inserts/sec{Colors.NC}',
        f'{Colors.GREEN}21.6M deletes/sec{Colors.NC}',
        f'{Colors.BLUE}3.9M deletes/sec{Colors.NC}',
        f'{Colors.BLUE}2.0M updates/sec{Colors.NC}',
        f'{Colors.BLUE}2.0M updates/sec{Colors.NC}',
        f'{Colors.GREEN}13.1M rows/sec{Colors.NC}'
    ])

    table.print_footer()
    print()


def test_benchmark_table():
    """Test BenchmarkTable (4-column JOIN style)"""
    print("=== Testing BenchmarkTable (4-column JOIN style) ===\n")

    BenchmarkTable.print_header("JOIN Operation")

    # Test rows
    BenchmarkTable.print_row('INNER JOIN (single FK)', 6_900_000, 9_900_000, 69.7)
    BenchmarkTable.print_row('INNER JOIN (multi FK)', 6_000_000, 9_800_000, 61.2)

    BenchmarkTable.print_separator()

    BenchmarkTable.print_row('LEFT JOIN (single FK)', 5_500_000, 9_500_000, 57.9)
    BenchmarkTable.print_row('LEFT JOIN (multi FK)', 5_000_000, 9_400_000, 53.2)

    BenchmarkTable.print_footer()

    BenchmarkTable.print_summary(10000, 100, 60.5)
    print()


def test_ansi_utilities():
    """Test ANSI-aware string utilities"""
    print("=== Testing ANSI-aware utilities ===\n")

    # Test with colored text
    colored_text = f"{Colors.GREEN}Success!{Colors.NC}"
    print(f"Colored text: {colored_text}")
    print(f"Visible length: {get_visible_length(colored_text)} (should be 8)")
    print(f"Actual length: {len(colored_text)} (with ANSI codes)")

    # Test padding
    padded = pad_string(colored_text, 20)
    print(f"Padded to 20: |{padded}|")
    print(f"Visual width: {get_visible_length(padded)} chars")
    print()


if __name__ == '__main__':
    test_flexible_table()
    test_benchmark_table()
    test_ansi_utilities()

    print(f"{Colors.GREEN}✓ All table rendering tests completed successfully!{Colors.NC}")
