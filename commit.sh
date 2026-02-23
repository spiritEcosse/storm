#!/bin/bash
# Pre-commit checks: format -> tidy --fix -> test -> coverage
# Runs automatically via git pre-commit hook, or manually: ./commit.sh
#
# All checks are mandatory. No skip flags available.
#
# Smart skips (automatic, based on staged files):
#   No C++ or cmake files         → skip format, tidy, tests, coverage
#   cmake-only changes            → skip clang-format, clang-tidy; run tests + coverage + cmake-format
#   C++ but no src/tests/cmake    → skip tests, coverage
#
# SonarCloud quality gate runs on `git push` via .githooks/pre-push.

set -e  # Exit on any error

RUN_FORMAT=true
RUN_CMAKE_FORMAT=true
RUN_TIDY=true
RUN_TESTS=true
RUN_COVERAGE=true

# Smart skip: detect staged file changes to skip irrelevant checks
STAGED_FILES=$(git diff --cached --name-only 2>/dev/null || true)
if [[ -n "$STAGED_FILES" ]]; then
    HAS_SRC_CHANGES=false
    HAS_TEST_CHANGES=false
    HAS_CPP_CHANGES=false
    HAS_CMAKE_CHANGES=false

    while IFS= read -r file; do
        [[ "$file" == src/* ]] && HAS_SRC_CHANGES=true
        [[ "$file" == tests/* ]] && HAS_TEST_CHANGES=true
        [[ "$file" =~ \.(cpp|cppm|h|hpp)$ ]] && HAS_CPP_CHANGES=true
        [[ "$file" =~ (CMakeLists\.txt|\.cmake)$ ]] && HAS_CMAKE_CHANGES=true
    done <<< "$STAGED_FILES"

    if [[ "$HAS_CPP_CHANGES" == false && "$HAS_CMAKE_CHANGES" == false ]]; then
        echo "ℹ️  No C++ or cmake files in commit — skipping format, tidy, tests, coverage"
        RUN_FORMAT=false RUN_CMAKE_FORMAT=false RUN_TIDY=false
        RUN_TESTS=false RUN_COVERAGE=false
    elif [[ "$HAS_CPP_CHANGES" == false ]]; then
        # cmake-only changes: tests + coverage + cmake-format must still run
        echo "ℹ️  cmake-only changes — skipping clang-format, clang-tidy"
        RUN_FORMAT=false RUN_TIDY=false
    elif [[ "$HAS_SRC_CHANGES" == false && "$HAS_TEST_CHANGES" == false && "$HAS_CMAKE_CHANGES" == false ]]; then
        echo "ℹ️  No src/, tests/, or cmake changes — skipping tests, coverage"
        RUN_TESTS=false RUN_COVERAGE=false
    fi

    if [[ "$HAS_CMAKE_CHANGES" == false ]]; then
        RUN_CMAKE_FORMAT=false
    fi
fi

if [[ "$RUN_FORMAT" == true || "$RUN_CMAKE_FORMAT" == true ]]; then
    if [[ ! -f "build/debug/build.ninja" ]]; then
        echo "📐 Configuring debug build for format targets..."
        cmake --preset ninja-debug
    fi
fi

if [[ "$RUN_FORMAT" == true ]]; then
    echo "📝 Running clang-format..."
    cmake --build --preset ninja-debug --target format
fi

if [[ "$RUN_CMAKE_FORMAT" == true ]]; then
    echo "📝 Running cmake-format..."
    cmake --build --preset ninja-debug --target cmake-format
fi

if [[ "$RUN_TIDY" == true ]]; then
    echo ""
    echo "🔍 Running clang-tidy --fix on staged files..."
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
    echo "🧪 Running tests (SQLite + PostgreSQL)..."
    ctest --preset ninja-debug
fi

# 100% line coverage check
if [[ "$RUN_COVERAGE" == true ]]; then
    if [[ ! -f "build/debug/build.ninja" ]]; then
        echo ""
        echo "📊 Configuring debug build..."
        cmake --preset ninja-debug
    fi

    echo ""
    echo "📊 Running coverage analysis (required: 100% line coverage)..."
    COVERAGE_OUTPUT=$(cmake --build --preset ninja-debug-coverage --target coverage 2>&1) || {
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
        echo "   Run: cmake --build --preset ninja-debug --target coverage-html"
        echo "   Open: build/debug/coverage/html-filtered/index.html"
        exit 1
    fi

    echo "✅ Line coverage: 100.0%"
fi

echo ""
echo "✅ All checks passed!"
