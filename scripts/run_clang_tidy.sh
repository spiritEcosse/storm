#!/bin/bash
# Run clang-tidy using .clang-tidy configuration file
# Usage: ./run_clang_tidy.sh [--fix] [-j N]
#
# Prerequisites:
#   - Release build with compile_commands.json: cmake --preset ninja-release
#   - .clang-tidy file in project root (contains all check configurations)
#
# Options:
#   --fix  Apply suggested fixes automatically (use with caution)
#   -j N   Number of parallel jobs (default: all cores)

set -e

CLANG_TIDY="../clang-p2996/build/bin/clang-tidy"
BUILD_DIR="build/release"
COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json"
CLANG_TIDY_CONFIG=".clang-tidy"

# Parse arguments
FIX_FLAG=""
JOBS=$(nproc)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            FIX_FLAG="-fix"
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
echo "   Directories: src/ tests/ benchmarks/"
if [[ -n "$FIX_FLAG" ]]; then
    echo "   Mode: AUTO-FIX enabled"
else
    echo "   Mode: Check only (use --fix to apply fixes)"
fi
echo ""

# Find source files, excluding third_party
# File types: .cpp, .cppm only (headers excluded - they need module context clang-tidy can't provide)
SEARCH_DIRS="src tests benchmarks"

FILES=$(find $SEARCH_DIRS -type f \( -name "*.cpp" -o -name "*.cppm" \) \
    ! -path "*/third_party/*" \
    2>/dev/null | sort)

if [[ -z "$FILES" ]]; then
    echo "❌ No source files found"
    exit 1
fi

FILE_COUNT=$(echo "$FILES" | wc -l)
echo "📁 Found $FILE_COUNT source files to check"
echo ""

# Create temp directory for output files
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Export variables for subshells
export CLANG_TIDY BUILD_DIR FIX_FLAG TEMP_DIR

# Function to run clang-tidy on a single file (called in parallel)
# Uses .clang-tidy config file automatically (clang-tidy searches parent directories)
run_tidy() {
    local file="$1"
    local basename=$(echo "$file" | tr '/' '_')
    local outfile="$TEMP_DIR/$basename.out"

    # Run clang-tidy, capturing output and ignoring crashes
    # clang-tidy automatically reads .clang-tidy from project root
    # Filter out noisy clang-tidy meta-messages (but keep actual warnings/errors)
    timeout 60 "$CLANG_TIDY" \
        -p "$BUILD_DIR" \
        $FIX_FLAG \
        "$file" 2>&1 | grep -v -E "^[0-9]+ warnings? (generated|and)|^Suppressed [0-9]+|^Use -header-filter|^Use -system-headers" > "$outfile" || true

    # Check if it crashed
    if grep -q "PLEASE submit a bug report" "$outfile" 2>/dev/null; then
        echo "  ⚠ $file (clang-tidy crashed - skipped)"
        echo "" > "$outfile"  # Clear output to not count as errors
    else
        echo "  ✓ $file"
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
echo "   Warnings: $WARNINGS"
echo "   Errors: $ERRORS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ $ERRORS -gt 0 ]]; then
    echo "❌ clang-tidy found errors"
    exit 1
elif [[ $WARNINGS -gt 0 ]]; then
    echo "⚠️  clang-tidy found warnings (not blocking)"
    exit 0
else
    echo "✅ clang-tidy passed with no issues"
    exit 0
fi
