#!/usr/bin/env bash
# Storm-vs-raw SQLite efficiency comparison (Issue #74).
#
# Streams a raw-SQLite baseline (storm_anchors) and then a Storm run
# (storm_bench) to the live dashboard. With the dashboard started under
# `--baseline raw:last`, each matched Storm row shows "NN.N% of raw" — the
# dashboard speaking the project's "96–108% of raw SQLite" language.
#
# The raw baseline is REUSED, not re-run per Storm session: raw:last resolves
# once at dashboard start to the most recent is_raw run. Refreshing it is a
# deliberate step — re-run storm_anchors, then restart the dashboard.
#
# Usage:
#   1. In terminal A, start the dashboard:
#        ./build/release/benchmarks/dashboard/storm_bench_dashboard --baseline raw:last
#      (First time there is no raw run yet → it reports "no baseline" — expected.
#       After step 2 below produces one, restart it to pick the raw run up.)
#   2. In terminal B, run this script: it streams the raw baseline then a Storm run.
#
# Environment:
#   BUILD_DIR     default build/release
#   BENCH_FILTER  default 'Storm/(WHERE|SELECT|INSERT)/.*' (the subset with raw counterparts)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

BUILD_DIR="${BUILD_DIR:-build/release}"
BENCH_FILTER="${BENCH_FILTER:-Storm/(WHERE|SELECT|INSERT)/.*}"

bench_dir="$BUILD_DIR/benchmarks"
dash="$bench_dir/dashboard/storm_bench_dashboard"
anchors="$bench_dir/storm_anchors"
bench="$bench_dir/storm_bench"

for bin in "$dash" "$anchors" "$bench"; do
    if [[ ! -x "$bin" ]]; then
        echo "ERROR: $bin not found or not executable" >&2
        echo "Build first: cmake --preset ninja-release && cmake --build --preset ninja-release" >&2
        exit 1
    fi
done

cat <<EOF
Storm-vs-raw comparison
-----------------------
Make sure the dashboard is already running in another terminal:

    $dash --baseline raw:last

Press Enter to: (1) stream storm_anchors as the raw baseline, then
(2) stream storm_bench. Matched subset rows will show 'NN.N% of raw'.
EOF
read -r _

echo "==> streaming raw baseline (storm_anchors)…"
STORM_BENCH_SOCKET=1 "$anchors"

echo "==> streaming Storm run (storm_bench, filter='$BENCH_FILTER')…"
STORM_BENCH_SOCKET=1 "$bench" --benchmark_filter="$BENCH_FILTER"

echo
echo "Done. If this produced the FIRST raw run, restart the dashboard with"
echo "--baseline raw:last so it resolves the new raw baseline, then re-run"
echo "the Storm step to see the 'NN.N% of raw' labels."
