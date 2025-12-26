#!/bin/bash
# Quick commit workflow: format -> tidy --fix -> test -> bench -> commit -> push
# Usage: ./quick_commit.sh [--no-push] [--no-tidy] [--no-bench] [commit message]
#
# Options:
#   --no-push   Skip pushing to remote after commit
#   --no-tidy   Skip clang-tidy auto-fix (runs by default)
#   --no-bench  Skip quick benchmark sanity check (runs by default)

set -e  # Exit on any error

# Parse flags
NO_PUSH=false
RUN_TIDY=true
RUN_BENCH=true
COMMIT_MSG_ARG=""

for arg in "$@"; do
    if [[ "$arg" == "--no-push" ]]; then
        NO_PUSH=true
    elif [[ "$arg" == "--no-tidy" ]]; then
        RUN_TIDY=false
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

echo "📊 Changed files:"
git status --short

echo ""

# Auto-generate commit message summary
auto_summary() {
    local new_files=$(git status --porcelain | grep "^??" | wc -l)
    local modified_files=$(git status --porcelain | grep "^ M" | wc -l)
    local added_files=$(git status --porcelain | grep "^A " | wc -l)
    local deleted_files=$(git status --porcelain | grep "^D " | wc -l)

    # Detect what changed
    local has_formatting=false
    local has_docs=false
    local has_tests=false
    local has_benchmarks=false
    local has_src=false

    if git diff --stat | grep -q "\.cppm\|\.cpp\|\.hpp\|\.h"; then
        has_formatting=true
    fi
    if git diff --stat | grep -q "CLAUDE.md\|README\|docs/"; then
        has_docs=true
    fi
    if git diff --stat | grep -q "tests/"; then
        has_tests=true
    fi
    if git diff --stat | grep -q "benchmarks/"; then
        has_benchmarks=true
    fi
    if git diff --stat | grep -q "src/"; then
        has_src=true
    fi

    # Build summary
    local summary="chore: "
    local details=()

    # Check for new scripts
    if git status --porcelain | grep -q "quick_commit.sh"; then
        summary="chore: add quick_commit workflow script"
        details+=("automated format/test/commit/push workflow")
    fi

    if [[ $has_docs == true ]]; then
        details+=("update documentation")
    fi

    if [[ $has_formatting == true ]]; then
        details+=("run code formatting")
    fi

    # Combine
    if [[ ${#details[@]} -gt 0 ]]; then
        local detail_str=$(IFS=", "; echo "${details[*]}")
        summary="${summary} - ${detail_str}"
    fi

    echo "$summary"
}

# Check if commit message was provided as argument
if [[ -n "$COMMIT_MSG_ARG" ]]; then
    commit_msg="$COMMIT_MSG_ARG"
    echo "Using commit message: $commit_msg"
else
    # Generate auto-summary
    auto_msg=$(auto_summary)
    echo "📝 Auto-generated summary:"
    echo "   $auto_msg"
    echo ""

    # Use auto-generated message by default in non-interactive mode
    if [[ -t 0 ]]; then
        # Interactive mode
        read -p "Use this message? (Y/n) or enter your own: " user_input
        if [[ -z "$user_input" || "$user_input" =~ ^[Yy]$ ]]; then
            commit_msg="$auto_msg"
        elif [[ "$user_input" =~ ^[Nn]$ ]]; then
            read -p "Enter commit message: " commit_msg
            if [[ -z "$commit_msg" ]]; then
                echo "❌ Commit message cannot be empty"
                exit 1
            fi
        else
            # User entered a custom message directly
            commit_msg="$user_input"
        fi
    else
        # Non-interactive mode - use auto-generated message
        commit_msg="$auto_msg"
        echo "✓ Using auto-generated message (non-interactive mode)"
    fi
fi

echo ""
echo "📦 Committing changes..."
git add -A
git commit -m "$commit_msg

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
