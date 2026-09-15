#!/usr/bin/env python3
"""What ONE TU instantiates — compile it with `-ftime-trace` and attribute the
instantiation events by SELF time.

`-ftime-trace`'s `dur` is INCLUSIVE, so a template whose body instantiates ten
others carries all ten in its own number. Self time (a node's duration minus its
directly-nested instantiation events) is what makes a per-template table sum to
something, and it is not a field in the trace — it has to be derived, which is
why this lives in a script rather than in a shell one-liner.

    scripts/dev-container.sh exec python3 scripts/trace_one.py \
        tests/crud/test_select.cpp --filter SelectStatement

`--filter` and `--subset` are comma-separated SUBSTRINGS matched against the
event's `detail` (the template's spelling); an event matches if any of them
occurs in it. Substrings rather than a regex so a filter taken from a doc cannot
become a pattern this script compiles. Both print a subtotal against Frontend,
so the exact filter behind a published number is the one on the command line —
record it next to the number.

ONE MEASUREMENT PER STATE IS NOT A RESULT. This replays a command that consumes
the prebuilt storm BMIs and does not check they are current: against a stale
build tree it silently traces a library that no longer exists. Re-configure and
rebuild before trusting a run, and see tu_ablation.py's docstring for why a
single before/after across a library edit is worthless.
"""
import argparse
import collections
import json
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "lib"))
from compile_replay import (REPO_ROOT, command_for, load_compile_db,  # noqa: E402
                            resolve_tu, safe_path)

# Event kinds that represent template work. ParseTemplate and
# DeduceTemplateArguments are included so a template's cost is not split between
# "instantiating" and "getting there".
INSTANTIATION_EVENTS = {
    "InstantiateFunction",
    "InstantiateClass",
    "InstantiateDefaultArgument",
    "ParseTemplate",
    "DeduceTemplateArguments",
}


def compile_with_trace(entry, out_dir):
    """Compile one TU with -ftime-trace; return the trace JSON's path.

    A relative --out-dir resolves against the repository root, not the current
    directory, so the script behaves the same from a subdirectory as
    load_compile_db already does with --build-dir.
    """
    cmd, cwd = command_for(entry)
    if not os.path.isabs(out_dir):
        out_dir = os.path.join(REPO_ROOT, out_dir)
    obj = pathlib.Path(safe_path(out_dir)) / (pathlib.Path(entry["file"]).stem + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        cmd + ["-ftime-trace", "-ftime-trace-granularity=0", "-c", entry["file"], "-o", str(obj)],
        cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        sys.exit(proc.stderr[-3000:])
    trace = obj.with_suffix(".json")
    if not trace.is_file():
        sys.exit(f"{trace}: clang wrote no trace — is this a -ftime-trace-capable build?")
    return trace


def self_times(trace_path):
    """(detail -> self microseconds, Frontend microseconds) for one trace.

    Self time is assigned by walking the events in start order and subtracting
    each one from the innermost still-open event that contains it, so a nested
    instantiation is charged to itself and not again to its parent.
    """
    events = [e for e in json.loads(pathlib.Path(trace_path).read_text(encoding="utf-8"))["traceEvents"]
              if e.get("ph") == "X" and e.get("dur")]
    frontend = sum(e["dur"] for e in events if e["name"] == "Frontend")
    if not frontend:
        sys.exit(f"{trace_path}: no Frontend event — every figure here is a share "
                 f"of it, so there is nothing to report")

    nested = sorted((e for e in events
                     if e["name"] in INSTANTIATION_EVENTS and "detail" in e.get("args", {})),
                    key=lambda e: (e["ts"], -e["dur"]))
    own = [e["dur"] for e in nested]
    open_stack = []
    for index, event in enumerate(nested):
        while open_stack and (nested[open_stack[-1]]["ts"] + nested[open_stack[-1]]["dur"]) <= event["ts"]:
            open_stack.pop()
        if open_stack:
            own[open_stack[-1]] -= event["dur"]
        open_stack.append(index)

    totals = collections.Counter()
    for event, duration in zip(nested, own):
        totals[event["args"]["detail"]] += duration
    return totals, frontend


def matching(totals, needles):
    return {d: v for d, v in totals.items() if any(n in d for n in needles)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", help="TU path, or a unique suffix of one")
    parser.add_argument("--build-dir", default="build/debug")
    parser.add_argument("--out-dir", default="build/trace",
                        help="where the object and its trace JSON land (inside the repository)")
    parser.add_argument("--filter", help="comma-separated substrings of the template "
                                         "spelling; prints a subtotal")
    parser.add_argument("--subset", help="second substring list, applied within --filter")
    parser.add_argument("--top", type=int, default=15)
    args = parser.parse_args()

    entry = resolve_tu(load_compile_db(args.build_dir), args.source)
    trace = compile_with_trace(entry, args.out_dir)
    totals, frontend = self_times(trace)
    grand = sum(totals.values())

    print(f"{entry['file']}\n  trace: {trace}")
    print(f"  Frontend {frontend / 1e6:.2f}s   instantiation self-time "
          f"{grand / 1e6:.2f}s ({100 * grand / frontend:.1f}%) over {len(totals)} entities")

    print(f"\n  top {args.top} by self time:")
    for detail, value in totals.most_common(args.top):
        print(f"    {value / 1e6:7.4f}s  {detail[:110]}")

    if args.filter:
        matched = matching(totals, args.filter.split(","))
        total = sum(matched.values())
        print(f"\n  filter [{args.filter}]: {total / 1e6:.4f}s "
              f"= {100 * total / frontend:.2f}% of Frontend, {len(matched)} distinct entities")
        for detail, value in sorted(matched.items(), key=lambda kv: -kv[1]):
            print(f"    {value / 1e6:7.4f}s  {detail[:120]}")
        if args.subset:
            inner = sum(matching(matched, args.subset.split(",")).values())
            print(f"\n  subset [{args.subset}] within it: {inner / 1e6:.4f}s "
                  f"= {100 * inner / frontend:.2f}% of Frontend")


if __name__ == "__main__":
    sys.exit(main())
