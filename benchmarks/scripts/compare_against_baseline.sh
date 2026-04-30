#!/usr/bin/env bash
# Run storm_bench and (optionally) diff the result against a baseline JSON.
#
# This is the local-dev counterpart to the Bencher CI gate
# (.github/workflows/bench.yml, issue #238). Bencher's server-side store is
# the authoritative baseline for `develop`; this script is for offline
# investigation against any saved JSON snapshot (e.g. one downloaded from
# Bencher, or a /tmp/before.json captured before a local edit).
#
# Two modes:
#   1. No-arg / no baseline: just run the benchmarks, write current.json,
#      print Google Benchmark's own console summary. Exits 0.
#   2. With a baseline path (or BASELINE= env): diff against it via
#      compare.py + Mann-Whitney U-test. Exits 1 on any benchmark slower
#      than the threshold with statistical significance.
#
# Usage:
#   benchmarks/scripts/compare_against_baseline.sh                      # run-only
#   benchmarks/scripts/compare_against_baseline.sh path/to/baseline.json
#   BASELINE=path/to/baseline.json benchmarks/scripts/compare_against_baseline.sh
#
# Environment:
#   BUILD_DIR             default build/release
#   BASELINE              optional — path to a previous benchmark JSON to diff
#                         against. Omit (or pass empty) for run-only mode.
#   REGRESSION_THRESHOLD  default 1.05  (5% slowdown — anything above fails)
#                         Accepts ratios (1.05) or percent literals (5%).
#   UTEST_ALPHA           default 0.05  (Mann-Whitney U-test significance)
#                         Set to 0 to disable significance gating (any over-
#                         threshold delta fails regardless of repetitions).
#   BENCH_REPETITIONS     default 10
#   BENCH_FILTER          default "" (no filter — runs the full catalog)
#   BENCH_MIN_TIME        default ""  (let Google Benchmark pick)
#   CURRENT_JSON          default current.json (where the new run is written)
#   PYTHON                default python3 (override e.g. /path/to/venv/bin/python)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

BUILD_DIR="${BUILD_DIR:-build/release}"
BASELINE="${1:-${BASELINE:-}}"
REGRESSION_THRESHOLD="${REGRESSION_THRESHOLD:-1.05}"
UTEST_ALPHA="${UTEST_ALPHA:-0.05}"
BENCH_REPETITIONS="${BENCH_REPETITIONS:-10}"
BENCH_FILTER="${BENCH_FILTER:-}"
BENCH_MIN_TIME="${BENCH_MIN_TIME:-}"
CURRENT_JSON="${CURRENT_JSON:-current.json}"
PYTHON="${PYTHON:-python3}"

bench_bin="$BUILD_DIR/benchmarks/storm_bench"
if [[ ! -x "$bench_bin" ]]; then
    echo "ERROR: $bench_bin not found or not executable" >&2
    echo "Build first: cmake --preset ninja-release && cmake --build --preset ninja-release" >&2
    exit 1
fi

# Note: do NOT pass --benchmark_display_aggregates_only here. compare.py's
# Mann-Whitney U-test pulls every per-iteration row (`real_time`, `cpu_time`)
# matching the benchmark name; suppressing them collapses the U-test sample to
# the 4 aggregate rows (mean/median/stddev/cv) and the resulting p-value is
# meaningless.
bench_args=(
    --benchmark_repetitions="$BENCH_REPETITIONS"
    --benchmark_format=json
    --benchmark_out="$CURRENT_JSON"
)
if [[ -n "$BENCH_FILTER" ]]; then
    bench_args+=(--benchmark_filter="$BENCH_FILTER")
fi
if [[ -n "$BENCH_MIN_TIME" ]]; then
    bench_args+=(--benchmark_min_time="$BENCH_MIN_TIME")
fi

echo "Running storm_bench (reps=$BENCH_REPETITIONS, filter='${BENCH_FILTER:-<all>}') -> $CURRENT_JSON"
"$bench_bin" "${bench_args[@]}"

if [[ -z "$BASELINE" ]]; then
    echo
    echo "No baseline supplied — wrote $CURRENT_JSON. Pass a baseline path"
    echo "(or set BASELINE=...) to diff against a previous run."
    exit 0
fi

if [[ ! -f "$BASELINE" ]]; then
    echo "ERROR: baseline JSON not found: $BASELINE" >&2
    exit 1
fi

cache_file="$BUILD_DIR/CMakeCache.txt"
if [[ ! -f "$cache_file" ]]; then
    echo "ERROR: $cache_file not found — configure the build first" >&2
    exit 1
fi
benchmark_src="$(awk -F= '/^benchmark_SOURCE_DIR:STATIC=/ { print $2 }' "$cache_file")"
compare_py="$benchmark_src/tools/compare.py"
if [[ ! -f "$compare_py" ]]; then
    echo "ERROR: compare.py not found at $compare_py" >&2
    echo "Reconfigure to repopulate the CPM cache: cmake --preset ninja-release" >&2
    exit 1
fi

echo
echo "Comparing $CURRENT_JSON against $BASELINE (threshold=${REGRESSION_THRESHOLD}, alpha=${UTEST_ALPHA})"

report_json="$(mktemp -t storm-bench-compare.XXXXXX.json)"
trap 'rm -f "$report_json"' EXIT

"$PYTHON" "$compare_py" --display_aggregates_only \
    --dump_to_json "$report_json" \
    --no-color \
    benchmarks "$BASELINE" "$CURRENT_JSON"

# compare.py always exits 0 even on regressions; we read its diff JSON and
# decide. Each row exposes per-aggregate "measurements[*].time" deltas where
# time = (current - baseline) / |baseline|, so a +0.05 means a 5% slowdown.
"$PYTHON" - "$report_json" "$REGRESSION_THRESHOLD" "$UTEST_ALPHA" <<'PY'
import json
import sys

report_path, threshold_s, alpha_s = sys.argv[1], sys.argv[2], sys.argv[3]
threshold_s = threshold_s.strip()
if threshold_s.endswith("%"):
    threshold_pct = float(threshold_s.rstrip("%")) / 100.0
else:
    # Accept "1.05" as a ratio of new/old, "0.05" as a delta.
    raw = float(threshold_s)
    threshold_pct = raw - 1.0 if raw > 1.0 else raw
alpha = float(alpha_s)

with open(report_path) as f:
    diff = json.load(f)

regressions = []
for entry in diff:
    name = entry.get("name", "")
    if name == "OVERALL_GEOMEAN":
        continue
    agg = entry.get("aggregate_name", "")
    # Only judge mean/median aggregates; stddev/cv/iterations are not perf.
    if agg not in ("mean", "median"):
        continue
    measurements = entry.get("measurements", [])
    if not measurements:
        continue
    delta = float(measurements[0].get("time", 0.0))
    if delta < threshold_pct:
        continue
    utest = entry.get("utest") or {}
    pvalue = utest.get("time_pvalue")
    if alpha > 0 and pvalue is not None and pvalue >= alpha:
        # Past the threshold but not significant — skip.
        continue
    regressions.append((name, agg, delta, pvalue))

if regressions:
    pct = threshold_pct * 100.0
    print(f"\nFAIL: {len(regressions)} regression(s) past +{pct:.2f}%:")
    for name, agg, delta, pvalue in regressions:
        p_str = f", p={pvalue:.4f}" if pvalue is not None else ""
        print(f"  {name} [{agg}]: +{delta * 100:.2f}%{p_str}")
    sys.exit(1)

pct = threshold_pct * 100.0
print(f"\nOK: no benchmark slower than +{pct:.2f}%")
PY
