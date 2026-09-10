#!/usr/bin/env python3
"""Decompose ONE test TU's compile time into the three things a test file is made of.

  intercept  what the TU pays before its first test body — includes, the gtest
             PCH, `import storm;`, the models and their `fields::` proxies
  fixture    what gtest's typed-test registration and StormTestFixture's table
             machinery cost, once per backend
  content    what the assertions and the distinct query shapes in the bodies cost

It answers "is there anything in this file worth optimizing", which a whole-build
breakdown (scripts/ninjalog_stats.py) cannot. Variants:

  full            the file as it is
  n=<k>           only the first k test blocks
  trivial1        only the first block, body `EXPECT_TRUE(true)` — the fixture is
                  still instantiated for both backends, no QuerySet is
  trivial1-1model trivial1 with the fixture narrowed to its first model, so the
                  marginal cost of an extra fixture model is visible
  merge           every body concatenated into ONE block, each in its own scope:
                  same assertions, same query shapes, one block instead of N
  sink            every EXPECT_*/ASSERT_* replaced by a variadic no-op that still
                  instantiates its arguments — isolates gtest's comparison and
                  printing machinery from storm's
  merge+sink      both

Every variant, `full` included, is compiled with the unused-entity warnings
suppressed. Ablation deletes bodies but keeps the file-scope helpers only those
bodies used, and the tree builds with -Werror, so without this the ablated rows
would not compile at all. It applies to `full` too, so all rows share one set of
flags and stay comparable — at the cost of `full` no longer being byte-identical
to what the real build does.

The file under test is edited in place and restored in a finally block. Run it on
a clean tree, and check `git diff` if it is interrupted.

    scripts/dev-container.sh exec python3 scripts/tu_ablation.py \
        tests/crud/test_conditional_update.cpp --variants full,merge,sink,n=0

ONE MEASUREMENT PER STATE IS NOT A RESULT. Comparing a run taken before a library
edit against one taken after it compares page-cache states as much as code: a
rebuilt BMI is cold, and that alone moved a TU by ~0.8 s here — enough to invent a
7% win that three interleaved rounds then erased. To compare two versions of the
library, rebuild and re-measure each side several times, alternating, and read the
median of each side. See docs/internals/performance/COMPILE_TIME.md.
"""
import argparse
import os
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "lib"))
from compile_replay import REPO_ROOT, load_compile_db, measure, safe_path  # noqa: E402

BLOCK_RE = re.compile(r'^(TYPED_TEST|TEST_F|TEST)\s*\(([^)]*)\)', re.M)
ASSERT_RE = re.compile(r'\b(EXPECT|ASSERT)_[A-Z_]+\s*\(')
FIXTURE_RE = re.compile(r'StormTestFixture<\s*(\w+)\s*,\s*(\w+)\s*(?:,[^<>]*)?>')

# Ablation leaves helpers that only the deleted bodies used. tests/ builds with
# -Werror (tests/CMakeLists.txt), so these would be errors rather than warnings.
ABLATION_FLAGS = (
    "-Wno-unused-function",
    "-Wno-unused-variable",
    "-Wno-unused-const-variable",
    "-Wno-unused-but-set-variable",
    "-Wno-unused-lambda-capture",
)

SINK = ("template <typename... StormProbeTs>\n"
        "static auto storm_probe_sink(StormProbeTs&&...) -> void {}\n")


def mask_literals(text):
    """Blank out comments and literals so brace scanning cannot be misled.

    Replaces every comment, string, raw string and character literal with spaces
    of the same length, keeping all offsets. Without this an apostrophe inside a
    prose comment ("don't") opens a phantom character literal and swallows the
    rest of the file — which is how this scanner first failed, silently skipping
    two of the TUs it was asked to measure. Every remaining way to run off the
    end raises instead: a variant built from a mis-parse would be measured and
    published, so a mis-parse must never be silent.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c, nxt = text[i], text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                # a backslash-continued // comment runs onto the next line
                if text[i] == "\\" and text[i + 1:i + 2] == "\n":
                    i += 1
                i += 1
        elif c == "/" and nxt == "*":
            end = text.find("*/", i + 2)
            if end < 0:
                raise ValueError(f"unterminated /* comment at offset {i}")
            for k in range(i, end + 2):
                out[k] = " " if text[k] != "\n" else "\n"
            i = end + 2
        elif c == "R" and nxt == '"' and not (i and (text[i - 1].isalnum() or text[i - 1] == "_")):
            close = text.find("(", i)
            if close < 0:
                raise ValueError(f"unterminated raw string at offset {i}")
            end = text.find(")" + text[i + 2:close] + '"', close)
            if end < 0:
                raise ValueError(f"unterminated raw string at offset {i}")
            end += close - i + 1
            for k in range(i, end):
                out[k] = " " if text[k] != "\n" else "\n"
            i = end
        elif c == '"' or (c == "'" and not (i and (text[i - 1].isalnum() or text[i - 1] == "_"))):
            out[i] = " "
            i += 1
            while i < n and text[i] != c:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                out[i] = " " if text[i] != "\n" else "\n"
                i += 1
            if i >= n:
                raise ValueError(f"unterminated {c} literal")
            out[i] = " "
            i += 1
        else:
            i += 1
    return "".join(out)


def scan_balanced(masked, i, open_ch="{", close_ch="}"):
    """Index of the delimiter closing the one at masked[i]."""
    depth = 0
    while i < len(masked):
        if masked[i] == open_ch:
            depth += 1
        elif masked[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unbalanced delimiters — check mask_literals against this file")


def blocks(text):
    """(macro, suite, start, body_open, body_close, end) for each test block."""
    masked = mask_literals(text)
    out = []
    for m in BLOCK_RE.finditer(masked):
        i = masked.find("{", m.end())
        if i < 0:
            continue
        j = scan_balanced(masked, i)
        end = masked.find("\n", j)
        out.append((m.group(1), m.group(2).split(",")[0].strip(),
                    m.start(), i, j, end + 1 if end > 0 else len(text)))
    return out


def keep_first(text, n):
    """Delete every test block except the first n."""
    out, last = [], 0
    for k, b in enumerate(blocks(text)):
        if k < n:
            continue
        out.append(text[last:b[2]])
        last = b[5]
    out.append(text[last:])
    return "".join(out)


def merge_blocks(text):
    """Concatenate every body into one block, each wrapped in its own scope.

    Refuses a file whose blocks do not all share one macro and one suite. Merging
    a plain TEST into a TYPED_TEST compiles — its body never names TypeParam —
    but silently instantiates it once per backend instead of once, so the variant
    would compile MORE than `full` while being reported as compiling less.
    """
    bs = blocks(text)
    macros, suites = {b[0] for b in bs}, {b[1] for b in bs}
    if len(macros) > 1 or len(suites) > 1:
        shown = sorted(suites)[:4]
        more = f" (+{len(suites) - len(shown)} more)" if len(suites) > len(shown) else ""
        sys.exit(f"cannot merge: this file has macros {sorted(macros)} and "
                 f"{len(suites)} suite(s) {shown}{more} — merging across them changes "
                 f"what is instantiated, so the row would not be comparable")
    bodies = "".join(f"\n    {{ // merged: block {k}\n{text[b[3] + 1:b[4]]}\n    }}\n"
                     for k, b in enumerate(bs))
    merged = f"{bs[0][0]}({bs[0][1]}, StormMergedProbe) {{{bodies}}}\n"
    out, last = [], 0
    for k, b in enumerate(bs):
        out.append(text[last:b[2]])
        if k == 0:
            out.append(merged)
        last = b[5]
    out.append(text[last:])
    return "".join(out)


def sink_asserts(text):
    """Replace every assertion macro with a no-op sink over the same arguments.

    The argument expressions are still parsed and instantiated; only gtest's
    comparison and printing templates go away. A streamed `<< "message"` tail is
    dropped with the macro, since the sink returns void — which is why the
    terminating `;` is located in the MASKED copy: `EXPECT_TRUE(x) << "...;\\n"`
    puts a semicolon inside the literal, and ending the span there would emit an
    unterminated string.
    """
    masked = mask_literals(text)
    edits = []
    for m in ASSERT_RE.finditer(masked):
        open_paren = m.end() - 1
        close = scan_balanced(masked, open_paren, "(", ")")
        semi = masked.find(";", close)
        if semi < 0:
            raise ValueError(f"assertion at offset {m.start()} has no terminating ';'")
        edits.append((m.start(), semi + 1,
                      "storm_probe_sink" + text[open_paren:close + 1] + ";"))
    result = text
    for start, end, repl in reversed(edits):
        result = result[:start] + repl + result[end:]
    anchor = result.find("\nimport std;")
    cut = result.find("\n", anchor + 1) + 1 if anchor > 0 else 0
    return result[:cut] + "\n" + SINK + result[cut:]


def trivial_first(text, one_model=False):
    """Keep only the first test block, with a body that touches nothing.

    Isolates what the FIXTURE costs (gtest's typed-test registration plus
    StormTestFixture's table machinery) from what the query bodies cost: the
    fixture is still instantiated for both backends, no QuerySet is.
    """
    bs = blocks(text)
    out, last = [], 0
    for k, b in enumerate(bs):
        out.append(text[last:b[2]])
        if k == 0:
            out.append(text[b[2]:b[3] + 1] + "\n    EXPECT_TRUE(true);\n}\n")
        last = b[5]
    out.append(text[last:])
    result = "".join(out)
    if one_model:
        result, count = FIXTURE_RE.subn(r"StormTestFixture<\1, \2>", result)
        if count == 0:
            sys.exit("no StormTestFixture<Model, Conn, ...> declaration to narrow — "
                     "this variant would silently measure the same thing as trivial1")
    return result


def build_variant(text, name, total):
    if name == "full":
        return text
    if name == "merge":
        return merge_blocks(text)
    if name == "sink":
        return sink_asserts(text)
    if name == "merge+sink":
        return sink_asserts(merge_blocks(text))
    if name == "trivial1":
        return trivial_first(text)
    if name == "trivial1-1model":
        return trivial_first(text, one_model=True)
    if name.startswith("n="):
        if not name[2:].isdigit() or int(name[2:]) > total:
            sys.exit(f"{name}: expected n=0..{total} for this file")
        return keep_first(text, int(name[2:]))
    sys.exit(f"unknown variant {name!r}")


def resolve_tu(db, wanted):
    """The one TU in the compile database whose path ends with `wanted`.

    Sorted and ambiguity-checked because the match names the file this script
    OVERWRITES: picking a different one on a different run, or the first of
    several, is not a risk worth taking for a convenience suffix.
    """
    matches = sorted(e["file"] for e in db if e["file"].endswith(wanted))
    if not matches:
        sys.exit(f"{wanted}: not in the compile database")
    if len(matches) > 1:
        sys.exit(f"{wanted} is ambiguous: {matches}")
    resolved = safe_path(matches[0])
    tests_dir = os.path.join(REPO_ROOT, "tests") + os.sep
    if not resolved.startswith(tests_dir):
        sys.exit(f"{resolved}: refusing to rewrite anything outside tests/")
    return resolved


def report(rows, names):
    base = dict(rows).get("full")
    if base is None:
        print("\nno 'full' variant measured — nothing to compare the others against",
              flush=True)
        return
    print(f"\n{'variant':16} {'sec':>7} {'delta':>8} {'%':>7}")
    for name in names:
        sec = dict(rows).get(name)
        if sec is not None:
            print(f"{name:16} {sec:7.2f} {sec - base:+8.2f} {100 * (sec - base) / base:+6.1f}%")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tu", help="test TU to decompose, by path suffix (must be under tests/)")
    ap.add_argument("--build-dir", default="build/debug",
                    help="configured build tree, relative to the repository root")
    ap.add_argument("--runs", type=int, default=3,
                    help="compiles per variant; the best is reported (default 3)")
    ap.add_argument("--variants", default="full,merge,sink,n=0",
                    help="comma-separated, in report order (default: %(default)s)")
    args = ap.parse_args()

    db = load_compile_db(args.build_dir)
    match = resolve_tu(db, args.tu)
    path = pathlib.Path(match)
    original = path.read_text(encoding="utf-8")
    total = len(blocks(original))
    if total == 0:
        sys.exit(f"{match}: no test blocks found — if its bodies live in an included "
                 f"header, every variant would compile the same file")
    names = [v.strip() for v in args.variants.split(",") if v.strip()]
    print(f"{match}\n{total} test blocks\n", flush=True)

    rows = []
    try:
        with tempfile.TemporaryDirectory() as tmp:
            obj = str(pathlib.Path(tmp) / "probe.o")
            for name in names:
                path.write_text(build_variant(original, name, total), encoding="utf-8")
                best, err = measure(db, match, obj, args.runs, ABLATION_FLAGS)
                if err:
                    print(f"{name:16} FAILED\n{err}\n", flush=True)
                    continue
                rows.append((name, best))
                print(f"{name:16} {best:7.2f}s", flush=True)
    finally:
        path.write_text(original, encoding="utf-8")
        print("\nrestored source", flush=True)

    report(rows, names)
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
