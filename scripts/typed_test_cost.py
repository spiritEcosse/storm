#!/usr/bin/env python3
"""Measure what the second backend costs in Storm's TYPED_TEST suites.

Every TYPED_TEST over DatabaseTypes instantiates its body once per backend,
which looks like a 2x. Measured, it is ~13%: the two instantiations share most
of their work. This script reproduces that by compiling real TUs as-is, then
again with DatabaseTypes narrowed to SQLite alone — test bodies untouched, so
the difference is the second instantiation and nothing else.

It edits tests/test_db_helpers.h in place and restores it in a finally block.
Run it on a clean tree, and check `git diff` afterwards if interrupted.

    scripts/dev-container.sh exec python3 scripts/typed_test_cost.py
"""
import argparse
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import time

# The repository this copy of the script belongs to. Paths derived from CLI
# arguments are confined to it — see safe_path.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))


def safe_path(path) -> str:
    """Resolve a CLI-derived path, confined to this repository.

    Called inside the filesystem access it guards rather than assigned first:
    that is the shape the taint analysis behind S8707 recognises as sanitizing
    the sink, and it keeps the guard visible at the point of use.
    """
    resolved = os.path.realpath(path)
    if resolved != REPO_ROOT and not resolved.startswith(REPO_ROOT + os.sep):
        raise SystemExit(f"{path}: outside the repository ({REPO_ROOT}); "
                         f"run the copy of this script that lives in that tree")
    return resolved


TWO = ("using DatabaseTypes = ::testing::Types<storm::db::sqlite::Connection, "
       "storm::db::postgresql::Connection>;")
ONE = "using DatabaseTypes = ::testing::Types<storm::db::sqlite::Connection>;"

# The four heaviest DatabaseTypes users, plus one control: test_collate.cpp
# declares SqliteTypes, so the edit cannot reach it and its delta is the noise
# floor (~1%). Keep the control — it is what makes the others readable.
DEFAULT_TUS = [
    "tests/schema/test_types.cpp",
    "tests/query/test_distinct.cpp",
    "tests/query/test_sql_verify.cpp",
    "tests/crud/test_conditional_update.cpp",
    "tests/query/test_collate.cpp",
]


def resolve_tus(db, requested):
    """Map the requested TU suffixes onto exact paths from the compile database.

    An allow-list, and the reason one is needed: everything downstream is fed to
    a compiler process, so nothing from argv may reach it. What the caller types
    only SELECTS here — the value that travels on is an element of the database's
    own set of files, never a transformation of the argument.
    """
    known = sorted({e["file"] for e in db})
    resolved = []
    for wanted in requested:
        match = next((f for f in known if f.endswith(wanted)), None)
        if match is None:
            print(f"{wanted:42} NO ENTRY")
            continue
        resolved.append(match)
    return resolved


def command_for(db, source_file, obj):
    """Build the compile command for an exact database entry."""
    entry = next((e for e in db if e["file"] == source_file), None)
    if entry is None:
        return None, None
    argv = shlex.split(entry["command"])
    if pathlib.Path(argv[0]).name in ("ccache", "sccache"):
        sys.exit(f"compiler launcher active ({argv[0]}) — "
                 "run: cmake -U CMAKE_CXX_COMPILER_LAUNCHER .")
    kept, skip = [], False
    for arg in argv:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-c"):
            skip = arg == "-o"
            continue
        if arg.endswith((".cpp", ".cppm")):
            continue
        kept.append(arg)
    return kept + ["-c", entry["file"], "-o", obj], entry["directory"]


def measure(cmd, cwd, runs: int):
    """Compile once per run, return (best seconds, error text). Min of N."""
    best = None
    for _ in range(runs):
        start = time.perf_counter()
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            return None, proc.stderr[-1500:]
        best = elapsed if best is None else min(best, elapsed)
    return best, None


def measure_all(db, tus, obj, runs: int, original: str, helpers: pathlib.Path):
    """Time every TU under both backend configurations.

    Edits tests/test_db_helpers.h between the two passes; the caller restores it
    in a finally block, so an interrupted run cannot leave the edit behind.
    """
    results = {}
    for label, text in (("2 backends", original), ("1 backend", original.replace(TWO, ONE))):
        helpers.write_text(text)
        print(f"\n=== {label} ===", flush=True)
        for source_file in tus:
            name = os.path.relpath(source_file, REPO_ROOT)
            cmd, cwd = command_for(db, source_file, obj)
            if cmd is None:
                print(f"{name:42} NO ENTRY")
                continue
            best, failed = measure(cmd, cwd, runs)
            if failed:
                print(f"{name:42} FAILED\n{failed}", flush=True)
                continue
            results.setdefault(name, {})[label] = best
            print(f"{name:42} {best:6.2f}s", flush=True)
    return results


def report(results) -> None:
    """Print the per-TU comparison and the total."""
    print(f"\n{'TU':42} {'2bk':>7} {'1bk':>7} {'delta':>8} {'%':>7}")
    total_two = total_one = 0.0
    for tu, row in results.items():
        two, one = row.get("2 backends"), row.get("1 backend")
        if two and one:
            total_two += two
            total_one += one
            print(f"{tu:42} {two:7.2f} {one:7.2f} {one - two:+8.2f} {100 * (one - two) / two:+6.1f}%")
    if total_two:
        print(f"{'TOTAL':42} {total_two:7.2f} {total_one:7.2f} "
              f"{total_one - total_two:+8.2f} {100 * (total_one - total_two) / total_two:+6.1f}%")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build/debug",
                    help="configured build tree, relative to the repository root")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("tus", nargs="*", default=None,
                    help=f"TUs to measure (default: {len(DEFAULT_TUS)} incl. the control)")
    args = ap.parse_args()

    root = pathlib.Path(REPO_ROOT)
    helpers = root / "tests" / "test_db_helpers.h"
    db_path = root / args.build_dir / "compile_commands.json"
    if not os.path.isfile(safe_path(db_path)):
        sys.exit(f"{db_path} not found — configure the build first")
    db = json.loads(pathlib.Path(safe_path(db_path)).read_text())
    tus = resolve_tus(db, args.tus or DEFAULT_TUS)
    if not tus:
        sys.exit("none of the requested TUs are in the compile database")

    original = helpers.read_text()
    if TWO not in original:
        sys.exit("DatabaseTypes definition not found — check tests/test_db_helpers.h")

    # The object file goes to a private temporary directory rather than a fixed
    # path in /tmp: a world-writable location is both a hazard (S5443) and a
    # collision waiting to happen between two concurrent runs.
    try:
        with tempfile.TemporaryDirectory() as tmp:
            results = measure_all(db, tus, str(pathlib.Path(tmp) / "probe.o"),
                                  args.runs, original, helpers)
    finally:
        helpers.write_text(original)
        print("\nrestored tests/test_db_helpers.h", flush=True)

    report(results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
