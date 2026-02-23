#!/bin/bash
# Pre-commit checks: format -> tidy --fix -> test -> coverage -> sonar-scanner (SonarCloud)
# Runs automatically via git pre-commit hook, or manually: ./commit.sh
#
# All checks are mandatory. No skip flags available.
#
# Smart skips (automatic, based on staged files):
#   No C++ or cmake files         → skip format, tidy, tests, coverage, sonar
#   cmake-only changes            → skip clang-format, clang-tidy, sonar; run tests + coverage + cmake-format
#   C++ but no src/tests/cmake    → skip tests, coverage
#
# SonarCloud upload requires SONAR_TOKEN env var. Skipped gracefully if unset.
# Add to your shell profile: export SONAR_TOKEN=your_token

set -e  # Exit on any error

# ── Tool installer helpers ────────────────────────────────────────────────────

_ensure_build_wrapper() {
    if ! command -v build-wrapper-linux-x86-64 &>/dev/null; then
        echo "📦 Installing build-wrapper..."
        curl --create-dirs -sSLo /tmp/bw.zip \
            "https://sonarcloud.io/static/cpp/build-wrapper-linux-x86.zip"
        unzip -o /tmp/bw.zip -d "$HOME/.sonar/"
        rm /tmp/bw.zip
        export PATH="$HOME/.sonar/build-wrapper-linux-x86:$PATH"
    fi
}

_ensure_sonar_scanner() {
    local version="8.0.1.6346"
    local bin="$HOME/.sonar/sonar-scanner-${version}-linux-x64/bin"
    if ! command -v sonar-scanner &>/dev/null && [[ ! -f "$bin/sonar-scanner" ]]; then
        echo "📦 Installing sonar-scanner ${version}..."
        curl --create-dirs -sSLo /tmp/sonar-scanner.zip \
            "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${version}-linux-x64.zip"
        unzip -o /tmp/sonar-scanner.zip -d "$HOME/.sonar/"
        rm /tmp/sonar-scanner.zip
    fi
    export PATH="$bin:$PATH"
}

# ─────────────────────────────────────────────────────────────────────────────

RUN_FORMAT=true
RUN_CMAKE_FORMAT=true
RUN_TIDY=true
RUN_TESTS=true
RUN_COVERAGE=true
RUN_SONAR=true

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
        echo "ℹ️  No C++ or cmake files in commit — skipping format, tidy, tests, coverage, sonar"
        RUN_FORMAT=false RUN_CMAKE_FORMAT=false RUN_TIDY=false
        RUN_TESTS=false RUN_COVERAGE=false RUN_SONAR=false
    elif [[ "$HAS_CPP_CHANGES" == false ]]; then
        # cmake-only changes: tests + coverage + cmake-format must still run
        # (cmake changes can break test builds or alter coverage instrumentation)
        echo "ℹ️  cmake-only changes — skipping clang-format, clang-tidy, sonar"
        RUN_FORMAT=false RUN_TIDY=false RUN_SONAR=false
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

    # When sonar will run: wrap coverage build with build-wrapper so it captures
    # the full compilation database (coverage rebuild compiles everything).
    if [[ "$RUN_SONAR" == true ]]; then
        _ensure_build_wrapper
        COVERAGE_OUTPUT=$(build-wrapper-linux-x86-64 --out-dir bw-output \
            cmake --build --preset ninja-debug-coverage --target coverage 2>&1) || {
            echo "$COVERAGE_OUTPUT"
            echo "❌ Coverage build/analysis failed. Fix issues before committing."
            exit 1
        }
    else
        COVERAGE_OUTPUT=$(cmake --build --preset ninja-debug-coverage --target coverage 2>&1) || {
            echo "$COVERAGE_OUTPUT"
            echo "❌ Coverage build/analysis failed. Fix issues before committing."
            exit 1
        }
    fi
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

# SonarCloud upload
if [[ "$RUN_SONAR" == true ]]; then
    # SonarCloud upload (requires SONAR_TOKEN; skipped gracefully if unset)
    if [[ -z "$SONAR_TOKEN" ]]; then
        echo "⚠️  SONAR_TOKEN not set — skipping SonarCloud upload"
        echo "   Add to your shell profile: export SONAR_TOKEN=your_token"
    else
        # If coverage was skipped (non-src C++ changes), we still need bw-output
        if [[ "$RUN_COVERAGE" == false ]]; then
            if [[ ! -f "build/debug/build.ninja" ]]; then
                cmake --preset ninja-debug
            fi
            _ensure_build_wrapper
            echo ""
            echo "🔨 Building with build-wrapper (coverage was skipped)..."
            build-wrapper-linux-x86-64 --out-dir bw-output \
                cmake --build --preset ninja-debug
        fi

        _ensure_sonar_scanner

        echo ""
        echo "☁️  Uploading analysis to SonarCloud..."

        # Determine coverage report path (may not exist if coverage was skipped)
        COV_ARGS=""
        if [[ -f "build/debug/coverage/coverage-filtered.lcov" ]]; then
            COV_ARGS="-Dsonar.cfamily.llvm-cov.reportPath=build/debug/coverage/coverage-filtered.lcov"
        fi

        sonar-scanner \
            -Dsonar.host.url=https://sonarcloud.io \
            -Dsonar.token="${SONAR_TOKEN}" \
            -Dsonar.cfamily.compile-commands=bw-output/compile_commands.json \
            $COV_ARGS || {
            echo "❌ SonarCloud upload failed."
            exit 1
        }

        echo "✅ SonarCloud analysis uploaded — results ready in ~2-3 min"
        echo "   Check: ./scripts/sonarcloud-check.sh --branch \$(git branch --show-current)"
    fi
fi

echo ""
echo "✅ All checks passed!"
