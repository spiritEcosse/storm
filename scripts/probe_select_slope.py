#!/usr/bin/env python3
"""What does ONE extra (model, backend) SELECT query stack cost to compile?

Bounds any "hoist the T-independent methods out of SelectStatement<T, ConnType>"
refactor: the movable part is a fraction of a specialization, so the cost of a
whole specialization is the ceiling on what such a refactor can return.

Three variants are generated for each model count N, from N synthetic models of
identical shape:

  decl    N models + their fields:: proxies, nothing queried
  select  + the SELECT stack instantiated for each, ONE backend
  select2 + the same on BOTH backends

Every chain is terminated with .execute()/.to_sql() rather than left at the
proxy. A member of a class template is instantiated on odr-use, so a probe that
stops at `qs.select()` compiles the proxy's declarations and NONE of the bodies
the study is about — which biases the marginal cost downward, i.e. toward
"the refactor is not worth it". See COMPILE_TIME.md.

The reported number is the SLOPE across the requested counts (last minus first,
per model), not (variant - decl)/N: the query stack has a large intercept —
entering the machinery at all — and dividing an intercept-inclusive difference
by N yields a figure that falls with N and means nothing on its own.

ONE MEASUREMENT PER STATE IS NOT A RESULT. Comparing a run taken before a
library edit against one taken after it compares page-cache states as much as
code: a rebuilt BMI is cold, and that alone moved a TU by ~0.8 s here. To
compare two versions of the library, rebuild and re-measure each side several
times, alternating, and read the median of each side.

    scripts/dev-container.sh exec python3 scripts/probe_select_slope.py --counts 1,3,6
"""
import argparse
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "lib"))
from compile_replay import (command_for, load_compile_db, resolve_tu,  # noqa: E402
                            time_compile)

# <meta> stays textual — `import std;` does not export std::meta:: (COMPILER_ISSUES.md §9)
# — and must precede the imports, exactly as a model header spells it.
PREAMBLE = ("#include <gtest/gtest.h>\n"
            "#include <meta>\n"
            "import storm;\n"
            "import std;\n")

SQLITE = "storm::db::sqlite::Connection"
POSTGRESQL = "storm::db::postgresql::Connection"

# Ablated probes orphan nothing, but the results are deliberately unexamined.
PROBE_FLAGS = ("-Wno-unused-variable", "-Wno-unused-but-set-variable")


def model(i):
    """One model plus its fields:: proxy, spelled as shared/models/*.h spells it.

    Deliberately plain: no relation container, FK, optional or blob member. That
    makes the slope a FLOOR for a real model — SelectStatement's m2m half is
    `if constexpr`-gated on has_m2m_field_ || has_reverse_fk_field_, so this
    model never instantiates it (prepare_clause_sql, run_q1/run_q2_stitch, the
    stitch-key machinery). The error runs opposite to the .to_sql() one in
    query_block, which no real TU calls.
    """
    return f"""
struct M{i} {{
    [[= storm::primary]] int id{{}};
    std::string name;
    int age{{}};
    double salary{{}};
    bool is_active{{}};
}};
namespace fields {{
struct M{i}T;
consteval {{ std::meta::define_aggregate(^^M{i}T, storm::field_specs_for(^^M{i})); }}
inline constexpr M{i}T M{i}{{}};
}}
"""


def query_block(i, conn, tag):
    """The SELECT stack for one model, every chain terminated.

    .execute() is what odr-uses the proxy bodies — and through them build_sql,
    prepare_statement, the extraction loop and the consteval SQL builders.
    .to_sql() reaches the to_sql_* trio, which .execute() does not.
    """
    return f"""
    {{
        storm::QuerySet<M{i}, {conn}> qs{tag}{i};
        auto rows{tag}{i} = qs{tag}{i}.where(fields::M{i}.age > 30)
                                .order_by<fields::M{i}.name>().limit(10)
                                .select().execute();
        auto one{tag}{i}  = qs{tag}{i}.first().execute();
        auto got{tag}{i}  = qs{tag}{i}.get().execute();
        auto sql{tag}{i}  = qs{tag}{i}.select().to_sql();
        (void)rows{tag}{i}; (void)one{tag}{i}; (void)got{tag}{i}; (void)sql{tag}{i};
    }}"""


def source(n, variant):
    parts = [PREAMBLE] + [model(i) for i in range(n)] + ["\nTEST(Probe, Body) {"]
    if variant in ("select", "select2"):
        parts += [query_block(i, SQLITE, "a") for i in range(n)]
    if variant == "select2":
        parts += [query_block(i, POSTGRESQL, "b") for i in range(n)]
    parts.append("\n    EXPECT_TRUE(true);\n}\n")
    return "".join(parts)


VARIANTS = ("decl", "select", "select2")


def parse_counts(text):
    try:
        counts = sorted({int(c) for c in text.split(",")})
    except ValueError:
        sys.exit(f"--counts {text!r}: expected comma-separated integers")
    if len(counts) < 2:
        sys.exit("--counts needs at least two distinct values: one point cannot "
                 "separate the intercept from the slope")
    if counts[0] < 1:
        sys.exit("--counts values must be >= 1")
    return counts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", default="tests/crud/test_select.cpp",
                        help="TU whose compile command the probes are replayed with")
    parser.add_argument("--build-dir", default="build/debug")
    parser.add_argument("--counts", default="1,3,6")
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()

    counts = parse_counts(args.counts)
    entry = resolve_tu(load_compile_db(args.build_dir), args.reference)
    cmd, cwd = command_for(entry)

    print(f"reference: {entry['file']}")
    print(f"build dir: {args.build_dir}   compiler: {cmd[0]}")
    print(f"min of {args.runs} runs, seconds\n")

    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)

        # Warm up: the first compile in a session is inflated by cold page cache
        # for the module BMIs, which has manufactured a fake win here before.
        warm = tmp / "warm.cpp"
        warm.write_text(source(1, "decl"), encoding="utf-8")
        _, warm_err = time_compile(cmd, cwd, warm, tmp / "warm.o", 1, PROBE_FLAGS)
        if warm_err:
            sys.exit(f"the warm-up probe does not compile — the generated source and "
                     f"this tree disagree:\n{warm_err}")

        measured = {v: {} for v in VARIANTS}
        header = "  ".join(f"{v:>8}" for v in VARIANTS)
        print(f"{'N':>3}  {header}")
        for n in counts:
            for variant in VARIANTS:
                src = tmp / f"probe_{n}_{variant}.cpp"
                src.write_text(source(n, variant), encoding="utf-8")
                seconds, err = time_compile(cmd, cwd, src, tmp / f"probe_{n}_{variant}.o",
                                            args.runs, PROBE_FLAGS)
                if err:
                    sys.exit(f"{variant} at N={n} failed to compile:\n{err}")
                measured[variant][n] = seconds
            print(f"{n:3d}  " + "  ".join(f"{measured[v][n]:8.2f}" for v in VARIANTS))

    lo, hi = counts[0], counts[-1]
    print(f"\nslope over N={lo}..{hi}, seconds per additional model:")
    for variant in VARIANTS:
        slope = (measured[variant][hi] - measured[variant][lo]) / (hi - lo)
        print(f"  {variant:8} {slope:6.3f}")
    stack = ((measured["select"][hi] - measured["select"][lo])
             - (measured["decl"][hi] - measured["decl"][lo])) / (hi - lo)
    backend = ((measured["select2"][hi] - measured["select2"][lo])
               - (measured["select"][hi] - measured["select"][lo])) / (hi - lo)
    print(f"\n  one extra model's SELECT stack, first backend : {stack:6.3f}")
    print(f"  what the second backend adds per model        : {backend:6.3f}")
    print(f"  intercept at N={lo} (select - decl)            : "
          f"{measured['select'][lo] - measured['decl'][lo]:6.3f}")


if __name__ == "__main__":
    sys.exit(main())
