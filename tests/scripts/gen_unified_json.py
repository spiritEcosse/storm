#!/usr/bin/env python3
"""
Regenerate tests/test_cases/unified_cases.json from unified_cases.yaml.

Emits only the keys the C++ TestCase parser understands:
  Bench-shared:  name (→ test_name alias handled in parser), model, operation,
                 where, order_by, group_by, distinct, limit, aggregate, join, setop
  Test-only:     query_type, dataset, dataset_size, expected,
                 insert_count, update_count, erase_count,
                 aggregations (chain tests), where_expr / has_where_expr

Legacy duplicate keys (agg_field, group_by_field, distinct_field, join_name,
limit_value, offset_value, aggregation, having_*, ...) are dropped.
"""

from __future__ import annotations
import json
import sys
from pathlib import Path

try:
    import yaml  # type: ignore
except ImportError:
    print("Error: PyYAML is required.  pip install pyyaml", file=sys.stderr)
    sys.exit(1)

# query_type values that map to aggregate.func when query_type is missing
_AGG_FUNCS = {"count", "sum", "avg", "min", "max", "count_distinct", "count_field"}
_GROUP_OPS  = {"group_count", "group_sum", "group_avg", "group_min", "group_max"}
_OP_MAP     = {"first": "first", "one": "get_where"}


def _derive_query_type(c: dict) -> str | None:
    """Infer query_type for cases that lack it."""
    qt = c.get("query_type")
    if qt:
        return qt
    # chain tests
    if c.get("aggregations"):
        return "chain"
    # scalar aggregate via new aggregate block
    agg = c.get("aggregate") or {}
    func = agg.get("func", "")
    if func in _AGG_FUNCS:
        return func
    return None


def _clean_case(c: dict) -> dict:
    out: dict = {}

    # --- identity / routing ---
    out["name"] = c["name"]
    out["model"] = c.get("model", "person")

    qt = _derive_query_type(c)
    if qt:
        out["query_type"] = qt

    if c.get("dataset"):
        out["dataset"] = c["dataset"]
    if c.get("dataset_size"):
        out["dataset_size"] = c["dataset_size"]

    # --- operation (first / get_where) ---
    op = c.get("operation") or _OP_MAP.get(qt or "", "")
    if op:
        out["operation"] = op

    # --- bench-shared nested specs (already in new format in YAML) ---
    if c.get("where"):
        out["where"] = c["where"]

    if c.get("where_expr"):
        out["where_expr"] = c["where_expr"]
        out["has_where_expr"] = True

    if c.get("order_by"):
        out["order_by"] = c["order_by"]

    # group_by — already new format; drop agg into aggregate below for group ops
    gb = c.get("group_by")
    if gb and isinstance(gb, dict) and gb.get("enabled"):
        out["group_by"] = gb
        # For group ops, embed the aggregate func inside group_by (no top-level aggregate)
        if qt in _GROUP_OPS:
            func_map = {
                "group_count": "count", "group_sum": "sum", "group_avg": "avg",
                "group_min":   "min",   "group_max": "max",
            }
            agg_field = c.get("agg_field", "")
            out["aggregate"] = {"enabled": True, "func": func_map[qt], "field": agg_field}

    if c.get("distinct") and isinstance(c["distinct"], dict) and c["distinct"].get("enabled"):
        out["distinct"] = c["distinct"]

    if c.get("limit") and isinstance(c["limit"], dict) and c["limit"].get("enabled"):
        out["limit"] = c["limit"]
    elif c.get("limit_value") or c.get("offset_value"):
        # fallback for cases that only have legacy limit_value
        lv = c.get("limit_value", 0) or 0
        ov = c.get("offset_value", 0) or 0
        if lv or ov:
            out["limit"] = {"enabled": True, "value": lv, "offset": ov}

    if c.get("join") and isinstance(c["join"], dict) and c["join"].get("enabled"):
        out["join"] = c["join"]

    # aggregate (scalar, not group-by)
    agg = c.get("aggregate")
    if agg and isinstance(agg, dict) and agg.get("enabled") and qt not in _GROUP_OPS:
        out["aggregate"] = agg
    elif qt in _AGG_FUNCS and "aggregate" not in out:
        # derive from legacy agg_field
        out["aggregate"] = {"enabled": True, "func": qt, "field": c.get("agg_field", "")}

    # --- test-only fields ---
    for k in ("insert_count", "update_count", "erase_count"):
        if c.get(k):
            out[k] = c[k]

    # chain aggregations
    if c.get("aggregations"):
        out["aggregations"] = c["aggregations"]

    if "expected" in c:
        out["expected"] = c["expected"]

    return out


def main() -> None:
    repo      = Path(__file__).resolve().parents[2]
    yaml_path = repo / "tests" / "test_cases" / "unified_cases.yaml"
    json_path = repo / "tests" / "test_cases" / "unified_cases.json"

    if len(sys.argv) >= 2:
        yaml_path = Path(sys.argv[1])
    if len(sys.argv) >= 3:
        json_path = Path(sys.argv[2])

    with yaml_path.open() as f:
        cases = yaml.safe_load(f)

    if not isinstance(cases, list):
        print("Error: expected top-level list in YAML", file=sys.stderr)
        sys.exit(1)

    cleaned = [_clean_case(c) for c in cases if isinstance(c, dict)]

    with json_path.open("w") as f:
        json.dump(cleaned, f, separators=(",", ":"))
        f.write("\n")

    print(f"Generated {len(cleaned)} cases → {json_path}")


if __name__ == "__main__":
    main()
