#!/bin/bash
# Pre-commit checks: format -> tidy --fix -> test -> sonar -> bench
# Runs automatically via git pre-commit hook, or manually: ./commit.sh
#
# Options (flags or env vars):
#   --no-sonar / SKIP_SONAR=1   Skip local sonar check
#   --no-bench / SKIP_BENCH=1   Skip quick benchmark sanity check

set -e  # Exit on any error

# Parse flags
RUN_SONAR=true
RUN_BENCH=true

for arg in "$@"; do
    if [[ "$arg" == "--no-sonar" ]]; then
        RUN_SONAR=false
    elif [[ "$arg" == "--no-bench" ]]; then
        RUN_BENCH=false
    fi
done

# Support env vars (useful for: SKIP_BENCH=1 git commit -m "msg")
[[ "${SKIP_SONAR:-}" == "1" ]] && RUN_SONAR=false
[[ "${SKIP_BENCH:-}" == "1" ]] && RUN_BENCH=false

echo "📝 Running clang-format..."
find src tests benchmarks -type f \( -name "*.cpp" -o -name "*.cppm" -o -name "*.h" -o -name "*.hpp" \) -exec ../clang-p2996/build/bin/clang-format -i --style=file {} +

# Clang-tidy with auto-fix (always runs - must pass before commit)
echo ""
echo "🔍 Running clang-tidy --fix (auto-fixing issues)..."
./scripts/run_clang_tidy.sh --fix || {
    echo "❌ clang-tidy failed. Fix issues before committing."
    exit 1
}

# Re-stage files modified by format/tidy so changes are included in the commit
git add -u

echo ""
echo "🧪 Running unit tests..."
ctest --test-dir build/debug --output-on-failure

# Local sonar check (runs by default)
if [[ "$RUN_SONAR" == true ]]; then
    echo ""
    echo "🔍 Running local sonar check..."
    ./scripts/sonar-check.sh src tests benchmarks || {
        echo "❌ Sonar check failed. Fix issues or skip with: SKIP_SONAR=1 git commit -m \"msg\""
        exit 1
    }
fi

# Quick benchmark sanity check (runs by default)
if [[ "$RUN_BENCH" == true ]]; then
    BENCH_BIN="./build/release/benchmarks/storm_bench"
    if [[ -x "$BENCH_BIN" ]]; then
        echo ""
        echo "⚡ Running quick benchmark sanity check (100 iterations)..."
        for test in insert_batch_100 update_pk_batch_100 delete_pk_batch_100 select_join_1000; do
            result=$($BENCH_BIN --filter=$test --iterations=100 2>&1 | grep -E "Efficiency:|PASSED|FAILED" | head -1)
            printf "   %-25s %s\n" "$test:" "$result"
        done
        echo "✅ Benchmark sanity check complete"
    else
        echo "⚠️  Benchmark binary not found, skipping (build with -DENABLE_BENCH=ON)"
    fi
fi

echo ""
echo "✅ All checks passed!"
