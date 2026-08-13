#!/usr/bin/env python3
"""Split the unified YAML test corpus into one JSON file per category.

Usage:
    python3 split_unified_cases.py input.yaml output_dir

Writes <output_dir>/unified_cases_<category>.json for each category, plus the
combined <output_dir>/unified_cases.json (still consumed by any includer that
does not define STORM_UNIFIED_CASES_FILE).

Issue #561: the 247-case corpus compiled as a single TU took 53 s -- 89% of it
the per-case template instantiation, linear at ~190 ms/case. Splitting the
corpus across TUs lets those instantiations compile in parallel.

The categories mirror the four dispatch families in UnifiedYamlBody::TestBody
exactly (see tests/test_unified_yaml_body.h), so each TU instantiates only the
runners its own cases need, and a failing suite name points at a coherent area.
A query_type absent from the tables below is a corpus/dispatch mismatch and is
a hard error -- silently bucketing it would hide a case that no runner handles.

CATEGORIES below is duplicated by the foreach() in tests/CMakeLists.txt and by
the set of tests/yaml/test_unified_yaml_*.cpp TUs. Changing one means changing
all three: adding a category here alone writes a JSON nothing embeds, which
drops those cases from the suite with a green build.
"""

from __future__ import annotations

import json
import pathlib
import sys

import yaml  # PyYAML required

# query_type -> category, mirroring UnifiedYamlTest::TestBody's dispatch:
#   is_write_op()          -> dispatch_write_family()
#   select/first/one       -> dispatch_select()
#   is_grouped_op()        -> dispatch_grouped_family()
#   everything else        -> dispatch_aggregate()
WRITE_OPS = {"insert_one", "insert_batch", "update_batch", "erase_all", "erase_batch"}
SELECT_OPS = {"select", "first", "one"}
GROUPED_OPS = {
    "chain",
    "distinct",
    "group_count",
    "group_sum",
    "group_avg",
    "group_min",
    "group_max",
}
AGGREGATE_OPS = {"count", "count_field", "count_distinct", "sum", "avg", "min", "max"}

CATEGORIES = ("select", "grouped", "aggregate", "crud")


def categorize(case: dict, index: int) -> str:
    """Map one case to its category, mirroring the C++ dispatch families."""
    if not isinstance(case, dict):
        raise SystemExit(
            f"Error: corpus entry {index} is {type(case).__name__}, not a mapping -- "
            f"most likely a YAML indentation slip."
        )
    qt = case.get("query_type")
    if qt in WRITE_OPS:
        return "crud"
    if qt in SELECT_OPS:
        return "select"
    if qt in GROUPED_OPS:
        return "grouped"
    if qt in AGGREGATE_OPS:
        return "aggregate"
    raise SystemExit(
        f"Error: case {case.get('name', '<unnamed>')!r} has unknown query_type "
        f"{qt!r}. Add it to the tables in {pathlib.Path(__file__).name} and to "
        f"the matching dispatch family in tests/test_unified_yaml_body.h."
    )


def main() -> None:
    if len(sys.argv) < 3:
        print("Usage: split_unified_cases.py input.yaml output_dir", file=sys.stderr)
        sys.exit(1)

    # Resolve both paths up front and require them to already exist: this script
    # only ever rewrites files CMake generates, so a mistyped argument should fail
    # here rather than read or write somewhere unexpected.
    input_path = pathlib.Path(sys.argv[1]).resolve()
    output_dir = pathlib.Path(sys.argv[2]).resolve()

    if not input_path.is_file():
        raise SystemExit(f"Error: corpus {input_path} is not an existing file")
    if not output_dir.is_dir():
        raise SystemExit(f"Error: output directory {output_dir} does not exist")

    with input_path.open(encoding="utf-8") as fh:
        cases = yaml.safe_load(fh) or []

    if not isinstance(cases, list):
        print("Error: expected a top-level list in the YAML corpus", file=sys.stderr)
        sys.exit(1)

    buckets: dict[str, list] = {name: [] for name in CATEGORIES}
    for index, case in enumerate(cases):
        buckets[categorize(case, index)].append(case)

    # A truncated, empty or all-comments YAML otherwise yields four empty JSONs,
    # four suites registering zero tests, and a green ctest that verifies nothing.
    # Every category maps to a dispatch family that has cases today, so an empty
    # bucket means the corpus or the tables above are wrong, not that the corpus
    # legitimately shrank.
    empty = [name for name in CATEGORIES if not buckets[name]]
    if empty:
        raise SystemExit(
            f"Error: no cases for {', '.join(empty)} in {input_path}. An empty category "
            f"would register a suite with zero tests and still pass. Check the corpus, "
            f"or drop the category from CATEGORIES here, from the foreach() in "
            f"tests/CMakeLists.txt, and delete its test TU."
        )

    for name in CATEGORIES:
        # The basename comes from the fixed CATEGORIES tuple, never from input, but
        # resolve and confirm containment anyway so the write target is provably
        # inside the directory that was passed in.
        out = (output_dir / f"unified_cases_{name}.json").resolve()
        if out.parent != output_dir:
            raise SystemExit(f"Error: refusing to write outside {output_dir}: {out}")
        out.write_text(json.dumps(buckets[name], separators=(",", ":")) + "\n", encoding="utf-8")

    total = sum(len(b) for b in buckets.values())
    parts = ", ".join(f"{name}={len(buckets[name])}" for name in CATEGORIES)
    print(f"Split {total} cases -> {parts}")


if __name__ == "__main__":
    main()
