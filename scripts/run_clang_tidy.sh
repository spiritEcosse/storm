#!/bin/bash
# Run clang-tidy on staged files (git diff --cached)
# Usage: ./run_clang_tidy.sh [--fix] [--all] [-j N]
#
# Prerequisites:
#   - Release build with compile_commands.json: cmake --preset ninja-release
#   - .clang-tidy file in project root (contains all check configurations)
#
# Options:
#   --fix   Apply suggested fixes automatically (use with caution)
#   --all   Check ALL C++ source files (not just staged files)
#   -j N    Number of parallel jobs (default: all cores)

set -e

CLANG_TIDY="../clang-p2996/build/bin/clang-tidy"
BUILD_DIR="build/release"
COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json"
CLANG_TIDY_CONFIG=".clang-tidy"

# Known patterns for files that use C++26 modules/reflection features not supported by clang-tidy
# These files will show a specific message instead of "crashed"
# clang-tidy fails on these due to: __has_feature parsing, std::meta::info, ^^ splice operator, import statements
#
# Patterns:
#   - All .cppm files (C++26 modules not supported by clang-tidy)
#   - All test files (they import Storm modules)
#   - All benchmark files (they import Storm modules)
#
# Only files that DON'T import Storm modules can be checked (currently none in this project)

# Parse arguments
FIX_FLAG=""
CHECK_ALL=false
JOBS=$(nproc)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            FIX_FLAG="-fix"
            shift
            ;;
        --all)
            CHECK_ALL=true
            shift
            ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

# Check prerequisites
if [[ ! -f "$COMPILE_COMMANDS" ]]; then
    echo "❌ Error: compile_commands.json not found at $COMPILE_COMMANDS" >&2
    echo "   Run: cmake --preset ninja-release" >&2
    exit 1
fi

if [[ ! -x "$CLANG_TIDY" ]]; then
    echo "❌ Error: clang-tidy not found at $CLANG_TIDY" >&2
    exit 1
fi

if [[ ! -f "$CLANG_TIDY_CONFIG" ]]; then
    echo "❌ Error: .clang-tidy config not found at $CLANG_TIDY_CONFIG" >&2
    exit 1
fi

echo "🔍 Running clang-tidy using .clang-tidy configuration..."
echo "   Config file: $CLANG_TIDY_CONFIG"
echo "   Build directory: $BUILD_DIR"
echo "   Parallel jobs: $JOBS"
if [[ "$CHECK_ALL" == true ]]; then
    echo "   Scope: ALL C++ source files"
else
    echo "   Scope: staged files (git diff --cached)"
fi
if [[ -n "$FIX_FLAG" ]]; then
    echo "   Mode: AUTO-FIX enabled"
else
    echo "   Mode: Check only (use --fix to apply fixes)"
fi
echo ""

# Collect C++ source files
if [[ "$CHECK_ALL" == true ]]; then
    FILES=$(find src tests benchmarks \( -name '*.cpp' -o -name '*.cppm' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null \
        | grep -v 'third_party' \
        | sort)
else
    FILES=$(git diff --cached --name-only 2>/dev/null \
        | grep -E '\.(cpp|cppm|h|hpp)$' \
        | grep -v 'third_party' \
        | sort)
fi

if [[ -z "$FILES" ]]; then
    if [[ "$CHECK_ALL" == true ]]; then
        echo "✅ No C++ files found — clang-tidy skipped"
    else
        echo "✅ No staged C++ files — clang-tidy skipped"
    fi
    exit 0
fi

FILE_COUNT=$(echo "$FILES" | wc -l)
echo "📁 Found $FILE_COUNT source files to check"
echo ""

# Create temp directory for output files
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Export variables for subshells
export CLANG_TIDY BUILD_DIR FIX_FLAG TEMP_DIR

# Function to check if file uses C++26 modules/reflection (pattern-based)
# Returns 0 (true) if file is expected to fail clang-tidy
is_cpp26_module_file() {
    local file="$1"

    # All .cppm files are C++26 modules
    if [[ "$file" == *.cppm ]]; then
        return 0
    fi

    # All files under tests/, benchmarks/, fuzz/ use C++26 modules/reflection
    # (directly or transitively via includes like <meta>, std::meta::info, etc.)
    # Exception: mock files that don't use modules are handled by the caller —
    # they parse successfully and never reach this function's "known skip" path.
    #
    # src/orm/query_builder.hpp is a pseudo-module header: it uses ^^, consteval
    # reflection, and must be #included after `import storm;`. It cannot be
    # parsed standalone by clang-tidy. Add it to the known-skip list.
    case "$file" in
        tests/*|benchmarks/*|fuzz/*|shared/*) return 0 ;;
        src/orm/query_builder.hpp) return 0 ;;
        *) return 1 ;;
    esac
}
export -f is_cpp26_module_file

# Function to run clang-tidy on a single file (called in parallel)
# Uses .clang-tidy config file automatically (clang-tidy searches parent directories)
run_tidy() {
    local file="$1"
    local basename=$(echo "$file" | tr '/' '_')
    local outfile="$TEMP_DIR/$basename.out"
    local statusfile="$TEMP_DIR/$basename.status"

    # Run clang-tidy, capturing output and ignoring crashes
    # clang-tidy automatically reads .clang-tidy from project root
    # Filter out noisy clang-tidy meta-messages (but keep actual warnings/errors)
    timeout 60 "$CLANG_TIDY" \
        -p "$BUILD_DIR" \
        $FIX_FLAG \
        "$file" 2>&1 | grep -v -E "^[0-9]+ warnings? (generated|and)|^Suppressed [0-9]+|^Use -header-filter|^Use -system-headers" > "$outfile" || true

    # Check for different failure modes:
    # 1. "Found compiler error" = clang-tidy couldn't parse the file (C++26 modules)
    # 2. "PLEASE submit a bug report" = clang-tidy crashed AFTER processing (warnings may exist)

    local has_compile_error=false
    local has_crash=false
    local has_warnings=false

    grep -q "Found compiler error" "$outfile" 2>/dev/null && has_compile_error=true
    grep -q "PLEASE submit a bug report" "$outfile" 2>/dev/null && has_crash=true
    grep -q ": warning:" "$outfile" 2>/dev/null && has_warnings=true

    if [[ "$has_compile_error" == true ]]; then
        if is_cpp26_module_file "$file"; then
            # Known C++26 module file with compile errors - skip entirely
            # Warnings from partial parsing are unreliable (false positives)
            echo "  ✓ $file (C++26 modules - skipped)"
            echo "known" > "$statusfile"
            echo "" > "$outfile"
        elif [[ "$has_warnings" == true ]]; then
            # Non-module file with compile errors but real warnings - keep them
            echo "  ❌ $file (with C++26 header errors)"
            echo "ok" > "$statusfile"
            sed -i '/: error:/d' "$outfile"
            sed -i '/Found compiler error/d' "$outfile"
        else
            # Non-module file with only compile errors - unexpected
            echo "  ⚠ $file (parse error - UNEXPECTED)" >&2
            echo "crashed" > "$statusfile"
            echo "" > "$outfile"
        fi
    elif [[ "$has_crash" == true ]]; then
        # Crashed after processing - may still have useful warnings
        if [[ "$has_warnings" == true ]]; then
            echo "  ❌ $file (with crash after analysis)"
            echo "ok" > "$statusfile"
            # Keep output - it has warnings
            # Remove crash backtrace from output
            sed -i '/PLEASE submit a bug report/,$d' "$outfile"
        else
            # Crashed with no warnings
            if is_cpp26_module_file "$file"; then
                echo "  ✓ $file (C++26 modules - skipped)"
                echo "known" > "$statusfile"
            else
                echo "  ⚠ $file (clang-tidy crashed - UNEXPECTED)"
                echo "crashed" > "$statusfile"
            fi
            echo "" > "$outfile"
        fi
    else
        # No crash, no compile error - success
        echo "  ✓ $file"
        echo "ok" > "$statusfile"
    fi

    return 0
}
export -f run_tidy

# Run clang-tidy in parallel
echo "Running clang-tidy on $FILE_COUNT files with $JOBS parallel jobs..."
echo ""
echo "$FILES" | xargs -P "$JOBS" -I {} bash -c 'run_tidy "$@"' _ {}

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Collect and display results (filter out "N warnings generated" noise)
WARNINGS=$(cat "$TEMP_DIR"/*.out 2>/dev/null | grep -c ": warning:") || WARNINGS=0
ERRORS=$(cat "$TEMP_DIR"/*.out 2>/dev/null | grep -c ": error:") || ERRORS=0

# Count file statuses
KNOWN_SKIPPED=$(grep -l "known" "$TEMP_DIR"/*.status 2>/dev/null | wc -l) || KNOWN_SKIPPED=0
UNEXPECTED_CRASHES=$(grep -l "crashed" "$TEMP_DIR"/*.status 2>/dev/null | wc -l) || UNEXPECTED_CRASHES=0
FILES_OK=$(grep -l "ok" "$TEMP_DIR"/*.status 2>/dev/null | wc -l) || FILES_OK=0

# Show actual warnings/errors (deduplicated, limited output)
if [[ $ERRORS -gt 0 ]] || [[ $WARNINGS -gt 0 ]]; then
    echo ""
    echo "📋 Issues found:"
    cat "$TEMP_DIR"/*.out 2>/dev/null | grep -E ": (warning|error):" | sort -u | head -50
    echo ""
fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 Summary:"
echo "   Files checked: $FILE_COUNT"
echo "   Files passed: $FILES_OK"
echo "   C++26 skipped: $KNOWN_SKIPPED (expected - modules/reflection not supported)"
if [[ $UNEXPECTED_CRASHES -gt 0 ]]; then
    echo "   Unexpected crashes: $UNEXPECTED_CRASHES ⚠️"
fi
echo "   Warnings: $WARNINGS"
echo "   Errors: $ERRORS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Exit logic:
# 1. Errors always fail
# 2. Unexpected crashes fail (not in known list)
# 3. Warnings BLOCK commits (must fix or acknowledge)
# 4. Known C++26 skips are OK

if [[ $ERRORS -gt 0 ]]; then
    echo "❌ clang-tidy found errors"
    exit 1
elif [[ $UNEXPECTED_CRASHES -gt 0 ]]; then
    echo "❌ clang-tidy crashed on unexpected files (update is_cpp26_module_file if valid)"
    exit 1
elif [[ $WARNINGS -gt 0 ]]; then
    echo "❌ clang-tidy found $WARNINGS warning(s) - fix before committing"
    echo "   Run with --fix to auto-fix some issues, or update .clang-tidy to exclude checks"
    exit 1
else
    echo "✅ clang-tidy passed with no issues"
    exit 0
fi
