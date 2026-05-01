#!/usr/bin/env python3
"""Render a Markdown PR comment from compare.py's --dump_to_json output.

Companion to benchmarks/scripts/compare_against_baseline.sh and the bench.yml
regression gate (#241). Reads the diff JSON produced by Google Benchmark's
compare.py and emits a PR-comment-ready Markdown body on stdout.

The first line is always the marker comment `<!-- storm-bench-comment -->`,
which bench.yml greps for to decide between creating a new comment and
editing the existing one.

Usage:
    render_pr_comment.py <diff.json> [--threshold 0.05] [--alpha 0.05]
                                     [--baseline-run-id N] [--current-sha SHA]

Exit code: 0 on success (regardless of regression count). The gate decision
is owned by compare_against_baseline.sh; this script only renders.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass


MARKER = "<!-- storm-bench-comment -->"


@dataclass
class Row:
    name: str
    delta: float
    pvalue: float | None
    baseline: float
    current: float
    time_unit: str


def collect_rows(diff: list[dict]) -> list[Row]:
    rows: list[Row] = []
    for entry in diff:
        name = entry.get("name", "")
        if name == "OVERALL_GEOMEAN":
            continue
        if entry.get("aggregate_name", "") not in ("mean", "median"):
            continue
        measurements = entry.get("measurements", [])
        if not measurements:
            continue
        m = measurements[0]
        utest = entry.get("utest") or {}
        rows.append(
            Row(
                name=name,
                delta=float(m.get("time", 0.0)),
                pvalue=utest.get("time_pvalue"),
                baseline=float(m.get("real_time_other", 0.0)),
                current=float(m.get("real_time", 0.0)),
                time_unit=entry.get("time_unit", "ns"),
            )
        )
    return rows


def is_significant(pvalue: float | None, alpha: float) -> bool:
    if alpha <= 0 or pvalue is None:
        return True
    return pvalue < alpha


def fmt_delta(d: float) -> str:
    sign = "+" if d >= 0 else ""
    return f"{sign}{d * 100:.2f}%"


def fmt_p(p: float | None) -> str:
    return f"{p:.4f}" if p is not None else "—"


def render_table(rows: list[Row]) -> str:
    if not rows:
        return "_(none)_\n"
    lines = [
        "| Benchmark | Baseline | Current | Δ | p-value |",
        "|---|---:|---:|---:|---:|",
    ]
    for r in rows:
        lines.append(
            f"| `{r.name}` | {r.baseline:.1f} {r.time_unit} | "
            f"{r.current:.1f} {r.time_unit} | **{fmt_delta(r.delta)}** | {fmt_p(r.pvalue)} |"
        )
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("diff_json")
    ap.add_argument("--threshold", type=float, default=0.05)
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--baseline-run-id", default="")
    ap.add_argument("--current-sha", default="")
    args = ap.parse_args()

    with open(args.diff_json) as f:
        diff = json.load(f)

    rows = collect_rows(diff)
    regressions = [r for r in rows if r.delta >= args.threshold and is_significant(r.pvalue, args.alpha)]
    improvements = [r for r in rows if r.delta <= -args.threshold and is_significant(r.pvalue, args.alpha)]
    unchanged = [r for r in rows if r not in regressions and r not in improvements]

    regressions.sort(key=lambda r: -r.delta)
    improvements.sort(key=lambda r: r.delta)

    threshold_pct = args.threshold * 100.0
    alpha = args.alpha
    n_total = len(rows)
    n_reg = len(regressions)
    n_imp = len(improvements)
    n_eq = len(unchanged)

    def plur(n: int, word: str) -> str:
        return f"{n} {word}{'' if n == 1 else 's'}"

    if n_reg:
        verdict = f":x: **{plur(n_reg, 'regression')} past +{threshold_pct:.1f}% (p<{alpha})**"
    elif n_imp:
        verdict = f":white_check_mark: No regressions. {plur(n_imp, 'improvement')} past −{threshold_pct:.1f}%."
    else:
        verdict = ":white_check_mark: No significant change."

    out = [MARKER, "## storm_bench regression gate", "", verdict, ""]

    out.append(
        f"_{n_total} benchmark(s) compared (mean+median aggregates) — "
        f"{n_reg} regressed, {n_imp} improved, {n_eq} within ±{threshold_pct:.1f}%."
        f" Threshold +{threshold_pct:.1f}%, Mann-Whitney U-test α={alpha}._"
    )
    out.append("")

    if regressions:
        out += ["### Regressions", "", render_table(regressions)]

    if improvements:
        out += ["### Improvements", "", render_table(improvements)]

    if args.baseline_run_id or args.current_sha:
        out.append("---")
        if args.baseline_run_id:
            out.append(
                f"_Baseline: develop run [{args.baseline_run_id}]"
                f"(https://github.com/spiritEcosse/storm/actions/runs/{args.baseline_run_id})_"
            )
        if args.current_sha:
            out.append(f"_Current: `{args.current_sha[:12]}`_")

    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
