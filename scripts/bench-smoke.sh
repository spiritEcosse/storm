#!/bin/bash
# Smoke benchmark: run benchmarks with --smoke and enforce efficiency thresholds.
# Usage: ./scripts/bench-smoke.sh [path/to/storm_bench]
#
# Two-tier detection:
#   Tier 1 — Median efficiency must be >= 93% (real regressions drag most tests down)
#   Tier 2 — No single test may drop below 50% (catches catastrophic breakage)
#
# Exits 0 if both tiers pass, exits 1 otherwise.

set -euo pipefail

BENCH="${1:-./build/release/benchmarks/storm_bench}"

if [[ ! -x "$BENCH" ]]; then
    echo "Error: benchmark binary not found at $BENCH" >&2
    echo "Build with: cmake --preset ninja-release && cmake --build --preset ninja-release" >&2
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
echo "=== Benchmark regression detection ==="

# Thresholds
MEDIAN_THRESHOLD=93.0
FLOOR_THRESHOLD=60.0

# Collect valid efficiency values (exclude NaN/inf and >200%)
mapfile -t values < <(
    while IFS= read -r line; do
        eff="${line##* }"
        case "$eff" in *nan*|*inf*) continue ;; esac
        if awk "BEGIN { if ($eff > 200) exit 0; exit 1 }"; then
            continue
        fi
        echo "$eff"
    done < bench_results.txt
)

count=${#values[@]}
if [[ "$count" -eq 0 ]]; then
    echo "ERROR: no valid benchmark results found"
    rm -f bench_output.txt bench_results.txt
    exit 1
fi

# Sort values numerically
mapfile -t sorted < <(printf '%s\n' "${values[@]}" | sort -g)

# Compute median
mid=$((count / 2))
if (( count % 2 == 1 )); then
    median="${sorted[$mid]}"
else
    median=$(awk "BEGIN { printf \"%.2f\", (${sorted[$((mid-1))]} + ${sorted[$mid]}) / 2 }")
fi

# Compute P10 (10th percentile)
p10_idx=$(awk "BEGIN { idx = int($count * 0.1); if (idx < 0) idx = 0; print idx }")
p10="${sorted[$p10_idx]}"

# Find minimum and count below 90%
minimum="${sorted[0]}"
below_90=0
floor_failures=""
for val in "${sorted[@]}"; do
    if awk "BEGIN { if ($val < 90) exit 0; exit 1 }"; then
        below_90=$((below_90 + 1))
    fi
    if awk "BEGIN { if ($val < $FLOOR_THRESHOLD) exit 0; exit 1 }"; then
        floor_failures="yes"
    fi
done

# Find names of floor failures for reporting
floor_names=""
if [[ -n "$floor_failures" ]]; then
    while IFS= read -r line; do
        name="${line% *}"
        eff="${line##* }"
        case "$eff" in *nan*|*inf*) continue ;; esac
        if awk "BEGIN { if ($eff < $FLOOR_THRESHOLD) exit 0; exit 1 }"; then
            floor_names="${floor_names}  FLOOR FAIL: ${name} at ${eff}%"$'\n'
        fi
    done < bench_results.txt
fi

# Report
echo "Tests analyzed: $count"
echo "Median efficiency: ${median}%"
echo "P10 (10th percentile): ${p10}%"
echo "Minimum: ${minimum}%"
echo "Tests below 90%: $below_90"
echo ""

# Cleanup temp files
rm -f bench_output.txt bench_results.txt

# Tier 1 — Median check
failed=false
if awk "BEGIN { if ($median < $MEDIAN_THRESHOLD) exit 0; exit 1 }"; then
    echo "FAIL: Median efficiency ${median}% is below ${MEDIAN_THRESHOLD}% threshold"
    echo "This indicates a real regression affecting most benchmarks."
    failed=true
fi

# Tier 2 — Catastrophic floor check
if [[ -n "$floor_failures" ]]; then
    echo -n "$floor_names"
    echo "FAIL: One or more tests below ${FLOOR_THRESHOLD}% catastrophic floor"
    failed=true
fi

if [[ "$failed" == true ]]; then
    exit 1
fi
echo "All benchmarks passed (median ${median}% >= ${MEDIAN_THRESHOLD}%, floor >= ${FLOOR_THRESHOLD}%)"
