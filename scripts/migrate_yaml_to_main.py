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

    # Determine data source
    if model == "person":
        data = list(PEOPLE)
    elif model == "message":
        data = list(MESSAGES)
    else:
        return tc

    # Apply WHERE filter
    filtered = apply_where(data, where)

    exp = tc.get("expected", {})

    # SELECT queries
    if qt in ("select", "first", "one"):
        exp["count"] = len(filtered)

        # Order by first_name / first_age
        ob = tc.get("order_by")
        if ob and filtered:
            field = ob["field"]
            asc = ob.get("asc", True)
            # Sort, handling None
            def sort_key(p):
                v = p.get(field)
                if v is None:
                    return (0, "") if asc else (1, "")
                return (1, v) if asc else (0, v)

            sorted_data = sorted(filtered, key=lambda p: sort_key(p), reverse=not asc if not isinstance(sort_key(filtered[0]), tuple) else False)
            # Actually, let's do proper sorting
            none_items = [p for p in filtered if p.get(field) is None]
            non_none = [p for p in filtered if p.get(field) is not None]
            non_none.sort(key=lambda p: p[field], reverse=not asc)
            if asc:
                sorted_data = none_items + non_none
            else:
                sorted_data = non_none + none_items

            # Apply limit/offset
            offset = tc.get("offset_value", 0)
            limit = tc.get("limit_value")
            if offset:
                sorted_data = sorted_data[offset:]
            if limit is not None:
                sorted_data = sorted_data[:limit]

            exp["count"] = len(sorted_data)

            if sorted_data:
                if "first_name" in exp:
                    exp["first_name"] = sorted_data[0]["name"]
                if "first_age" in exp:
                    exp["first_age"] = sorted_data[0]["age"]
        else:
            # Apply limit/offset without order
            offset = tc.get("offset_value", 0)
            limit = tc.get("limit_value")
            result = filtered
            if offset:
                result = result[offset:]
            if limit is not None:
                result = result[:limit]
            exp["count"] = len(result)

    # Scalar aggregates
    elif qt in ("count", "count_field", "count_distinct", "sum", "avg", "min", "max"):
        agg_field = tc.get("agg_field", "age")
        val, typ = compute_agg(filtered, agg_field, qt)
        if typ == "int":
            exp["int_val"] = val
            exp.pop("dbl_val", None)
        else:
            exp["dbl_val"] = val
            exp.pop("int_val", None)

    # GROUP BY
    elif qt.startswith("group_"):
        gb_field = tc.get("group_by_field")
        agg_field = tc.get("agg_field", "age")
        agg_type = qt.replace("group_", "")  # count, sum, avg, min, max

        # Apply WHERE first
        groups = defaultdict(list)
        for p in filtered:
            key = p.get(gb_field)
            groups[key].append(p)

        # Apply HAVING if present
        having_field = tc.get("having_field")
        having_op = tc.get("having_op")
        having_val = tc.get("having_value_int")
        if having_val is None:
            having_val = tc.get("having_value_dbl")
        if having_val is None:
            having_val = tc.get("having_value_str")

        if having_field and having_op:
            filtered_groups = {}
            for key, members in groups.items():
                if key is not None and compare(key, having_op, having_val):
                    filtered_groups[key] = members
            groups = filtered_groups

        # Sort group keys
        non_none_keys = sorted(k for k in groups if k is not None)
        group_keys = non_none_keys

        # Compute aggregation per group
        group_agg = []
        for key in group_keys:
            members = groups[key]
            if agg_type == "count":
                group_agg.append(len(members))
            elif agg_type == "sum":
                vals = [p[agg_field] for p in members if p.get(agg_field) is not None]
                group_agg.append(sum(vals))
            elif agg_type == "avg":
                vals = [p[agg_field] for p in members if p.get(agg_field) is not None]
                group_agg.append(sum(vals) / len(vals) if vals else 0)
            elif agg_type == "min":
                vals = [p[agg_field] for p in members if p.get(agg_field) is not None]
                group_agg.append(min(vals) if vals else 0)
            elif agg_type == "max":
                vals = [p[agg_field] for p in members if p.get(agg_field) is not None]
                group_agg.append(max(vals) if vals else 0)

        # Apply order_by on groups
        ob = tc.get("order_by")
        if ob:
            asc = ob.get("asc", True)
            # Sort by key
            combined = list(zip(group_keys, group_agg))
            combined.sort(key=lambda x: x[0], reverse=not asc)
            group_keys = [c[0] for c in combined]
            group_agg = [c[1] for c in combined]

        # Apply limit/offset
        offset = tc.get("offset_value", 0)
        limit = tc.get("limit_value")
        if offset:
            group_keys = group_keys[offset:]
            group_agg = group_agg[offset:]
        if limit is not None:
            group_keys = group_keys[:limit]
            group_agg = group_agg[:limit]

        total_count = sum(len(groups[k]) for k in (non_none_keys if not having_field else group_keys))
        exp["count"] = total_count if not (offset or limit) else sum(group_agg) if agg_type == "count" else total_count
        exp["groups_count"] = len(group_keys)

        if group_keys:
            # Determine if keys are int or string
            if isinstance(group_keys[0], str):
                exp["group_keys"] = group_keys
            else:
                exp["group_keys"] = [int(k) if isinstance(k, (int, float)) and k == int(k) else k for k in group_keys]

            # Determine if agg values are int or float
            if agg_type in ("avg", "min", "max"):
                exp["group_agg_dbl"] = [round(v, 2) for v in group_agg]
                exp.pop("group_agg_int", None)
            else:
                exp["group_agg_int"] = [int(v) for v in group_agg]
                exp.pop("group_agg_dbl", None)

    # Chain aggregates
    elif qt == "chain":
        aggs = tc.get("aggregations", [])
        for agg in aggs:
            func = agg.get("func", "")
            field = agg.get("field", "age")
            if func == "count":
                agg["res_value"] = len(filtered)
            elif func == "sum":
                vals = [p[field] for p in filtered if p.get(field) is not None]
                agg["res_value"] = sum(vals)
            elif func == "avg":
                vals = [p[field] for p in filtered if p.get(field) is not None]
                agg["res_value"] = round(sum(vals) / len(vals), 1) if vals else 0.0
            elif func == "min":
                vals = [p[field] for p in filtered if p.get(field) is not None]
                agg["res_value"] = float(min(vals)) if vals else 0.0
            elif func == "max":
                vals = [p[field] for p in filtered if p.get(field) is not None]
                agg["res_value"] = float(max(vals)) if vals else 0.0

    # DISTINCT
    elif qt == "distinct":
        d_fields = []
        for i in range(1, 3):
            f = tc.get(f"distinct_field" if i == 1 else f"distinct_field{i}")
            if i == 1:
                f = tc.get("distinct_field")
            if f:
                d_fields.append(f)

        if len(d_fields) == 1:
            vals = set()
            for p in filtered:
                v = p.get(d_fields[0])
                if v is not None:
                    vals.add(v)
            exp["count"] = len(vals)
        elif len(d_fields) == 2:
            vals = set()
            for p in filtered:
                v1 = p.get(d_fields[0])
                v2 = p.get(d_fields[1])
                vals.add((v1, v2))
            exp["count"] = len(vals)

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
