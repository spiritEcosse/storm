#!/bin/bash
# Run clang-tidy. Three modes — pick one (or none, default is --diff):
#
#   --diff   (default)  Diff-mode — only warnings on lines touched by the
#                       staged commit (git diff --cached) block. Pre-existing
#                       warnings on other lines are out of scope. This is the
#                       pre-commit default.
#   --full              Full-file scan, staged files only. Block on any warning
#                       in any staged file (including pre-existing drift).
#                       Used to be the default before Issue #262.
#   --all               Full-file scan, ALL C++ files. Used by the scheduled
#                       weekly CI sweep to detect accumulated drift.
#
# Prerequisites:
#   - Release build with compile_commands.json: cmake --preset ninja-release
#   - .clang-tidy file in project root (contains all check configurations)
#
# Options:
#   --fix   Apply suggested fixes automatically (use with caution)
#   -j N    Number of parallel jobs (default: all cores)

set -e

readonly RULE='━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━'

CLANG_TIDY="../clang-p2996/build/bin/clang-tidy"
CLANG_TIDY_DIFF="../clang-p2996/clang-tools-extra/clang-tidy/tool/clang-tidy-diff.py"
BUILD_DIR="build/release"
COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json"
CLANG_TIDY_CONFIG=".clang-tidy"

# Parse arguments
FIX_FLAG=""
MODE="diff"   # diff | full | all

# Memory-aware default job count (issue #326). Since the `import std;` migration,
# every clang-tidy process loads the full std-module BMI (~1.5-2 GB resident),
# so `-j $(nproc)` on a many-core / modest-RAM box OOM-kills the run. Default to
# min(nproc, MemAvailable_GB / 2) with a floor of 1; an explicit `-j N` overrides.
# ~2 GB/job is the headroom budget per import-std clang-tidy invocation.
_nproc=$(nproc)
# /proc/meminfo doesn't exist off Linux (e.g. macOS) — awk then exits non-zero, which
# would otherwise kill this script under `set -e` despite the 2>/dev/null. `|| true`
# lets that fall through to the nproc-only branch below, same as MemAvailable missing.
_mem_avail_kb=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo 2>/dev/null || true)
if [[ -n "$_mem_avail_kb" ]]; then
    _mem_jobs=$((_mem_avail_kb / 1024 / 1024 / 2))   # MemAvailable(GB) / 2
    [[ "$_mem_jobs" -lt 1 ]] && _mem_jobs=1
    JOBS=$(( _nproc < _mem_jobs ? _nproc : _mem_jobs ))
else
    JOBS="$_nproc"
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            FIX_FLAG="-fix"
            shift
            ;;
        --diff)
            MODE="diff"
            shift
            ;;
        --full)
            MODE="full"
            shift
            ;;
        --all)
            MODE="all"
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

if [[ "$MODE" == "diff" && ! -f "$CLANG_TIDY_DIFF" ]]; then
    echo "❌ Error: clang-tidy-diff.py not found at $CLANG_TIDY_DIFF" >&2
    echo "   Required for --diff mode. Re-run with --full or --all, or fix the path." >&2
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
case "$MODE" in
    diff) echo "   Scope: staged lines only (git diff --cached, lines added/changed)" ;;
    full) echo "   Scope: staged files (whole-file scan)" ;;
    all)  echo "   Scope: ALL C++ source files (sweep)" ;;
    *)
        echo "❌ Internal error: unknown MODE '$MODE'" >&2
        exit 2
        ;;
esac
if [[ -n "$FIX_FLAG" ]]; then
    echo "   Mode: AUTO-FIX enabled"
else
    echo "   Mode: Check only (use --fix to apply fixes)"
fi
echo ""

# is_known_unparseable(), is_always_skip_file(), and filter_skiplist_from_diff()
# live in scripts/lib/clang_tidy_skiplist.sh — extracted in #550 so
# scripts/tests/test_run_clang_tidy_skiplist.sh can unit-test the
# classification without a live clang-tidy binary. See that file for the
# per-entry rationale (which files are unparseable and why).
source "$(dirname "${BASH_SOURCE[0]}")/lib/clang_tidy_skiplist.sh"

# ─── --diff mode short path ─────────────────────────────────────────────────
# Pipe `git diff -U0 --cached` through clang-tidy-diff.py — it only emits
# diagnostics on lines the staged commit touches. Pre-existing warnings on
# other lines are out of scope. See Issue #262 for rationale.
if [[ "$MODE" == "diff" ]]; then
    DIFF_OUT=$(mktemp)
    trap "rm -f $DIFF_OUT" EXIT

    # -p1 strips the a/ b/ prefix that `git diff` prepends to paths.
    # -iregex limits the scan to C++ sources we actually want clang-tidy on
    # (note: clang-tidy-diff.py doesn't understand our skip-list, so files
    # under tests/ etc. will be attempted; we filter the output after).
    DIFF_FIX=""
    [[ -n "$FIX_FLAG" ]] && DIFF_FIX="-fix"

    # filter_skiplist_from_diff (scripts/lib/clang_tidy_skiplist.sh) drops diff
    # sections for known-unparseable / always-skip files BEFORE clang-tidy-diff.py
    # sees them — see that file for the rationale.

    # -timeout 240 (issue #326): the tests/yaml/test_unified_yaml_*.cpp TUs run an
    # #embed + consteval JSON parse under import std; — the old 60s timed them out
    # (Terminated by signal 9 / timeout) and failed the gate. Issue #561 split the
    # corpus across those four TUs, cutting each one's share of that cost.
    # NOTE: deliberately NOT passing -config-file here. Forcing the root
    # .clang-tidy overrides clang-tidy's normal directory-hierarchy config lookup,
    # which defeats per-directory overrides like tests/.clang-tidy (it sets
    # InheritParentConfig: true and disables readability-function-cognitive-
    # complexity — "large test bodies are intentional coverage"). With the root
    # config forced, that disable was ignored and pre-existing complex TEST bodies
    # fired the moment an unrelated import-std edit touched a line inside them
    # (issue #326). Letting clang-tidy walk the tree picks up root .clang-tidy for
    # src/ and the merged tests/.clang-tidy for tests/, matching the --full path
    # (which already omits -config-file).
    set +e
    git diff -U0 --cached \
        | filter_skiplist_from_diff \
        | python3 "$CLANG_TIDY_DIFF" \
            -clang-tidy-binary "$CLANG_TIDY" \
            -p1 \
            -path "$BUILD_DIR" \
            -iregex '.*\.(cpp|cppm|h|hpp)' \
            -j "$JOBS" \
            -timeout 240 \
            -quiet \
            $DIFF_FIX \
            2>&1 | tee "$DIFF_OUT"
    DIFF_RC=${PIPESTATUS[1]}
    set -e

    # Stage any auto-fixes so the re-check sees them
    [[ -n "$FIX_FLAG" ]] && git add -u 2>/dev/null || true

    # clang-tidy-diff.py prints "No relevant changes found." when nothing in
    # the diff matches the regex. That's a clean pass.
    if grep -q "No relevant changes found" "$DIFF_OUT"; then
        echo ""
        echo "✅ clang-tidy --diff: no staged C++ lines to check"
        exit 0
    fi

    # Count actual diagnostics — clang-tidy-diff.py's exit code is unreliable
    # across versions (it may return 0 even with warnings). Both counts are
    # unfiltered: toolchain/third-party standalone-parse noise never reaches
    # $DIFF_OUT in the first place, because filter_skiplist_from_diff already
    # dropped those files' diff sections above before clang-tidy-diff.py ran
    # (source-level skip, keyed by file identity via is_known_unparseable()).
    # A post-hoc message-text regex used to filter DIFF_ERR only — asymmetric
    # with DIFF_WARN (#550 gap 1) and, being unanchored, it once matched a
    # genuine `unknown type name 'concept'` error against a real Storm file
    # (src/orm/fields.cppm) as well as the toolchain noise it was meant to
    # catch (#550 gap 2). Retired in favour of the source-level skip alone: a
    # file not on that reviewed list is never silently excluded again — if
    # one starts producing this noise, it needs an entry (and a rationale) in
    # scripts/lib/clang_tidy_skiplist.sh, the same as every file already there.
    #
    # DIFF_ERR is labelled "in staged files", not "on staged lines" like
    # DIFF_WARN: clang-tidy's -line-filter (which clang-tidy-diff.py sets per
    # file) only suppresses warnings outside the diffed hunks — a hard parse
    # failure (clang-diagnostic-error) is reported regardless of which line
    # triggered it, since the rest of the TU couldn't be checked at all. So a
    # one-line staged edit to a TU that fails to parse standalone blocks on
    # that whole-file failure, not on the edited line specifically.
    DIFF_WARN=$(grep -c ": warning:" "$DIFF_OUT" || true)
    DIFF_ERR=$(grep -c ": error:" "$DIFF_OUT" || true)

    echo ""
    echo "$RULE"
    echo "📊 --diff summary:"
    echo "   Warnings on staged lines: $DIFF_WARN"
    echo "   Errors in staged files:   $DIFF_ERR"
    echo "$RULE"

    if [[ "$DIFF_WARN" -gt 0 || "$DIFF_ERR" -gt 0 ]]; then
        if [[ -n "$FIX_FLAG" ]]; then
            # Auto-fix pass complete — re-run check-only to confirm clean.
            echo ""
            echo "🔄 Re-checking after auto-fix..."
            FIX_FLAG="" exec "$0" --diff
        fi
        echo "❌ clang-tidy --diff: new warnings/errors on staged lines"
        exit 1
    fi

    echo "✅ clang-tidy --diff: staged lines are clean"
    exit 0
fi

# ─── --full and --all modes: file-based scan ────────────────────────────────
if [[ "$MODE" == "all" ]]; then
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
    if [[ "$MODE" == "all" ]]; then
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


# Function to run clang-tidy on a single file (called in parallel)
# Uses .clang-tidy config file automatically (clang-tidy searches parent directories)
run_tidy() {
    local file="$1"
    local basename=$(echo "$file" | tr '/' '_')
    local outfile="$TEMP_DIR/$basename.out"
    local statusfile="$TEMP_DIR/$basename.status"

    # Short-circuit for vendored / upstream files we must never touch (e.g.
    # generator.cppm — see is_always_skip_file rationale).
    if is_always_skip_file "$file"; then
        echo "  ⏭  $file (always-skip — vendored upstream)"
        echo "known" > "$statusfile"
        echo "" > "$outfile"
        return 0
    fi

    # Run clang-tidy, capturing output and ignoring crashes
    # clang-tidy automatically reads .clang-tidy from project root
    # Filter out noisy clang-tidy meta-messages (but keep actual warnings/errors)
    # Timeout 240s (issue #326): some TUs run a heavy consteval pass under
    # `import std;` — e.g. each tests/yaml/test_unified_yaml_*.cpp #embeds and
    # consteval-parses its slice of the unified test-case corpus. 60s was too
    # short even before #561 split that corpus four ways.
    timeout 240 "$CLANG_TIDY" \
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
        if is_known_unparseable "$file"; then
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
            # Parse failure on a file we expect to be parseable (e.g. src/*.cppm
            # since the 2026-05-11 clang-p2996 rebuild). Surface it loudly — it
            # almost always means the build state is stale or the toolchain
            # regressed. Silent skipping here was the root cause of Issue #262.
            echo "  ❌ $file (PARSE FAILURE — toolchain or build state is broken)" >&2
            echo "     clang-tidy could not parse this file. Re-run cmake --preset" >&2
            echo "     ninja-release and rebuild before retrying. See Issue #262." >&2
            echo "crashed" > "$statusfile"
            # Keep the original error in the outfile so the summary can print it.
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
            if is_known_unparseable "$file"; then
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
echo "$RULE"

# Collect and display results — deduplicate across parallel output files first.
# Without sort -u, each warning appears once per parallel worker that saw it,
# inflating the count (e.g. 50 real warnings reported as 665).
WARNINGS=$(cat "$TEMP_DIR"/*.out 2>/dev/null | grep ": warning:" | sort -u | wc -l) || WARNINGS=0
ERRORS=$(cat "$TEMP_DIR"/*.out 2>/dev/null | grep ": error:" | sort -u | wc -l) || ERRORS=0

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

echo "$RULE"
echo "📊 Summary:"
echo "   Files checked: $FILE_COUNT"
echo "   Files passed: $FILES_OK"
echo "   C++26 skipped: $KNOWN_SKIPPED (expected - modules/reflection not supported)"
if [[ $UNEXPECTED_CRASHES -gt 0 ]]; then
    echo "   Unexpected crashes: $UNEXPECTED_CRASHES ⚠️"
fi
echo "   Warnings: $WARNINGS"
echo "   Errors: $ERRORS"
echo "$RULE"

# Exit logic:
# 1. Errors always fail
# 2. Unexpected crashes fail (not in known list)
# 3. Warnings BLOCK commits (must fix or acknowledge)
# 4. Known C++26 skips are OK

if [[ $ERRORS -gt 0 ]]; then
    echo "❌ clang-tidy found errors"
    exit 1
elif [[ $UNEXPECTED_CRASHES -gt 0 ]]; then
    echo "❌ clang-tidy could not parse $UNEXPECTED_CRASHES file(s) — toolchain or build state may be stale"
    echo "   Try: rm -rf build/release && cmake --preset ninja-release && cmake --build --preset ninja-release"
    echo "   See: https://github.com/spiritEcosse/storm/issues/262"
    exit 1
elif [[ $WARNINGS -gt 0 ]]; then
    if [[ -n "$FIX_FLAG" ]]; then
        # Fixes were applied — re-stage and re-run check-only to verify all warnings are gone.
        git add -u 2>/dev/null || true
        echo ""
        echo "🔄 Re-checking after auto-fix..."
        exec "$0"
    fi
    echo "❌ clang-tidy found $WARNINGS warning(s) - fix before committing"
    echo "   Run with --fix to auto-fix some issues, or update .clang-tidy to exclude checks"
    exit 1
else
    echo "✅ clang-tidy passed with no issues"
    exit 0
fi
