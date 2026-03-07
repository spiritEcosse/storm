#!/usr/bin/env python3
"""Convert a YAML test-case file to JSON for #embed in C++26 consteval parsers.

Usage:
    python3 yaml_to_json_tests.py input.yaml [output.json]

If output.json is omitted the JSON is written to stdout.
Requires PyYAML.
"""

import json
import sys
import pathlib

import yaml  # PyYAML required


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: yaml_to_json_tests.py input.yaml [output.json]", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    with open(input_path, encoding="utf-8") as fh:
        data = yaml.safe_load(fh) or []

    json_text = json.dumps(data, separators=(",", ":"))

    if output_path:
        pathlib.Path(output_path).write_text(json_text + "\n", encoding="utf-8")
    else:
        print(json_text)


if __name__ == "__main__":
    main()
