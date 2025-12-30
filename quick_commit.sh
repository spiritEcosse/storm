#!/bin/bash
# Quick commit workflow: format -> tidy --fix -> test -> sonar -> bench -> commit -> push
# Usage: ./quick_commit.sh [--no-push] [--no-tidy] [--no-sonar] [--no-bench] [commit message]
#
# Options:
#   --no-push   Skip pushing to remote after commit
#   --no-tidy   Skip clang-tidy auto-fix (runs by default)
#   --no-sonar  Skip local sonar check (runs by default)
#   --no-bench  Skip quick benchmark sanity check (runs by default)

set -e  # Exit on any error

# Parse flags
NO_PUSH=false
RUN_TIDY=true
RUN_SONAR=true
RUN_BENCH=true
COMMIT_MSG_ARG=""

for arg in "$@"; do
    if [[ "$arg" == "--no-push" ]]; then
        NO_PUSH=true
    elif [[ "$arg" == "--no-tidy" ]]; then
        RUN_TIDY=false
    elif [[ "$arg" == "--no-sonar" ]]; then
        RUN_SONAR=false
    elif [[ "$arg" == "--no-bench" ]]; then
        RUN_BENCH=false
    else
        COMMIT_MSG_ARG="$COMMIT_MSG_ARG $arg"
    fi
done
COMMIT_MSG_ARG=$(echo "$COMMIT_MSG_ARG" | xargs)  # Trim whitespace

echo "📝 Running clang-format..."
find src tests benchmarks -type f \( -name "*.cpp" -o -name "*.cppm" -o -name "*.h" -o -name "*.hpp" \) -exec ../clang-p2996/build/bin/clang-format -i --style=file {} +

# Clang-tidy with auto-fix (runs by default)
if [[ "$RUN_TIDY" == true ]]; then
    echo ""
    echo "🔍 Running clang-tidy --fix (auto-fixing issues)..."
    ./scripts/run_clang_tidy.sh --fix || {
        echo "❌ clang-tidy failed. Fix issues or run with --no-tidy to skip"
        exit 1
    }
fi

echo ""
echo "🧪 Running unit tests..."
ctest --test-dir build/debug --output-on-failure

# Local sonar check (runs by default)
if [[ "$RUN_SONAR" == true ]]; then
    echo ""
    echo "🔍 Running local sonar check..."
    ./scripts/sonar-check.sh src tests benchmarks || {
        echo "❌ Sonar check failed. Fix issues or run with --no-sonar to skip"
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
echo ""

# Check if there are changes to commit
if [[ -z $(git status --porcelain) ]]; then
    echo "ℹ️  No changes to commit"
    exit 0
fi

# Require commit message
if [[ -z "$COMMIT_MSG_ARG" ]]; then
    echo "❌ Commit message required"
    echo "Usage: ./quick_commit.sh \"commit message\""
    exit 1
fi

echo "📦 Staging and committing..."
git add -A
git commit -m "$COMMIT_MSG_ARG

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>"

echo ""
if [[ "$NO_PUSH" == true ]]; then
    echo "ℹ️  Skipped push (--no-push flag used)"
    echo "   Run 'git push' manually when ready"
else
    echo "🚀 Pushing to remote..."
    git push
    echo "✅ Done!"
fi
