#!/usr/bin/env python3
"""Measure what a Storm test TU pays before its first assertion.

Method (docs/internals/performance/COMPILE_TIME.md): replay a real TU's exact
command from compile_commands.json with the source swapped for a generated
probe, serially, min of N runs, ccache disabled, warm-up first.

Probes are generated from the tree's own shared/models.h, so they follow the
models rather than pinning a stale copy of them.

    scripts/dev-container.sh exec python3 scripts/compile_time_probe.py
    scripts/dev-container.sh exec python3 scripts/compile_time_probe.py --body real
"""
import argparse
import json
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
import time

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


def split_models(models_h: str):
    """Return (prologue, [(name, end_line_index)]) for shared/models.h."""
    lines = models_h.splitlines(keepends=True)
    first = next(i for i, l in enumerate(lines) if l.startswith("struct "))
    try:
        stop = next(i for i, l in enumerate(lines) if l.startswith("namespace fields"))
    except StopIteration:
        stop = len(lines)
    names, ends, i = [], [], first
    while i < stop:
        m = re.match(r"struct (\w+) \{", lines[i])
        if m:
            j = next(k for k in range(i, stop) if lines[k].startswith("};"))
            names.append(m.group(1))
            ends.append(j)
            i = j + 1
        else:
            i += 1
    return lines, names, ends


def selector_block(name: str) -> str:
    return (
        f"struct {name}T;\n"
        f"consteval {{ std::meta::define_aggregate(^^{name}T,"
        f" storm::field_specs_for(^^{name})); }}\n"
        f"inline constexpr {name}T {name}{{}};\n"
    )


def write_models_header(dest: pathlib.Path, lines, names, ends, count, selectors,
                        has_selector) -> None:
    """Prefix-truncate the real header at a struct boundary, keeping enums."""
    out = "".join(lines[: ends[count - 1] + 1])
    if selectors:
        blocks = "".join(selector_block(n) for n in names[:count] if n in has_selector)
        out += "\nnamespace fields {\n" + blocks + "} // namespace fields\n"
    dest.write_text(out)


def base_command(build_dir: pathlib.Path, reference: str):
    db = json.loads((build_dir / "compile_commands.json").read_text())
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
    ap.add_argument("--storm-root", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent,
                    help="storm checkout (default: the repo this script lives in)")
    ap.add_argument("--build-dir", default="build/debug",
                    help="configured build tree, relative to --storm-root")
    ap.add_argument("--reference", default="tests/query/test_aggregate.cpp",
                    help="TU whose compile command is replayed")
    ap.add_argument("--runs", type=int, default=3, help="min of N runs (default 3)")
    ap.add_argument("--body", choices=sorted(BODIES), default="gtest",
                    help="probe body (default gtest; 'empty' understates PCH-lazy costs)")
    ap.add_argument("--counts", default="1,3,5,7,11",
                    help="model counts to measure (default 1,3,5,7,11)")
    args = ap.parse_args()

    root = args.storm_root.resolve()
    build_dir = root / args.build_dir
    if not (build_dir / "compile_commands.json").exists():
        sys.exit(f"{build_dir}/compile_commands.json not found — configure the build first")

    models_h = (root / "shared" / "models.h").read_text()
    lines, names, ends = split_models(models_h)
    has_selector = {m.group(1) for m in re.finditer(r"inline constexpr (\w+)T \w+\{\};", models_h)}
    has_selector = {n[:-1] if n.endswith("T") else n for n in has_selector}

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
            if count > len(names):
                continue
            for selectors in (False, True):
                header = tmpdir / f"models_{count}{'s' if selectors else ''}.h"
                write_models_header(header, lines, names, ends, count, selectors, has_selector)
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
