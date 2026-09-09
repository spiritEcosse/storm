#!/usr/bin/env python3
"""Measure what a Storm test TU pays before its first assertion.

Method (docs/internals/performance/COMPILE_TIME.md): replay a real TU's exact
command from compile_commands.json with the source swapped for a generated
probe, serially, min of N runs, ccache disabled, warm-up first.

Probes are generated from the tree's own shared/models/ headers, so they follow
the models rather than pinning a stale copy of them. Models are included in
dependency order and counted as models, not as files — see model_headers().

    scripts/dev-container.sh exec python3 scripts/compile_time_probe.py
    scripts/dev-container.sh exec python3 scripts/compile_time_probe.py --body real
"""
import argparse
import json
import os
import pathlib
import re
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


BODIES = {
    # Matches the table in COMPILE_TIME.md. An empty body understates costs that
    # come out of the gtest PCH lazily, so it is not the default.
    "empty": "int p() { return 0; }\n",
    "gtest": (
        "TEST(Probe, Body) {\n"
        '    std::vector<std::string> v{"a", "b"};\n'
        "    EXPECT_EQ(v.size(), 2U);\n"
        '    EXPECT_EQ(v[0], "a");\n'
        "}\n"
    ),
    # What a real test TU does: instantiates the query stack it is measuring.
    "real": (
        "TEST(Probe, Body) {\n"
        "    using Conn = storm::db::sqlite::Connection;\n"
        "    storm::QuerySet<Person, Conn> qs;\n"
        "    auto q = qs.where(fields::Person.age > 30)\n"
        "                 .order_by<fields::Person.name>().limit(10);\n"
        "    auto r = q.select();\n"
        "    (void)r;\n"
        "}\n"
    ),
}

PREAMBLE = "#include <gtest/gtest.h>\n#include \"test_db_helpers.h\"\nimport storm;\nimport std;\n"


def model_headers(models_dir: pathlib.Path):
    """Return shared/models/*.h in dependency order (dependencies first).

    Before #634 this parsed one shared/models.h and truncated it at a struct
    boundary; the models now live one per header, so the split is physical and
    the order is derived from what each header includes rather than from where
    it happened to sit in a single file. Ties break alphabetically, so the
    sequence is deterministic.

    Ordering matters because the probe measures CUMULATIVE prefixes: taking the
    first N headers must yield a self-consistent set, which topological order
    guarantees (message.h is never counted before the person.h it includes).
    """
    headers = {h.stem: h for h in sorted(models_dir.glob("*.h"))}
    if not headers:
        sys.exit(f"{models_dir}: no model headers found — has the layout changed again?")

    deps = {
        stem: {inc for inc in re.findall(r'#include "(\w+)\.h"', path.read_text())
               if inc in headers}
        for stem, path in headers.items()
    }

    ordered, remaining = [], dict(deps)
    while remaining:
        ready = sorted(k for k, d in remaining.items() if d <= set(ordered))
        if not ready:  # a cycle would loop forever; models.h forbids one
            sys.exit(f"cyclic includes among {sorted(remaining)}")
        ordered.extend(ready)
        for stem in ready:
            del remaining[stem]

    # Count MODELS, not headers. color.h defines an enum and carries no fields::
    # proxy, so counting it as "1 model" would make the first step — the one the
    # intercept finding rests on — measure nothing. It still reaches the probe
    # as a dependency of extended_types.h, just not as a unit of the count.
    return [headers[stem] for stem in ordered
            if re.search(r"^struct \w+ \{", headers[stem].read_text(), re.M)]


FIELDS_BLOCK = re.compile(r"\nnamespace fields \{.*?\} // namespace fields\n", re.S)


def write_probe_header(tmpdir: pathlib.Path, headers, count: int, selectors: bool) -> pathlib.Path:
    """Materialise a header including the first `count` model headers.

    The model headers are copied into tmpdir so their own relative includes
    still resolve. With selectors=False each copy has its `namespace fields`
    block stripped — that block ships inside the model header now, so isolating
    the cost of the struct from the cost of its proxy needs the copy edited
    rather than the block synthesized as it was pre-#634.
    """
    stage = tmpdir / f"models_{count}{'s' if selectors else ''}"
    stage.mkdir(exist_ok=True)

    # Stage the transitive closure, not just the counted headers: a counted
    # header's own #includes (person.h, color.h, ...) must resolve inside the
    # staging directory, whether or not those are themselves counted.
    pending, staged = list(headers[:count]), set()
    while pending:
        header = pending.pop()
        if header.name in staged:
            continue
        staged.add(header.name)
        text = header.read_text()
        if not selectors:
            text = FIELDS_BLOCK.sub("\n", text)
        (stage / header.name).write_text(text)
        for inc in re.findall(r'#include "(\w+\.h)"', text):
            sibling = header.parent / inc
            if sibling.exists():
                pending.append(sibling)

    dest = tmpdir / f"probe_models_{count}{'s' if selectors else ''}.h"
    dest.write_text("".join(f'#include "{stage / h.name}"\n' for h in headers[:count]))
    return dest


def base_command(build_dir: pathlib.Path, reference: str):
    db = json.loads(pathlib.Path(
        safe_path(build_dir / "compile_commands.json")).read_text())
    try:
        entry = next(e for e in db if e["file"].endswith(reference))
    except StopIteration:
        sys.exit(f"no compile_commands.json entry ends with {reference!r}")
    argv = shlex.split(entry["command"])
    if pathlib.Path(argv[0]).name in ("ccache", "sccache"):
        sys.exit(
            f"compiler launcher active ({argv[0]}). Timing through a cache is "
            "meaningless — run: cmake -U CMAKE_CXX_COMPILER_LAUNCHER ."
        )
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
    return kept, entry["directory"]


def time_compile(cmd, cwd, source, obj, runs):
    full = cmd + ["-c", str(source), "-o", str(obj)]
    best = None
    for _ in range(runs):
        start = time.perf_counter()
        proc = subprocess.run(full, cwd=cwd, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            return None, proc.stderr[-2000:]
        best = elapsed if best is None else min(best, elapsed)
    return best, None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build/debug",
                    help="configured build tree, relative to the repository root")
    ap.add_argument("--reference", default="tests/query/test_aggregate.cpp",
                    help="TU whose compile command is replayed")
    ap.add_argument("--runs", type=int, default=3, help="min of N runs (default 3)")
    ap.add_argument("--body", choices=sorted(BODIES), default="gtest",
                    help="probe body (default gtest; 'empty' understates PCH-lazy costs)")
    ap.add_argument("--counts", default="1,3,5,7,11",
                    help="model counts to measure (default 1,3,5,7,11)")
    args = ap.parse_args()

    root = pathlib.Path(REPO_ROOT)
    build_dir = root / args.build_dir
    if not os.path.isfile(safe_path(build_dir / "compile_commands.json")):
        sys.exit(f"{build_dir}/compile_commands.json not found — configure the build first")

    headers = model_headers(root / "shared" / "models")

    cmd, cwd = base_command(build_dir, args.reference)
    body = BODIES[args.body]

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = pathlib.Path(tmp)
        obj = tmpdir / "probe.o"

        def probe(name: str, include: str) -> pathlib.Path:
            src = tmpdir / f"probe_{name}.cpp"
            src.write_text(PREAMBLE + include + body)
            return src

        variants = [("baseline (no models)", probe("base", ""))]
        for count in [int(c) for c in args.counts.split(",") if c.strip()]:
            if count > len(headers):
                continue
            for selectors in (False, True):
                header = write_probe_header(tmpdir, headers, count, selectors)
                label = f"{count} model{'s' if count > 1 else ''}" + (
                    " + fields:: proxies" if selectors else " (structs only)")
                variants.append((label, probe(f"{count}{'s' if selectors else ''}",
                                              f'#include "{header}"\n')))
        variants.append(("full test_models.h",
                         probe("full", '#include "test_models.h"\n')))

        print(f"reference: {args.reference}   body: {args.body}   min of {args.runs}")
        print("warming up...", flush=True)
        time_compile(cmd, cwd, variants[0][1], obj, 1)

        baseline = None
        print(f"\n{'variant':34} {'sec':>7} {'delta':>8}")
        for label, source in variants:
            seconds, err = time_compile(cmd, cwd, source, obj, args.runs)
            if err:
                print(f"{label:34} FAILED\n{err}")
                continue
            if baseline is None:
                baseline = seconds
                print(f"{label:34} {seconds:7.2f} {'—':>8}")
            else:
                print(f"{label:34} {seconds:7.2f} {seconds - baseline:+8.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
