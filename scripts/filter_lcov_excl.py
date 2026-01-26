#!/usr/bin/env python3
"""
Filter LCOV_EXCL_START/STOP marked lines from lcov coverage files.

This script processes lcov-format coverage files and removes coverage data
for lines that fall within LCOV_EXCL_START/STOP blocks in the source code.

This is needed because llvm-cov export doesn't process these markers
(they are normally processed by geninfo during gcov capture).

Usage:
    ./filter_lcov_excl.py input.lcov output.lcov
    ./filter_lcov_excl.py input.lcov  # prints to stdout
"""

import sys
import re
from pathlib import Path
from typing import TextIO


def find_excluded_lines(source_path: str) -> set[int]:
    """
    Parse a source file and return set of line numbers inside LCOV_EXCL blocks.

    Supports:
    - LCOV_EXCL_START / LCOV_EXCL_STOP - exclude block of lines
    - LCOV_EXCL_LINE - exclude single line
    - LCOV_EXCL_BR_START / LCOV_EXCL_BR_STOP - branch exclusion (treated same as line)
    """
    excluded = set()

    try:
        with open(source_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except (FileNotFoundError, PermissionError) as e:
        print(f"Warning: Cannot read {source_path}: {e}", file=sys.stderr)
        return excluded

    in_excl_block = False

    for line_num, line in enumerate(lines, start=1):
        # Check for block markers
        if 'LCOV_EXCL_START' in line or 'LCOV_EXCL_BR_START' in line:
            in_excl_block = True
            excluded.add(line_num)  # The marker line itself is excluded
        elif 'LCOV_EXCL_STOP' in line or 'LCOV_EXCL_BR_STOP' in line:
            in_excl_block = False
            # Don't exclude the STOP line itself
        elif in_excl_block:
            excluded.add(line_num)
        elif 'LCOV_EXCL_LINE' in line or 'LCOV_EXCL_BR_LINE' in line:
            excluded.add(line_num)

    return excluded


def filter_lcov(input_file: TextIO, output_file: TextIO) -> dict:
    """
    Filter lcov file, removing DA/BRDA entries for excluded lines.

    Returns statistics dict.
    """
    stats = {
        'files_processed': 0,
        'lines_removed': 0,
        'branches_removed': 0,
        'functions_removed': 0,
    }

    current_source = None
    excluded_lines: set[int] = set()

    # Patterns for coverage data lines
    da_pattern = re.compile(r'^DA:(\d+),')
    brda_pattern = re.compile(r'^BRDA:(\d+),')
    fnl_pattern = re.compile(r'^FNL:(\d+),')

    for line in input_file:
        line = line.rstrip('\n\r')

        # Track current source file
        if line.startswith('SF:'):
            current_source = line[3:]
            excluded_lines = find_excluded_lines(current_source)
            stats['files_processed'] += 1
            output_file.write(line + '\n')
            continue

        # Filter DA (line coverage) entries
        da_match = da_pattern.match(line)
        if da_match:
            line_num = int(da_match.group(1))
            if line_num in excluded_lines:
                stats['lines_removed'] += 1
                continue
            output_file.write(line + '\n')
            continue

        # Filter BRDA (branch coverage) entries
        brda_match = brda_pattern.match(line)
        if brda_match:
            line_num = int(brda_match.group(1))
            if line_num in excluded_lines:
                stats['branches_removed'] += 1
                continue
            output_file.write(line + '\n')
            continue

        # Filter FNL (function line) entries - function defined on excluded line
        fnl_match = fnl_pattern.match(line)
        if fnl_match:
            line_num = int(fnl_match.group(1))
            if line_num in excluded_lines:
                stats['functions_removed'] += 1
                continue
            output_file.write(line + '\n')
            continue

        # Pass through all other lines (SF, FN, FNF, FNH, LF, LH, BRF, BRH, end_of_record, etc.)
        output_file.write(line + '\n')

    return stats


def recalculate_totals(lcov_content: str) -> str:
    """
    Recalculate LF/LH (lines found/hit) and BRF/BRH (branches found/hit) totals.
    """
    lines = lcov_content.split('\n')
    result = []

    # Track counts for current file
    lines_found = 0
    lines_hit = 0
    branches_found = 0
    branches_hit = 0

    da_pattern = re.compile(r'^DA:(\d+),(\d+)')
    brda_pattern = re.compile(r'^BRDA:\d+,\d+,\d+,([^-].*|-)$')

    for line in lines:
        # Reset counters at start of new file
        if line.startswith('SF:'):
            lines_found = 0
            lines_hit = 0
            branches_found = 0
            branches_hit = 0
            result.append(line)
            continue

        # Count DA entries
        da_match = da_pattern.match(line)
        if da_match:
            lines_found += 1
            if int(da_match.group(2)) > 0:
                lines_hit += 1
            result.append(line)
            continue

        # Count BRDA entries
        brda_match = brda_pattern.match(line)
        if brda_match:
            branches_found += 1
            hit_count = brda_match.group(1)
            if hit_count != '-' and int(hit_count) > 0:
                branches_hit += 1
            result.append(line)
            continue

        # Replace LF/LH with recalculated values
        if line.startswith('LF:'):
            result.append(f'LF:{lines_found}')
            continue
        if line.startswith('LH:'):
            result.append(f'LH:{lines_hit}')
            continue

        # Replace BRF/BRH with recalculated values
        if line.startswith('BRF:'):
            result.append(f'BRF:{branches_found}')
            continue
        if line.startswith('BRH:'):
            result.append(f'BRH:{branches_hit}')
            continue

        result.append(line)

    return '\n'.join(result)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    # Read input
    if input_path == '-':
        input_content = sys.stdin.read()
    else:
        with open(input_path, 'r') as f:
            input_content = f.read()

    # Filter
    from io import StringIO
    input_io = StringIO(input_content)
    output_io = StringIO()

    stats = filter_lcov(input_io, output_io)

    # Recalculate totals
    filtered_content = recalculate_totals(output_io.getvalue())

    # Write output
    if output_path:
        with open(output_path, 'w') as f:
            f.write(filtered_content)
        print(f"Filtered {input_path} -> {output_path}", file=sys.stderr)
    else:
        sys.stdout.write(filtered_content)

    # Print stats
    print(f"Statistics:", file=sys.stderr)
    print(f"  Files processed: {stats['files_processed']}", file=sys.stderr)
    print(f"  Lines removed:   {stats['lines_removed']}", file=sys.stderr)
    print(f"  Branches removed: {stats['branches_removed']}", file=sys.stderr)
    print(f"  Functions removed: {stats['functions_removed']}", file=sys.stderr)


if __name__ == '__main__':
    main()
