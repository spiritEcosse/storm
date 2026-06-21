#!/usr/bin/env python3
"""Migrate unified_cases.yaml from multiple datasets to 'main' / 'main_msg'."""

import yaml
import sys
import copy
from collections import Counter, defaultdict

# ============================================================================
# PEOPLE_25 dataset
# ============================================================================
PEOPLE = [
    {"name": "Alice",   "age": 25, "salary": 55000.0, "is_active": True,  "years_experience": 5,  "department": "Engineering", "score": 85,   "nickname": "Ali"},
    {"name": "Bob",     "age": 30, "salary": 62000.0, "is_active": True,  "years_experience": 10, "department": "Sales",       "score": 90,   "nickname": "Bobby"},
    {"name": "Charlie", "age": 35, "salary": 78000.0, "is_active": False, "years_experience": 15, "department": "Marketing",   "score": None, "nickname": None},
    {"name": "Diana",   "age": 28, "salary": 48000.0, "is_active": True,  "years_experience": 5,  "department": "HR",          "score": 75,   "nickname": "Di"},
    {"name": "Eve",     "age": 40, "salary": 92000.0, "is_active": False, "years_experience": 10, "department": "Engineering", "score": None, "nickname": None},
    {"name": "Frank",   "age": 45, "salary": 88000.0, "is_active": True,  "years_experience": 15, "department": "Sales",       "score": 60,   "nickname": None},
    {"name": "Grace",   "age": 25, "salary": 52000.0, "is_active": True,  "years_experience": 5,  "department": "Marketing",   "score": 95,   "nickname": "Gracie"},
    {"name": "Henry",   "age": 33, "salary": 70000.0, "is_active": False, "years_experience": 10, "department": "Support",     "score": None, "nickname": None},
    {"name": "Ivy",     "age": 30, "salary": 65000.0, "is_active": True,  "years_experience": 5,  "department": "Engineering", "score": 80,   "nickname": "Iv"},
    {"name": "Jack",    "age": 38, "salary": 85000.0, "is_active": False, "years_experience": 15, "department": "HR",          "score": None, "nickname": None},
    {"name": "Karen",   "age": 25, "salary": 50000.0, "is_active": True,  "years_experience": 5,  "department": "Sales",       "score": 85,   "nickname": "Kiki"},
    {"name": "Leo",     "age": 42, "salary": 95000.0, "is_active": True,  "years_experience": 10, "department": "Engineering", "score": 70,   "nickname": None},
    {"name": "Mia",     "age": 28, "salary": 46000.0, "is_active": True,  "years_experience": 5,  "department": "Marketing",   "score": None, "nickname": None},
    {"name": "Nick",    "age": 35, "salary": 72000.0, "is_active": False, "years_experience": 15, "department": "Support",     "score": 55,   "nickname": "Nicky"},
    {"name": "Olivia",  "age": 48, "salary": 98000.0, "is_active": True,  "years_experience": 10, "department": "Sales",       "score": None, "nickname": None},
    {"name": "Paul",    "age": 22, "salary": 32000.0, "is_active": False, "years_experience": 5,  "department": "HR",          "score": 40,   "nickname": None},
    {"name": "Quinn",   "age": 30, "salary": 67000.0, "is_active": True,  "years_experience": 10, "department": "Engineering", "score": None, "nickname": None},
    {"name": "Rachel",  "age": 36, "salary": 76000.0, "is_active": False, "years_experience": 5,  "department": "Support",     "score": 65,   "nickname": "Rach"},
    {"name": "Sam",     "age": 40, "salary": 90000.0, "is_active": True,  "years_experience": 15, "department": "Marketing",   "score": None, "nickname": None},
    {"name": "Tina",    "age": 27, "salary": 44000.0, "is_active": True,  "years_experience": 5,  "department": "Sales",       "score": 88,   "nickname": "T"},
    {"name": "Uma",     "age": 33, "salary": 69000.0, "is_active": False, "years_experience": 10, "department": "HR",          "score": 50,   "nickname": None},
    {"name": "Victor",  "age": 45, "salary": 93000.0, "is_active": True,  "years_experience": 15, "department": "Engineering", "score": None, "nickname": None},
    {"name": "Wendy",   "age": 29, "salary": 58000.0, "is_active": True,  "years_experience": 10, "department": "Support",     "score": 78,   "nickname": "Wen"},
    {"name": "Xander",  "age": 38, "salary": 82000.0, "is_active": False, "years_experience": 15, "department": "Marketing",   "score": None, "nickname": None},
    {"name": "Yara",    "age": 22, "salary": 35000.0, "is_active": True,  "years_experience": 5,  "department": "Support",     "score": 92,   "nickname": "Yari"},
]

MESSAGES = [
    {"content": "Hello",      "value": 10, "sender": "Alice"},
    {"content": "World",      "value": 20, "sender": "Alice"},
    {"content": "Hi there",   "value": 30, "sender": "Alice"},
    {"content": "Goodbye",    "value": 40, "sender": "Bob"},
    {"content": "Testing",    "value": 50, "sender": "Bob"},
    {"content": "Greetings",  "value": 60, "sender": "Charlie"},
    {"content": "Reply",      "value": 70, "sender": "Charlie"},
    {"content": "Quick note", "value": 80, "sender": "Diana"},
]

# Which old datasets map to main/main_msg
DATASET_MAP = {
    "standard_5": "main",
    "score_5": "main",
    "standard_10": "main",
    "where_3": "main",
    "dept_8": "main",
    "dept_8_msg_5": "main_msg",
    "join": "main_msg",
}

def compare(val, op, ref):
    """Apply comparison operator."""
    if op == "==": return val == ref
    if op == "!=": return val != ref
    if op == ">": return val > ref
    if op == ">=": return val >= ref
    if op == "<": return val < ref
    if op == "<=": return val <= ref
    if op == "LIKE":
        import re
        pattern = ref.replace("%", ".*").replace("_", ".")
        return bool(re.match(f"^{pattern}$", str(val)))
    if op == "IN":
        return val in ref
    if op == "BETWEEN":
        return ref[0] <= val <= ref[1]
    raise ValueError(f"Unknown op: {op}")

def get_field(p, field):
    return p[field]

def apply_where(data, where):
    """Filter data by where clause."""
    if not where:
        return data
    field = where.get("field")
    op = where.get("op")
    # value can be int, float, bool, or string
    val = where.get("value")
    if val is None:
        val = where.get("value_int")
    if val is None:
        val = where.get("value_dbl")
    if val is None:
        val = where.get("value_str")

    result = []
    for p in data:
        pval = get_field(p, field)
        if pval is None:
            continue
        try:
            if compare(pval, op, val):
                result.append(p)
        except TypeError:
            continue
    return result

def compute_agg(data, agg_field, query_type):
    """Compute aggregate value."""
    if query_type == "count":
        return len(data), "int"
    if query_type == "count_field":
        vals = [p[agg_field] for p in data if p.get(agg_field) is not None]
        return len(vals), "int"
    if query_type == "count_distinct":
        vals = [p[agg_field] for p in data if p.get(agg_field) is not None]
        return len(set(vals)), "int"

    vals = [p[agg_field] for p in data if p.get(agg_field) is not None]
    if not vals:
        return 0, "int"

    if query_type == "sum":
        s = sum(vals)
        if isinstance(s, float):
            return s, "dbl"
        return s, "int"
    if query_type == "avg":
        return sum(vals) / len(vals), "dbl"
    if query_type == "min":
        return float(min(vals)), "dbl"
    if query_type == "max":
        return float(max(vals)), "dbl"
    raise ValueError(f"Unknown agg: {query_type}")


def _sort_with_nulls(data, field, asc):
    """Sort data by field, placing None values first (asc) or last (desc)."""
    none_items = [p for p in data if p.get(field) is None]
    non_none = [p for p in data if p.get(field) is not None]
    non_none.sort(key=lambda p: p[field], reverse=not asc)
    if asc:
        return none_items + non_none
    return non_none + none_items


def _apply_limit_offset(data, tc):
    """Apply limit and offset from test case to data list."""
    offset = tc.get("offset_value", 0)
    limit = tc.get("limit_value")
    if offset:
        data = data[offset:]
    if limit is not None:
        data = data[:limit]
    return data


def _process_select(tc, filtered, exp):
    """Process SELECT/FIRST/ONE query types."""
    exp["count"] = len(filtered)

    ob = tc.get("order_by")
    if ob and filtered:
        field = ob["field"]
        asc = ob.get("asc", True)
        sorted_data = _sort_with_nulls(filtered, field, asc)
        sorted_data = _apply_limit_offset(sorted_data, tc)
        exp["count"] = len(sorted_data)

        if sorted_data:
            if "first_name" in exp:
                exp["first_name"] = sorted_data[0]["name"]
            if "first_age" in exp:
                exp["first_age"] = sorted_data[0]["age"]
    else:
        result = _apply_limit_offset(filtered, tc)
        exp["count"] = len(result)


def _process_scalar_agg(tc, filtered, exp):
    """Process scalar aggregate query types (count, sum, avg, etc.)."""
    qt = tc.get("query_type", "")
    agg_field = tc.get("agg_field", "age")
    val, typ = compute_agg(filtered, agg_field, qt)
    if typ == "int":
        exp["int_val"] = val
        exp.pop("dbl_val", None)
    else:
        exp["dbl_val"] = val
        exp.pop("int_val", None)


def _get_having_value(tc):
    """Extract HAVING comparison value from test case."""
    having_val = tc.get("having_value_int")
    if having_val is None:
        having_val = tc.get("having_value_dbl")
    if having_val is None:
        having_val = tc.get("having_value_str")
    return having_val


def _compute_group_agg_value(agg_type, members, agg_field):
    """Compute a single aggregation value for a group."""
    if agg_type == "count":
        return len(members)
    vals = [p[agg_field] for p in members if p.get(agg_field) is not None]
    if agg_type == "sum":
        return sum(vals)
    if agg_type == "avg":
        return sum(vals) / len(vals) if vals else 0
    if agg_type == "min":
        return min(vals) if vals else 0
    if agg_type == "max":
        return max(vals) if vals else 0
    return 0


def _apply_group_pagination(group_keys, group_agg, tc):
    """Apply order_by, offset, limit to group results."""
    ob = tc.get("order_by")
    if ob:
        asc = ob.get("asc", True)
        combined = sorted(zip(group_keys, group_agg), key=lambda x: x[0], reverse=not asc)
        group_keys = [c[0] for c in combined]
        group_agg = [c[1] for c in combined]

    offset = tc.get("offset_value", 0)
    limit = tc.get("limit_value")
    if offset:
        group_keys = group_keys[offset:]
        group_agg = group_agg[offset:]
    if limit is not None:
        group_keys = group_keys[:limit]
        group_agg = group_agg[:limit]
    return group_keys, group_agg


def _write_group_expected(exp, group_keys, group_agg, agg_type):
    """Write group results into expected dict."""
    if not group_keys:
        return
    if isinstance(group_keys[0], str):
        exp["group_keys"] = group_keys
    else:
        exp["group_keys"] = [int(k) if isinstance(k, (int, float)) and k == int(k) else k for k in group_keys]

    if agg_type in ("avg", "min", "max"):
        exp["group_agg_dbl"] = [round(v, 2) for v in group_agg]
        exp.pop("group_agg_int", None)
    else:
        exp["group_agg_int"] = [int(v) for v in group_agg]
        exp.pop("group_agg_dbl", None)


def _process_group_by(tc, filtered, exp):
    """Process GROUP BY query types."""
    qt = tc.get("query_type", "")
    gb_field = tc.get("group_by_field")
    agg_field = tc.get("agg_field", "age")
    agg_type = qt.replace("group_", "")

    groups = defaultdict(list)
    for p in filtered:
        groups[p.get(gb_field)].append(p)

    having_field = tc.get("having_field")
    having_op = tc.get("having_op")
    having_val = _get_having_value(tc)

    if having_field and having_op:
        groups = {
            key: members for key, members in groups.items()
            if key is not None and compare(key, having_op, having_val)
        }

    non_none_keys = sorted(k for k in groups if k is not None)
    group_keys = list(non_none_keys)
    group_agg = [_compute_group_agg_value(agg_type, groups[k], agg_field) for k in group_keys]

    group_keys, group_agg = _apply_group_pagination(group_keys, group_agg, tc)

    count_keys = non_none_keys if not having_field else group_keys
    total_count = sum(len(groups[k]) for k in count_keys)
    offset = tc.get("offset_value", 0)
    limit = tc.get("limit_value")
    has_pagination = offset or limit
    if not has_pagination:
        exp["count"] = total_count
    elif agg_type == "count":
        exp["count"] = sum(group_agg)
    else:
        exp["count"] = total_count
    exp["groups_count"] = len(group_keys)

    _write_group_expected(exp, group_keys, group_agg, agg_type)


def _compute_chain_agg(func, vals):
    """Compute a single chain aggregate value."""
    if func == "sum":
        return sum(vals)
    if func == "avg":
        return round(sum(vals) / len(vals), 1) if vals else 0.0
    if func == "min":
        return float(min(vals)) if vals else 0.0
    if func == "max":
        return float(max(vals)) if vals else 0.0
    return 0


def _process_chain(filtered, tc):
    """Process chain aggregate query types."""
    for agg in tc.get("aggregations", []):
        func = agg.get("func", "")
        field = agg.get("field", "age")
        if func == "count":
            agg["res_value"] = len(filtered)
            continue
        vals = [p[field] for p in filtered if p.get(field) is not None]
        agg["res_value"] = _compute_chain_agg(func, vals)


def _process_distinct(tc, filtered, exp):
    """Process DISTINCT query types."""
    d_fields = []
    f1 = tc.get("distinct_field")
    if f1:
        d_fields.append(f1)
    f2 = tc.get("distinct_field2")
    if f2:
        d_fields.append(f2)

    if len(d_fields) == 1:
        vals = {p.get(d_fields[0]) for p in filtered if p.get(d_fields[0]) is not None}
        exp["count"] = len(vals)
    elif len(d_fields) == 2:
        vals = {(p.get(d_fields[0]), p.get(d_fields[1])) for p in filtered}
        exp["count"] = len(vals)


def process_case(tc):
    """Process a single test case and update expected values."""
    old_ds = tc.get("dataset", "")
    if old_ds not in DATASET_MAP:
        return tc  # keep as-is (empty, custom, generated)

    new_ds = DATASET_MAP[old_ds]
    tc = copy.deepcopy(tc)
    tc["dataset"] = new_ds

    model = tc.get("model", "person")
    qt = tc.get("query_type", "")
    where = tc.get("where")

    if model == "person":
        data = list(PEOPLE)
    elif model == "message":
        data = list(MESSAGES)
    else:
        return tc

    filtered = apply_where(data, where)
    exp = tc.get("expected", {})

    if qt in ("select", "first", "one"):
        _process_select(tc, filtered, exp)
    elif qt in ("count", "count_field", "count_distinct", "sum", "avg", "min", "max"):
        _process_scalar_agg(tc, filtered, exp)
    elif qt.startswith("group_"):
        _process_group_by(tc, filtered, exp)
    elif qt == "chain":
        _process_chain(filtered, tc)
    elif qt == "distinct":
        _process_distinct(tc, filtered, exp)

    tc["expected"] = exp
    return tc


def main():
    with open("tests/test_cases/unified_cases.yaml") as f:
        cases = yaml.safe_load(f)

    new_cases = []
    for tc in cases:
        new_cases.append(process_case(tc))

    # Output
    with open("tests/test_cases/unified_cases.yaml", "w") as f:
        yaml.dump(new_cases, f, default_flow_style=False, sort_keys=False, allow_unicode=True, width=120)

    print(f"Processed {len(cases)} cases")
    changed = sum(1 for old, new in zip(cases, new_cases) if old.get("dataset") != new.get("dataset"))
    print(f"Changed dataset in {changed} cases")


if __name__ == "__main__":
    main()
