#!/usr/bin/env python3
"""Shared harness for the compile-time measurement scripts in scripts/.

`compile_time_probe.py`, `typed_test_cost.py` and `tu_ablation.py` all do the
same three things before they can measure anything: confine a CLI-derived path
to this repository, read `compile_commands.json` and rebuild one entry's command
without its output operands, and time that command serially taking the best of N
runs. Those live here so the three agree by construction — and so SonarCloud's
duplication gate, which does not exclude `scripts/**`, has nothing to report.

Not a CLI: import it from a script in scripts/.
"""
import json
import os
import pathlib
import shlex
import subprocess
import sys
import time

# The repository this copy of the harness belongs to (scripts/lib/ -> two up).
# Paths derived from CLI arguments are confined to it — see safe_path.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))


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


def load_compile_db(build_dir):
    """Read compile_commands.json out of a build tree inside this repository."""
    db_path = pathlib.Path(REPO_ROOT) / build_dir / "compile_commands.json"
    if not os.path.isfile(safe_path(db_path)):
        sys.exit(f"{db_path} not found — configure the build first")
    return json.loads(pathlib.Path(safe_path(db_path)).read_text(encoding="utf-8"))


def strip_output_flags(argv):
    """Drop -o/-c and the source operand from a compile command's argv."""
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
    return kept


def command_for(entry):
    """(argv without output operands, working directory) for one db entry.

    Refuses a compiler launcher: timing through a cache measures the cache. A
    launcher set with `CACHE ... FORCE` survives removal of the module that set
    it, which is how a stale one silently served objects in 0.2 s and invalidated
    a whole measurement round.
    """
    argv = shlex.split(entry["command"])
    if pathlib.Path(argv[0]).name in ("ccache", "sccache"):
        sys.exit(
            f"compiler launcher active ({argv[0]}). Timing through a cache is "
            "meaningless — run: cmake -U CMAKE_CXX_COMPILER_LAUNCHER ."
        )
    return strip_output_flags(argv), entry["directory"]


def time_compile(cmd, cwd, source, obj, runs, extra=()):
    """Compile `source` `runs` times serially; return (best seconds, error text).

    Best-of-N rather than mean: the distribution is one true cost plus scheduler
    noise on top, so the minimum is the estimator that noise cannot inflate.
    """
    full = cmd + list(extra) + ["-c", str(source), "-o", str(obj)]
    best = None
    for _ in range(runs):
        start = time.perf_counter()
        proc = subprocess.run(full, cwd=cwd, capture_output=True,
                              text=True, encoding="utf-8", errors="replace")
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            return None, proc.stderr[-2000:]
        best = elapsed if best is None else min(best, elapsed)
    return best, None


def measure(db, source_file, obj, runs, extra=()):
    """Compile one file from the compile database, best of `runs`.

    The allow-list test and the command construction it guards live in this one
    frame, and the value tested is the value used: `source_file` is checked
    against the database's own file set, and only then is the command for that
    exact entry assembled and run. Splitting the two — checking in the caller,
    building here — is what S6350 keeps objecting to, and it is a fair objection:
    from any other frame you cannot tell what constrains the argv being executed.
    """
    if source_file not in {e["file"] for e in db}:
        return None, f"{source_file}: not in the compile database"

    entry = next(e for e in db if e["file"] == source_file)
    cmd, cwd = command_for(entry)
    return time_compile(cmd, cwd, source_file, obj, runs, extra)
