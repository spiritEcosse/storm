#!/usr/bin/env python3
"""
Migrate tests/test_cases/unified_cases.yaml to QueryBuilder-shaped nested specs.

Reads the legacy flat-field YAML and writes:
  - tests/test_cases/unified_cases.yaml  (rewritten with nested sub-specs)
  - tests/test_cases/unified_cases.json  (same data, compact JSON for #embed)

Transformation:
  - where: {field, op, value}         -> where: {enabled, conditions:[...], condition_count, combine_and}
  - where: {field, op, values:[...]}  -> conditions[0].values: [...] (IN list)
  - where: {field, op:BETWEEN, value, upper} -> conditions[0].values: [value, upper]
  - where: {field, op, value, field2, op2, value2, logic} -> two conditions
  - where: {..., field3, op3, value3} -> three conditions
  - op: is_null / is_not_null -> 'IS NULL' / 'IS NOT NULL'
  - where_expr: {...} (3 cases) -> has_where_expr: true at top level (where: omitted)
  - order_by: {field, asc} -> order_by: [{enabled, field, direction}]
  - limit_value / offset_value -> limit: {enabled, value, offset}
  - query_type sum/avg/min/max/count + agg_field -> aggregate: {enabled, func, field}
  - query_type count_distinct + agg_field -> aggregate: {enabled, func: count_distinct, field}
  - query_type group_count/group_sum/... (handled via group_by below; no top-level aggregate)
  - group_by_field(2) + having_* -> group_by: {enabled, fields:[...], having:{...}}
  - distinct_field(2) -> distinct: {enabled, fields:[...]}
  - join_name -> join: {enabled, type: inner, related: Message, fk: sender}
  - query_type first/one -> operation: first / get_where

Backward-compat fields kept (so old runners still compile): query_type, agg_field,
having_*, group_by_field*, distinct_field*, join_name, limit_value, offset_value,
insert_count, update_count, erase_count.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

try:
    import yaml  # type: ignore
except ImportError:
    print("Error: PyYAML is required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(1)


NULL_OP_MAP = {"is_null": "IS NULL", "is_not_null": "IS NOT NULL"}

AGG_QUERY_TYPES = {"count", "sum", "avg", "min", "max", "count_distinct"}
GROUP_QUERY_TYPES = {"group_count", "group_sum", "group_avg", "group_min", "group_max"}
OPERATION_MAP = {"first": "first", "one": "get_where"}


def normalize_op(op: str) -> str:
    return NULL_OP_MAP.get(op, op)


def value_count_for(op: str, values: list) -> int:
    if op in ("IS NULL", "IS NOT NULL"):
        return 0
    return len(values)


def make_condition(field: str, op: str, values: list) -> dict:
    op_n = normalize_op(op)
    return {
        "field": field,
        "op": op_n,
        "values": values,
        "value_count": value_count_for(op_n, values),
    }


def transform_where(w: dict) -> dict:
    """Convert a flat YAML where dict into the nested WhereSection format."""
    conditions: list[dict] = []
    combine_and = True

    op1 = w.get("op", "")
    field1 = w.get("field", "")

    if "values" in w:
        values1 = list(w["values"])
    elif "upper" in w:
        values1 = [w.get("value"), w["upper"]]
    elif normalize_op(op1) in ("IS NULL", "IS NOT NULL"):
        values1 = []
    elif "value" in w:
        values1 = [w["value"]]
    else:
        values1 = []

    conditions.append(make_condition(field1, op1, values1))

    if "field2" in w:
        op2 = w.get("op2", "")
        v2 = w["value2"] if "value2" in w else None
        values2 = [v2] if v2 is not None else []
        conditions.append(make_condition(w["field2"], op2, values2))
        combine_and = w.get("logic", "AND") == "AND"

    if "field3" in w:
        op3 = w.get("op3", "")
        v3 = w["value3"] if "value3" in w else None
        values3 = [v3] if v3 is not None else []
        conditions.append(make_condition(w["field3"], op3, values3))

    return {
        "enabled": True,
        "conditions": conditions,
        "condition_count": len(conditions),
        "combine_and": combine_and,
    }


def transform_order_by(ob: dict) -> list[dict]:
    return [
        {
            "enabled": True,
            "field": ob["field"],
            "direction": "ASC" if ob.get("asc", True) else "DESC",
        }
    ]


def transform_limit(case: dict) -> dict | None:
    lv = case.get("limit_value")
    ov = case.get("offset_value")
    if lv is None and ov is None:
        return None
    return {
        "enabled": True,
        "value": lv if lv is not None else 0,
        "offset": ov if ov is not None else 0,
    }


def transform_aggregate(case: dict) -> dict | None:
    qt = case.get("query_type", "")
    agg_inline = case.get("aggregation")

    if agg_inline:
        func = agg_inline.get("func", "")
        if not func:
            return None
        return {
            "enabled": True,
            "func": func,
            "field": agg_inline.get("field", ""),
        }

    if qt in AGG_QUERY_TYPES:
        return {
            "enabled": True,
            "func": qt,
            "field": case.get("agg_field", ""),
        }
    return None


def transform_group_by(case: dict) -> dict | None:
    gf = case.get("group_by_field")
    if not gf:
        return None
    fields = [gf]
    if case.get("group_by_field2"):
        fields.append(case["group_by_field2"])
    having: dict = {"enabled": False}
    if case.get("having_field"):
        having = {
            "enabled": True,
            "field": case["having_field"],
            "op": case.get("having_op", ""),
            "value": case.get("having_value_int", 0),
        }
    return {"enabled": True, "fields": fields, "having": having}


def transform_distinct(case: dict) -> dict | None:
    df = case.get("distinct_field")
    if not df:
        return None
    fields = [df]
    if case.get("distinct_field2"):
        fields.append(case["distinct_field2"])
    return {"enabled": True, "fields": fields}


def transform_join(case: dict) -> dict | None:
    jn = case.get("join_name")
    if not jn:
        return None
    # join_name in legacy YAML is always "sender" (Message -> Person FK).
    return {
        "enabled": True,
        "type": "inner",
        "related": "Person",
        "fk": "sender",
    }


def transform_case(case: dict) -> dict:
    out: dict = {}

    # Top-level scalars copied as-is (kept for backward compat with old runners).
    for k in (
        "name",
        "model",
        "query_type",
        "dataset",
        "dataset_size",
        "limit_value",
        "offset_value",
        "insert_count",
        "update_count",
        "erase_count",
        "agg_field",
        "group_by_field",
        "group_by_field2",
        "having_field",
        "having_op",
        "having_value_int",
        "distinct_field",
        "distinct_field2",
        "join_name",
        "chain_len",
    ):
        if k in case and case[k] is not None:
            out[k] = case[k]

    # where_expr: keep as-is in YAML and signal via has_where_expr in JSON.
    if "where_expr" in case:
        out["where_expr"] = case["where_expr"]
        out["has_where_expr"] = True
    elif "where" in case and case["where"]:
        out["where"] = transform_where(case["where"])

    if "order_by" in case and case["order_by"]:
        out["order_by"] = transform_order_by(case["order_by"])

    agg = transform_aggregate(case)
    if agg is not None and case.get("query_type") not in GROUP_QUERY_TYPES:
        out["aggregate"] = agg

    # Preserve the legacy `aggregation:` block for old chain runners.
    if "aggregation" in case and case["aggregation"]:
        out["aggregation"] = case["aggregation"]

    if "aggregations" in case and case["aggregations"]:
        out["aggregations"] = case["aggregations"]

    gb = transform_group_by(case)
    if gb is not None:
        out["group_by"] = gb

    dist = transform_distinct(case)
    if dist is not None:
        out["distinct"] = dist

    lim = transform_limit(case)
    if lim is not None:
        out["limit"] = lim

    join = transform_join(case)
    if join is not None:
        out["join"] = join

    qt = case.get("query_type", "")
    if qt in OPERATION_MAP:
        out["operation"] = OPERATION_MAP[qt]

    if "expected" in case:
        out["expected"] = case["expected"]

    return out


def load_yaml(path: Path) -> list[dict]:
    with path.open() as f:
        data = yaml.safe_load(f)
    if not isinstance(data, list):
        print(f"Error: expected top-level YAML list in {path}", file=sys.stderr)
        sys.exit(1)
    return data


def dump_yaml(path: Path, cases: list[dict]) -> None:
    with path.open("w") as f:
        yaml.safe_dump(cases, f, sort_keys=False, default_flow_style=False, width=1000)


def dump_json(path: Path, cases: list[dict]) -> None:
    with path.open("w") as f:
        json.dump(cases, f, separators=(",", ":"))
        f.write("\n")


def main() -> None:
    repo = Path(__file__).resolve().parents[2]
    yaml_path = repo / "tests" / "test_cases" / "unified_cases.yaml"
    json_path = repo / "tests" / "test_cases" / "unified_cases.json"

    if len(sys.argv) >= 2:
        yaml_path = Path(sys.argv[1])
    if len(sys.argv) >= 3:
        json_path = Path(sys.argv[2])

    cases = load_yaml(yaml_path)
    migrated = [transform_case(c) for c in cases if isinstance(c, dict)]

    dump_yaml(yaml_path, migrated)
    dump_json(json_path, migrated)
    print(f"Migrated {len(migrated)} cases -> {yaml_path}, {json_path}")


if __name__ == "__main__":
    main()
