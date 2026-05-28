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

# --- Colors & formatting ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

# --- State tracking ---
declare -a STEP_NAMES=()
declare -a STEP_RESULTS=()
declare -a STEP_TIMES=()
declare -a STEP_LOGFILES=()
FAILED=false
TOTAL_START=$SECONDS

# Fixed log files — one per step, stable paths for easy inspection after failure
LOG_FORMAT=/tmp/storm_format.log
LOG_CMAKE_FORMAT=/tmp/storm_cmake_format.log
LOG_TIDY=/tmp/storm_tidy.log
LOG_TIDY_BMI=/tmp/storm_tidy_bmi.log
LOG_RELEASE=/tmp/storm_release.log
LOG_TESTS=/tmp/storm_tests.log
LOG_COVERAGE=/tmp/storm_coverage.log

print_header() {
    local step_num=$1 total=$2 name=$3
    echo ""
    echo -e "${BLUE}${BOLD}[$step_num/$total]${RESET} ${BOLD}$name${RESET}"
    echo -e "${DIM}$(printf '%.0s─' {1..60})${RESET}"
    return 0
}

_step_begin() {
    local name="$1"
    local step_num=${#STEP_NAMES[@]}
    step_num=$((step_num + 1))
    STEP_NAMES+=("$name")
    print_header "$step_num" "$TOTAL_STEPS" "$name"
}

_step_finish() {
    local name="$1" exit_code="$2" elapsed="$3" logfile="$4"
    STEP_TIMES+=("${elapsed}s")
    STEP_LOGFILES+=("$logfile")
    if [[ $exit_code -eq 0 ]]; then
        STEP_RESULTS+=("pass")
        echo -e "${GREEN}✓ $name passed${RESET} ${DIM}(${elapsed}s)${RESET}"
        return 0
    else
        STEP_RESULTS+=("FAIL")
        FAILED=true
        echo -e "${RED}✗ $name FAILED${RESET} ${DIM}(${elapsed}s)${RESET}"
        echo -e "${DIM}  → full output: $logfile${RESET}"
        return 1
    fi
}

run_step() {
    local name="$1" logfile="$2"
    shift 2
    _step_begin "$name"
    local start=$SECONDS
    "$@" > "$logfile" 2>&1
    local exit_code=$?
    _step_finish "$name" "$exit_code" "$(( SECONDS - start ))" "$logfile"
}

# Show full output live (for long-running steps like tests/coverage)
run_step_live() {
    local name="$1" logfile="$2"
    shift 2
    _step_begin "$name"
    local start=$SECONDS
    "$@" 2>&1 | tee "$logfile"
    local exit_code=${PIPESTATUS[0]}
    _step_finish "$name" "$exit_code" "$(( SECONDS - start ))" "$logfile"
}

print_summary() {
    local total_elapsed=$(( SECONDS - TOTAL_START ))
    echo ""
    echo -e "${BOLD}$(printf '%.0s═' {1..60})${RESET}"

    if [[ "$FAILED" == true ]]; then
        echo -e "${RED}${BOLD} COMMIT BLOCKED — pre-commit checks failed${RESET}"
    else
        echo -e "${GREEN}${BOLD} All checks passed!${RESET}"
    fi

    echo -e "${BOLD}$(printf '%.0s═' {1..60})${RESET}"
    echo ""

    # Summary table
    for i in "${!STEP_NAMES[@]}"; do
        local icon
        if [[ "${STEP_RESULTS[$i]}" == "pass" ]]; then
            icon="${GREEN}✓${RESET}"
        else
            icon="${RED}✗${RESET}"
        fi
        printf "  %b  %-35s %s\n" "$icon" "${STEP_NAMES[$i]}" "${DIM}${STEP_TIMES[$i]}${RESET}"
    done

    echo ""
    echo -e "  ${DIM}Total: ${total_elapsed}s${RESET}"

    # Show failure log paths
    if [[ "$FAILED" == true ]]; then
        echo -e "${RED}${BOLD}Failed step logs:${RESET}"
        for i in "${!STEP_NAMES[@]}"; do
            if [[ "${STEP_RESULTS[$i]}" == "FAIL" ]]; then
                echo -e "  ${RED}✗${RESET} ${STEP_NAMES[$i]}: ${DIM}${STEP_LOGFILES[$i]}${RESET}"
            fi
        done
        echo ""
        echo -e "${YELLOW}${BOLD}Tip:${RESET} Fix the issue above and run ${BOLD}git commit${RESET} again."
    fi

    echo ""
    return 0
}

# --- Smart skip: detect staged file changes ---
RUN_FORMAT=true
RUN_CMAKE_FORMAT=true
RUN_TIDY=true
RUN_TESTS=true
RUN_COVERAGE=true

STAGED_FILES=$(git diff --cached --name-only 2>/dev/null || true)
if [[ -n "$STAGED_FILES" ]]; then
    HAS_SRC_CHANGES=false
    HAS_TEST_CHANGES=false
    HAS_CPP_CHANGES=false
    HAS_CMAKE_CHANGES=false
    HAS_BENCH_CHANGES=false

    while IFS= read -r file; do
        [[ "$file" == src/* ]] && HAS_SRC_CHANGES=true
        [[ "$file" == tests/* ]] && HAS_TEST_CHANGES=true
        [[ "$file" =~ \.(cpp|cppm|h|hpp)$ ]] && HAS_CPP_CHANGES=true
        [[ "$file" =~ (CMakeLists\.txt|\.cmake)$ ]] && HAS_CMAKE_CHANGES=true
        [[ "$file" == benchmarks/* ]] && HAS_BENCH_CHANGES=true
    done <<< "$STAGED_FILES"

    if [[ "$HAS_CPP_CHANGES" == false && "$HAS_CMAKE_CHANGES" == false ]]; then
        echo -e "${DIM}ℹ  No C++ or cmake files in commit — skipping format, tidy, tests, coverage${RESET}"
        RUN_FORMAT=false RUN_CMAKE_FORMAT=false RUN_TIDY=false
        RUN_TESTS=false RUN_COVERAGE=false
    elif [[ "$HAS_CPP_CHANGES" == false ]]; then
        echo -e "${DIM}ℹ  cmake-only changes — skipping clang-format, clang-tidy${RESET}"
        RUN_FORMAT=false RUN_TIDY=false
    elif [[ "$HAS_SRC_CHANGES" == false && "$HAS_TEST_CHANGES" == false && "$HAS_CMAKE_CHANGES" == false ]]; then
        echo -e "${DIM}ℹ  No src/, tests/, or cmake changes — skipping tests, coverage${RESET}"
        RUN_TESTS=false RUN_COVERAGE=false
    fi

    if [[ "$HAS_CMAKE_CHANGES" == false ]]; then
        RUN_CMAKE_FORMAT=false
    fi
fi

RUN_BENCH_RELEASE=false
if [[ "$HAS_BENCH_CHANGES" == true && "$HAS_CPP_CHANGES" == true ]]; then
    RUN_BENCH_RELEASE=true
fi

# --- Count total steps ---
TOTAL_STEPS=0
[[ "$RUN_FORMAT" == true ]] && ((TOTAL_STEPS++))
[[ "$RUN_CMAKE_FORMAT" == true ]] && ((TOTAL_STEPS++))
[[ "$RUN_TIDY" == true ]] && ((TOTAL_STEPS++))
# clang-tidy needs module BMIs built first when the tree consumes `import std;`
# (issue #330). Only counts as a step when build/release actually references the
# std module — keeps the count accurate on pre-#326 trees that skip it.
RUN_TIDY_BMI=false
if [[ "$RUN_TIDY" == true && -f "build/release/compile_commands.json" ]] \
   && grep -q -- '-fmodule-file=std=' "build/release/compile_commands.json" 2>/dev/null; then
    RUN_TIDY_BMI=true
    ((TOTAL_STEPS++))
fi
[[ "$RUN_BENCH_RELEASE" == true ]] && ((TOTAL_STEPS++))
[[ "$RUN_TESTS" == true ]] && ((TOTAL_STEPS++))
[[ "$RUN_COVERAGE" == true ]] && ((TOTAL_STEPS++))

if [[ $TOTAL_STEPS -eq 0 ]]; then
    echo -e "${GREEN}✓ No checks needed for this commit.${RESET}"
    exit 0
fi

echo -e "${BOLD}Running $TOTAL_STEPS pre-commit checks...${RESET}"

# --- Ensure debug build is configured ---
if [[ ("$RUN_FORMAT" == true || "$RUN_CMAKE_FORMAT" == true) && ! -f "build/debug/build.ninja" ]]; then
    echo -e "${DIM}Configuring debug build for format targets...${RESET}"
    cmake --preset ninja-debug > /dev/null 2>&1
fi

# --- Step 1: clang-format ---
if [[ "$RUN_FORMAT" == true ]]; then
    run_step "clang-format" "$LOG_FORMAT" cmake --build --preset ninja-debug --target format || true
fi

# --- Step 2: cmake-format ---
if [[ "$RUN_CMAKE_FORMAT" == true ]]; then
    run_step "cmake-format" "$LOG_CMAKE_FORMAT" cmake --build --preset ninja-debug --target cmake-format || true
fi

# --- Step 3: clang-tidy ---
# Default to --diff mode (issue #262): only block on warnings touching staged
# lines, so pre-existing drift in unrelated files doesn't block unrelated work.
# Set STORM_TIDY_FULL=1 to force whole-file staged scan (the pre-#262 behavior).
if [[ "$RUN_TIDY" == true ]]; then
    # clang-tidy parses each TU using build/release/compile_commands.json. A TU
    # that does `import std;` (or `import storm;`, which transitively imports
    # std) needs the std/storm module BMIs (.pcm) to exist, and clang-tidy will
    # NOT build them itself — it fails with "module file '…std.pcm' not found".
    # Those BMIs are produced by the release build, which runs AFTER tidy
    # (Step 3b), so on a fresh/stale build/release the tidy step fails. Build the
    # module BMIs first — but only when the project actually consumes the std
    # module (post-#326). On a tree without `import std;`, TUs parse standalone
    # and this build is pure overhead, so we skip it. See issue #330.
    if [[ "$RUN_TIDY_BMI" == true ]]; then
        if [[ ! -f "build/release/build.ninja" ]]; then
            cmake --preset ninja-release > /dev/null 2>&1
        fi
        run_step "module BMIs (for clang-tidy)" "$LOG_TIDY_BMI" \
            cmake --build --preset ninja-release --target storm
    fi

    if [[ -n "$STORM_TIDY_FULL" ]]; then
        run_step "clang-tidy --full --fix" "$LOG_TIDY" ./scripts/run_clang_tidy.sh --full --fix || true
    else
        run_step "clang-tidy --diff --fix" "$LOG_TIDY" ./scripts/run_clang_tidy.sh --diff --fix || true
    fi
fi

# --- Re-stage files modified by format/tidy ---
if [[ "$RUN_FORMAT" == true || "$RUN_TIDY" == true ]]; then
    git add -u
fi

# --- Step 3b: bench release build ---
if [[ "$RUN_BENCH_RELEASE" == true ]]; then
    if [[ ! -f "build/release/build.ninja" ]]; then
        cmake --preset ninja-release > /dev/null 2>&1
    fi
    run_step "release build" "$LOG_RELEASE" \
        cmake --build --preset ninja-release
fi

# --- Step 4: tests ---
if [[ "$RUN_TESTS" == true ]]; then
    run_step_live "tests (SQLite + PostgreSQL)" "$LOG_TESTS" ctest --preset ninja-debug || true
fi

# --- Step 5: coverage ---
if [[ "$RUN_COVERAGE" == true ]]; then
    if [[ ! -f "build/debug/build.ninja" ]]; then
        cmake --preset ninja-debug > /dev/null 2>&1
    fi

    # Format line numbers as compact ranges (e.g., "5, 10-15, 20")
    format_line_ranges() {
        local -n _lines=$1
        local result="" range_start="" range_end=""
        for ln in "${_lines[@]}"; do
            if [[ -z "$range_start" ]]; then
                range_start=$ln; range_end=$ln
            elif [[ $ln -eq $((range_end + 1)) ]]; then
                range_end=$ln
            else
                if [[ "$range_start" == "$range_end" ]]; then
                    result+="${result:+, }$range_start"
                else
                    result+="${result:+, }${range_start}-${range_end}"
                fi
                range_start=$ln; range_end=$ln
            fi
        done
        if [[ -n "$range_start" ]]; then
            if [[ "$range_start" == "$range_end" ]]; then
                result+="${result:+, }$range_start"
            else
                result+="${result:+, }${range_start}-${range_end}"
            fi
        fi
        echo "$result"
    }

    # Parse lcov file and display uncovered files + line ranges
    show_uncovered_lines() {
        local lcov_file="build/debug/coverage/coverage-filtered.lcov"
        if [[ ! -f "$lcov_file" ]]; then
            return
        fi

        echo ""
        echo -e "${BOLD}Uncovered lines:${RESET}"
        echo -e "${DIM}$(printf '%.0s─' {1..60})${RESET}"

        local current_file=""
        local uncovered_lines=()
        local has_uncovered=false

        # Flush accumulated uncovered lines for current file
        flush_file() {
            if [[ ${#uncovered_lines[@]} -gt 0 ]]; then
                local rel_path="${current_file#$PWD/}"
                echo -e "  ${YELLOW}${rel_path}${RESET}"
                echo -e "    Lines: ${RED}$(format_line_ranges uncovered_lines)${RESET}"
                has_uncovered=true
            fi
        }

        while IFS= read -r line; do
            case "$line" in
                SF:*)
                    flush_file
                    current_file="${line#SF:}"
                    uncovered_lines=()
                    ;;
                DA:*)
                    local da_data="${line#DA:}"
                    local ln_num="${da_data%%,*}"
                    local exec_count="${da_data#*,}"
                    if [[ "$exec_count" == "0" ]]; then
                        uncovered_lines+=("$ln_num")
                    fi
                    ;;
            esac
        done < "$lcov_file"
        flush_file

        if [[ "$has_uncovered" == false ]]; then
            echo -e "  ${DIM}(could not parse uncovered lines from lcov)${RESET}"
        fi

        echo ""
        echo -e "${DIM}For detailed HTML report:${RESET}"
        echo -e "${DIM}  cmake --build --preset ninja-debug-coverage --target coverage-html${RESET}"
        echo -e "${DIM}  open build/debug/coverage/html-filtered/index.html${RESET}"
    }

    # Run coverage as a compound check: clean + build + parse + threshold
    run_coverage_check() {
        # Clean stale profraw/profdata to prevent false uncovered lines
        cmake --build --preset ninja-debug-coverage --target coverage-clean > /dev/null 2>&1
        local output
        output=$(cmake --build --preset ninja-debug-coverage --target coverage 2>&1)
        local build_exit=$?

        if [[ $build_exit -ne 0 ]]; then
            echo "$output" | tail -20
            echo ""
            echo "Coverage build/analysis failed."
            return 1
        fi

        # Extract line coverage percentage
        local line_cov
        line_cov=$(echo "$output" | grep "lines\.\.\.\.\.\.\." | tail -1 | grep -oP '[0-9.]+(?=%)')

        if [[ -z "$line_cov" ]]; then
            echo ""
            echo "Could not parse line coverage from output."
            return 1
        fi

        if [[ "$line_cov" != "100.0" ]]; then
            echo -e "${RED}${BOLD}Line coverage: ${line_cov}% (required: 100.0%)${RESET}"
            show_uncovered_lines
            return 1
        fi

        echo "Line coverage: 100.0%"
        return 0
    }

    run_step_live "coverage (100% required)" "$LOG_COVERAGE" run_coverage_check || true
fi

# --- Final summary ---
print_summary

if [[ "$FAILED" == true ]]; then
    exit 1
fi
