#!/bin/bash
# Run coverage tests in batches, one process per gtest suite, each producing
# a .profraw file that llvm-profdata merges later.
#
# Historical note: this script was originally a workaround for what looked like
# a clang coverage-instrumentation segfault / hang. Issue #269 traced the hang
# to a Storm bug — a use-after-free of a thread-local `Statement*` cache in
# `AggregateStatement::execute_simple` — which manifested as an infinite loop
# under coverage instrumentation. The fix landed; this batched layout is kept
# because per-suite isolation is still the right shape (and #176 / #268 still
# matter for cross-module template coverage).
#
# IMPORTANT: Suites must NOT be split into individual test runs.
# Per-test profraw files lose cross-module template coverage data because
# the profiling counters for template instantiations across module boundaries
# require enough test volume within a single process to be properly recorded.
# See: https://github.com/spiritEcosse/storm/issues/176
# See: https://github.com/spiritEcosse/storm/issues/269

set -e

BUILD_DIR="${1:-.}"
TEST_BIN="${BUILD_DIR}/tests/storm_tests"

if [[ ! -x "$TEST_BIN" ]]; then
    echo "ERROR: Test binary not found: $TEST_BIN" >&2
    exit 1
fi

# Clean old batch profraw files
rm -f "${BUILD_DIR}"/batch_*.profraw

# Discover all test suites
TEST_LIST=$("$TEST_BIN" --gtest_list_tests 2>/dev/null)

TOTAL=0
FAILED=0

# Run a single batch: args = filter_pattern safe_name
run_batch() {
    local filter="$1" name="$2"
    local profraw="${BUILD_DIR}/batch_${name}.profraw"
    LLVM_PROFILE_FILE="$profraw" \
        "$TEST_BIN" --gtest_filter="$filter" > /dev/null 2>&1 || {
            echo "  WARN: batch $name crashed (coverage data may be partial)"
            FAILED=$((FAILED + 1))
        }
    # Remove empty profraw files from crashed runs (llvm-profdata merge rejects them)
    [[ -f "$profraw" && ! -s "$profraw" ]] && rm -f "$profraw"
    TOTAL=$((TOTAL + 1))
    return 0
}

# Extract unique suite names and run each as one batch
while IFS= read -r line; do
    if [[ "$line" =~ ^[A-Za-z] ]]; then
        suite="${line%% *}"  # strip "# TypeParam = ..." comment
        # safe filename: replace / with _
        safe="${suite//\//_}"
        safe="${safe%.}"
        run_batch "${suite}*" "$safe"
    fi
done <<< "$TEST_LIST"

echo "  Coverage batches: $((TOTAL - FAILED))/$TOTAL succeeded"
if [[ $FAILED -gt 0 ]]; then
    echo "  ($FAILED batches crashed — partial coverage collected)"
fi
