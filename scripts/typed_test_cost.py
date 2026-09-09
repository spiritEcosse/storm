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


def command_for(db, tu, obj):
    try:
        entry = next(e for e in db if e["file"].endswith(tu))
    except StopIteration:
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
        if arg.endswith(".cpp") or arg.endswith(".cppm"):
            continue
        kept.append(arg)
    return kept + ["-c", entry["file"], "-o", obj], entry["directory"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build/debug",
                    help="configured build tree, relative to the repository root")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--obj", default="/tmp/typed_test_cost.o")
    ap.add_argument("tus", nargs="*", default=None,
                    help=f"TUs to measure (default: {len(DEFAULT_TUS)} incl. the control)")
    args = ap.parse_args()

    root = pathlib.Path(REPO_ROOT)
    helpers = root / "tests" / "test_db_helpers.h"
    db_path = root / args.build_dir / "compile_commands.json"
    if not os.path.isfile(safe_path(db_path)):
        sys.exit(f"{db_path} not found — configure the build first")
    db = json.loads(pathlib.Path(safe_path(db_path)).read_text())
    tus = args.tus or DEFAULT_TUS

    original = helpers.read_text()
    if TWO not in original:
        sys.exit("DatabaseTypes definition not found — check tests/test_db_helpers.h")

    results = {}
    try:
        for label, text in (("2 backends", original), ("1 backend", original.replace(TWO, ONE))):
            helpers.write_text(text)
            print(f"\n=== {label} ===", flush=True)
            for tu in tus:
                cmd, cwd = command_for(db, tu, args.obj)
                if cmd is None:
                    print(f"{tu:42} NO ENTRY")
                    continue
                best = None
                failed = None
                for _ in range(args.runs):
                    start = time.perf_counter()
                    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
                    elapsed = time.perf_counter() - start
                    if proc.returncode != 0:
                        failed = proc.stderr[-1500:]
                        break
                    best = elapsed if best is None else min(best, elapsed)
                if failed:
                    print(f"{tu:42} FAILED\n{failed}", flush=True)
                    continue
                results.setdefault(tu, {})[label] = best
                print(f"{tu:42} {best:6.2f}s", flush=True)
    finally:
        helpers.write_text(original)
        print("\nrestored tests/test_db_helpers.h", flush=True)

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
