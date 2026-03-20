#!/bin/bash
# Smoke benchmark: run benchmarks with --smoke and enforce 95% efficiency threshold.
# Usage: ./scripts/bench-smoke.sh [path/to/storm_bench]
#
# Exits 0 if all non-skipped benchmarks >= 95%, exits 1 otherwise.

set -euo pipefail

BENCH="${1:-./build/release/benchmarks/storm_bench}"

if [[ ! -x "$BENCH" ]]; then
    echo "Error: benchmark binary not found at $BENCH"
    echo "Build with: cmake --preset ninja-release && cmake --build --preset ninja-release"
    exit 1
fi

# Run smoke benchmarks
"$BENCH" --smoke 2>&1 | tee bench_output.txt

# Strip ANSI codes, extract test names with efficiency
sed 's/\x1b\[[0-9;]*m//g' bench_output.txt \
    | awk '/^=== .* ===$/ { name=$0; gsub(/^=== | ===$/, "", name) }
           /^Efficiency:/ { split($2, a, "%"); printf "%s %s\n", name, a[1] }' \
    > bench_results.txt

echo ""
echo "=== Efficiency threshold check ==="

# Known-weak benchmarks excluded from threshold check
# (tracked in issues #157, #158 — fix perf, then remove from list)
SKIP="where_int_comparison_gt|update_pk_single|delete_pk_single"
SKIP="${SKIP}|distinct_join_100|distinct_where_join_100|first_100"
SKIP="${SKIP}|aggregate_count_10000"
SKIP="${SKIP}|select_multi_fk_join_100|select_multi_fk_join_10000"
SKIP="${SKIP}|setop_union_all_100|setop_union_all_10000"
SKIP="${SKIP}|first_where|select_where_limit_100|group_by_with_avg"
SKIP="${SKIP}|where_bool_equality|where_double_comparison"

threshold=95.0
failed=false
while IFS= read -r line; do
    name="${line% *}"
    eff="${line##* }"

    # Skip NaN/inf (near-zero timing on trivial ops)
    case "$eff" in *nan*|*inf*) continue ;; esac

    # Skip known-weak benchmarks
    if echo "$name" | grep -qE "^(${SKIP})$"; then
        continue
    fi

    # Skip extreme outliers (>200%) — trivial ops with near-zero raw time
    if awk "BEGIN { if ($eff > 200) exit 0; exit 1 }"; then
        continue
    fi

    if awk "BEGIN { if ($eff >= $threshold) exit 0; exit 1 }"; then
        :
    else
        echo "FAIL: ${name} at ${eff}% (below ${threshold}%)"
        failed=true
    fi
done < bench_results.txt

# Cleanup temp files
rm -f bench_output.txt bench_results.txt

if [[ "$failed" == true ]]; then
    echo "Benchmark regression detected — fix or add to skip list"
    exit 1
fi
echo "All benchmarks passed ${threshold}% threshold"
