#!/bin/bash
# Pre-commit checks: format -> tidy --fix -> test -> coverage -> sonar -> bench
# Runs automatically via git pre-commit hook, or manually: ./commit.sh
#
# All checks are mandatory. No skip flags available.
#
# Smart skips (automatic, based on staged files):
#   No C++ files in commit        → skip format, tidy, tests, coverage, sonar, bench
#   No src/ or tests/ changes     → skip tests, coverage
#   No src/ changes               → skip bench

set -e  # Exit on any error

RUN_FORMAT=true
RUN_TIDY=true
RUN_TESTS=true
RUN_COVERAGE=true
RUN_SONAR=true
RUN_BENCH=true

# PG connection string for tests and coverage (always enabled)
PG_CONNSTR="host=host.containers.internal port=5432 dbname=storm_db user=storm_db password=storm_db"

# Smart skip: detect staged file changes to skip irrelevant checks
STAGED_FILES=$(git diff --cached --name-only 2>/dev/null || true)
if [[ -n "$STAGED_FILES" ]]; then
    HAS_SRC_CHANGES=false
    HAS_TEST_CHANGES=false
    HAS_CPP_CHANGES=false

    while IFS= read -r file; do
        [[ "$file" == src/* ]] && HAS_SRC_CHANGES=true
        [[ "$file" == tests/* ]] && HAS_TEST_CHANGES=true
        [[ "$file" =~ \.(cpp|cppm|h|hpp)$ ]] && HAS_CPP_CHANGES=true
    done <<< "$STAGED_FILES"

    if [[ "$HAS_CPP_CHANGES" == false ]]; then
        echo "ℹ️  No C++ files in commit — skipping format, tidy, tests, coverage, sonar, bench"
        RUN_FORMAT=false RUN_TIDY=false RUN_TESTS=false
        RUN_COVERAGE=false RUN_SONAR=false RUN_BENCH=false
    elif [[ "$HAS_SRC_CHANGES" == false && "$HAS_TEST_CHANGES" == false ]]; then
        echo "ℹ️  No src/ or tests/ changes — skipping tests, coverage"
        RUN_TESTS=false RUN_COVERAGE=false
    elif [[ "$HAS_SRC_CHANGES" == false ]]; then
        echo "ℹ️  No src/ changes — skipping bench"
        RUN_BENCH=false
    fi
fi

if [[ "$RUN_FORMAT" == true ]]; then
    echo "📝 Running clang-format..."
    find src tests benchmarks -type f \( -name "*.cpp" -o -name "*.cppm" -o -name "*.h" -o -name "*.hpp" \) -exec ../clang-p2996/build/bin/clang-format -i --style=file {} +
fi

if [[ "$RUN_TIDY" == true ]]; then
    echo ""
    echo "🔍 Running clang-tidy --fix (auto-fixing issues)..."
    ./scripts/run_clang_tidy.sh --fix || {
        echo "❌ clang-tidy failed. Fix issues before committing."
        exit 1
    }
fi

# Re-stage files modified by format/tidy so changes are included in the commit
if [[ "$RUN_FORMAT" == true || "$RUN_TIDY" == true ]]; then
    git add -u
fi

if [[ "$RUN_TESTS" == true ]]; then
    echo ""
    echo "🧪 Running unit tests..."
    ctest --test-dir build/debug --output-on-failure
    echo ""
    echo "🐘 Running PostgreSQL tests..."
    STORM_PG_CONNSTR="$PG_CONNSTR" \
        ctest --test-dir build/debug -j"$(nproc)" --output-on-failure
fi

# 100% line coverage check
if [[ "$RUN_COVERAGE" == true ]]; then
    COVERAGE_BUILD_DIR="build/coverage"

    # Configure coverage build if not already done (uses same Clang as CMakePresets)
    if [[ ! -f "$COVERAGE_BUILD_DIR/build.ninja" ]]; then
        echo ""
        echo "📊 Configuring coverage build..."
        SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        CLANG_ROOT="${SCRIPT_DIR}/../clang-p2996"
        PATH="${CLANG_ROOT}/build/bin:$PATH" cmake -S . -B "$COVERAGE_BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER="${CLANG_ROOT}/build/bin/clang++" \
            -DCMAKE_C_COMPILER="${CLANG_ROOT}/build/bin/clang" \
            -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="${CLANG_ROOT}/build/bin/clang-scan-deps" \
            -DLIBCXX_ROOT="${CLANG_ROOT}" \
            -DENABLE_TESTS=ON \
            -DENABLE_COVERAGE=ON
    fi

    echo ""
    echo "📊 Running coverage analysis (required: 100% line coverage)..."
    export STORM_PG_CONNSTR="$PG_CONNSTR"
    COVERAGE_OUTPUT=$(cmake --build "$COVERAGE_BUILD_DIR" --target coverage 2>&1) || {
        echo "$COVERAGE_OUTPUT"
        echo "❌ Coverage build/analysis failed. Fix issues before committing."
        exit 1
    }
    echo "$COVERAGE_OUTPUT"

    # Extract line coverage percentage from lcov summary (e.g. "lines.......: 100.0% (N of N lines)")
    LINE_COVERAGE=$(echo "$COVERAGE_OUTPUT" | grep "lines\.\.\.\.\.\.\." | tail -1 | grep -oP '[0-9.]+(?=%)')

    if [[ -z "$LINE_COVERAGE" ]]; then
        echo "❌ Could not parse line coverage from output. Aborting."
        exit 1
    fi

    if [[ "$LINE_COVERAGE" != "100.0" ]]; then
        echo "❌ Line coverage is ${LINE_COVERAGE}% (required: 100.0%). Fix coverage before committing."
        echo "   Run: cmake --build build/coverage --target coverage-html"
        echo "   Open: build/coverage/coverage/html-filtered/index.html"
        exit 1
    fi

    echo "✅ Line coverage: 100.0%"
fi

# Local sonar check
if [[ "$RUN_SONAR" == true ]]; then
    echo ""
    echo "🔍 Running local sonar check..."
    ./scripts/sonar-check.sh src tests benchmarks || {
        echo "❌ Sonar check failed. Fix issues before committing."
        exit 1
    }
fi

# Quick benchmark sanity check
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
