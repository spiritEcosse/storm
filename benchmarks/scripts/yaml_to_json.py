#!/usr/bin/env python3
"""
Convert benchmark_tests.yaml to benchmark_tests.json

This script converts the human-friendly YAML benchmark definitions
to JSON format for compile-time parsing with C++26 #embed.

Usage:
    python yaml_to_json.py [input.yaml] [output.json]

If no arguments provided, uses default paths:
    Input:  benchmarks/tests/benchmark_tests.yaml
    Output: benchmarks/tests/benchmark_tests.json

Note: Uses built-in simple YAML parser (no external dependencies required).
      Supports the subset of YAML used by benchmark definitions.
"""

import json
import sys
from pathlib import Path


def parse_yaml_value(value: str):
    """Parse a YAML value string into Python type."""
    value = value.strip()

    if not value:
        return None

    # Handle quoted strings
    if (value.startswith('"') and value.endswith('"')) or \
       (value.startswith("'") and value.endswith("'")):
        return value[1:-1]

    # Handle booleans
    if value.lower() == 'true':
        return True
    if value.lower() == 'false':
        return False

    # Handle null
    if value.lower() in ('null', '~'):
        return None

    # Handle numbers
    try:
        if '.' in value:
            return float(value)
        return int(value)
    except ValueError:
        pass

    # Handle inline arrays [1, 2, 3]
    if value.startswith('[') and value.endswith(']'):
        inner = value[1:-1].strip()
        if not inner:
            return []
        items = [parse_yaml_value(item.strip()) for item in inner.split(',')]
        return items

    # Return as string
    return value


class _YamlParserState:
    """Mutable state for the simple YAML parser."""

    def __init__(self):
        self.result = {}
        self.in_list = False
        self.list_key = None
        self.list_items = []
        self.current_item = None
        self.list_item_indent = 0

    def finalize_list(self):
        """Save the current list into result and reset list state."""
        if self.current_item is not None:
            self.list_items.append(self.current_item)
        self.result[self.list_key] = self.list_items
        self.in_list = False
        self.list_key = None
        self.list_items = []
        self.current_item = None


def _parse_list_item_start(state: _YamlParserState, stripped: str, indent: int):
    """Handle a '- ' prefixed list item line."""
    if not state.in_list:
        return

    # Save previous item if exists
    if state.current_item is not None:
        state.list_items.append(state.current_item)

    rest = stripped[2:].strip()
    state.list_item_indent = indent

    if ':' in rest:
        key, _, value = rest.partition(':')
        state.current_item = {}
        value = value.strip()
        if value:
            state.current_item[key.strip()] = parse_yaml_value(value)
    else:
        state.current_item = parse_yaml_value(rest)


def _parse_nested_list_kv(state: _YamlParserState, stripped: str):
    """Handle a nested key-value pair inside a list item."""
    if not isinstance(state.current_item, dict):
        return
    key, _, value = stripped.partition(':')
    value = value.strip()
    if value:
        state.current_item[key.strip()] = parse_yaml_value(value)


def _parse_top_level_kv(state: _YamlParserState, stripped: str):
    """Handle a top-level key-value pair (outside any list)."""
    key, _, value = stripped.partition(':')
    key = key.strip()
    value = value.strip()

    if value:
        state.result[key] = parse_yaml_value(value)
    else:
        # Assume it starts a list
        state.in_list = True
        state.list_key = key
        state.list_items = []
        state.current_item = None


def parse_simple_yaml(yaml_content: str) -> dict:
    """
    Parse a simple subset of YAML used by benchmark definitions.

    Supports:
    - Comments (#)
    - Key-value pairs
    - Lists (with - prefix)
    - Nested objects (via indentation)
    - Inline arrays [1, 2, 3]
    - Strings, integers, floats, booleans
    """
    state = _YamlParserState()

    for line in yaml_content.split('\n'):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue

        indent = len(line) - len(line.lstrip())

        # Check if we're ending a list (dedent)
        if state.in_list and indent == 0 and not stripped.startswith('-'):
            state.finalize_list()

        # Dispatch to appropriate handler
        if stripped.startswith('- '):
            _parse_list_item_start(state, stripped, indent)
        elif state.in_list and indent > state.list_item_indent and ':' in stripped:
            _parse_nested_list_kv(state, stripped)
        elif ':' in stripped and not state.in_list:
            _parse_top_level_kv(state, stripped)

    # Don't forget the last item/list
    if state.in_list:
        state.finalize_list()

    return state.result


def convert_yaml_to_json(yaml_path: Path, json_path: Path) -> None:
    """Convert YAML benchmark definitions to JSON format."""

    # Read YAML
    with open(yaml_path, 'r') as f:
        yaml_content = f.read()

    data = parse_simple_yaml(yaml_content)

    if 'tests' not in data:
        print(f"Error: YAML file must have a 'tests' key", file=sys.stderr)
        print(f"Parsed keys: {list(data.keys())}", file=sys.stderr)
        sys.exit(1)

    tests = data['tests']

    if tests is None:
        print(f"Error: 'tests' is None - parsing failed", file=sys.stderr)
        sys.exit(1)

    # Transform YAML format to JSON format expected by C++ parser
    json_tests = []
    for test in tests:
        if not isinstance(test, dict):
            continue

        json_test = {}

        # Map YAML keys to JSON keys (with test_ prefix where needed)
        key_mapping = {
            'name': 'test_name',
            'category': 'test_category',
            'description': 'description',
            'model': 'model',
            'operation': 'operation',
            'size_profile': 'size_profile',
            'iterations': 'iterations',
            'dataset_size': 'dataset_size',
            'batch_size': 'batch_size',
            'where_field': 'where_field',
            'where_op': 'where_op',
            'where_value_int': 'where_value_int',
            'where_value_int2': 'where_value_int2',
            'where_value_double': 'where_value_double',
            'where_value_bool': 'where_value_bool',
            'where_value_string': 'where_value_string',
            'where_field2': 'where_field2',
            'where_op2': 'where_op2',
            'where_value2_int': 'where_value2_int',
            'where_in_values': 'where_in_values',
            'distinct_field': 'distinct_field',
            'distinct_field2': 'distinct_field2',
            'distinct_field3': 'distinct_field3',
            'aggregate_field': 'aggregate_field',
            'limit_value': 'limit_value',
            'offset_value': 'offset_value',
            'order_by_field': 'order_by_field',
            'order_by_direction': 'order_by_direction',
            'order_by_field2': 'order_by_field2',
            'order_by_direction2': 'order_by_direction2',
            'group_by_field': 'group_by_field',
            'group_by_field2': 'group_by_field2',
        }

        for yaml_key, json_key in key_mapping.items():
            if yaml_key in test and test[yaml_key] is not None:
                json_test[json_key] = test[yaml_key]

        if json_test:  # Only add non-empty tests
            json_tests.append(json_test)

    # Write JSON with nice formatting
    with open(json_path, 'w') as f:
        json.dump(json_tests, f, indent=2)
        f.write('\n')  # Trailing newline

    print(f"Converted {len(json_tests)} tests: {yaml_path} -> {json_path}")


def main():
    # Determine paths
    script_dir = Path(__file__).parent
    benchmarks_dir = script_dir.parent

    if len(sys.argv) >= 3:
        yaml_path = Path(sys.argv[1])
        json_path = Path(sys.argv[2])
    elif len(sys.argv) == 2:
        yaml_path = Path(sys.argv[1])
        json_path = yaml_path.with_suffix('.json')
    else:
        yaml_path = benchmarks_dir / 'tests' / 'benchmark_tests.yaml'
        json_path = benchmarks_dir / 'tests' / 'benchmark_tests.json'

    if not yaml_path.exists():
        print(f"Error: YAML file not found: {yaml_path}", file=sys.stderr)
        sys.exit(1)

    convert_yaml_to_json(yaml_path, json_path)


if __name__ == '__main__':
    main()
