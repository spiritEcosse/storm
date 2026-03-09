#!/bin/bash
# Run coverage tests in batches to work around Clang C++26 coverage instrumentation
# segfault that occurs when running too many tests in a single process.
#
# Auto-discovers test suites and runs each in a separate process.
# Large suites (>50 tests) are split into individual test runs.
# Each produces a .profraw file, later merged by llvm-profdata.

set -e

BUILD_DIR="${1:-.}"
TEST_BIN="${BUILD_DIR}/tests/storm_tests"
MAX_TESTS_PER_BATCH=50

if [[ ! -x "$TEST_BIN" ]]; then
    echo "ERROR: Test binary not found: $TEST_BIN" >&2
    exit 1
fi

# Clean old batch profraw files
rm -f "${BUILD_DIR}"/batch_*.profraw

# Discover all test suites and their tests
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

current_suite=""
test_names=()

flush_suite() {
    [[ -z "$current_suite" ]] && return
    local count=${#test_names[@]}
    # safe filename: replace / with _
    local safe="${current_suite//\//_}"
    safe="${safe%.}"

    if [[ $count -le $MAX_TESTS_PER_BATCH ]]; then
        # Run whole suite as one batch
        run_batch "${current_suite}*" "$safe"
    else
        # Split into individual tests
        for test_name in "${test_names[@]}"; do
            local full_name="${current_suite}${test_name}"
            local test_safe="${safe}_${test_name}"
            run_batch "$full_name" "$test_safe"
        done
    fi
    test_names=()
}

while IFS= read -r line; do
    if [[ "$line" =~ ^[A-Za-z] ]]; then
        # New suite header — flush previous suite
        flush_suite
        current_suite="${line%% *}"  # strip "# TypeParam = ..." comment
    elif [[ "$line" =~ ^[[:space:]] ]]; then
        # Test name within current suite
        test_names+=("${line## }")  # strip leading spaces
    fi
done <<< "$TEST_LIST"
# Flush last suite
flush_suite

echo "  Coverage batches: $((TOTAL - FAILED))/$TOTAL succeeded"
if [[ $FAILED -gt 0 ]]; then
    echo "  ($FAILED batches crashed — partial coverage collected)"
fi
