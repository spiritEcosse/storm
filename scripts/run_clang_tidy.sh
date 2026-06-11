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
_mem_avail_kb=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo 2>/dev/null)
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

# Known-skip list: files clang-tidy cannot parse and we accept that.
#
# Returns 0 (true) if a file is expected to fail clang-tidy parsing.
#
# These are precise file paths, not directory wildcards. The former broad
# "tests/*|benchmarks/*" wildcard was replaced in Issue #308 because 39 test
# files and most bench files parse fine — the wildcard was silently masking
# parse failures in files that should be clean.
#
# Files genuinely unparseable:
#   src/orm/query_builder.hpp — pseudo-module header; must be included after
#       `import storm;` so clang-tidy sees it without the BMI and fails.
#
#   Test headers that must come after `import storm;` — clang-tidy parses
#   them standalone and hits missing storm symbols:
#   tests/test_models.h, tests/test_seed_helpers.h, tests/test_select_runner.h,
#   tests/test_write_runner.h, tests/test_yaml_register.h,
#   tests/query/test_aggregate_fixture.h, tests/query/test_m2m_models.h,
#   tests/test_parser.hpp, tests/tools/storm_schema/models.h
#
#   benchmarks/bench_register.h — includes benchmark/benchmark.h which
#   clang-tidy cannot parse (gbench macro / linkage issue).
#
#   Benchmark textual headers — #included inside anonymous namespaces of main
#   TUs; cannot be parsed standalone (need import storm or benchmark BMI):
#   benchmarks/models.hpp, benchmarks/m2m_models.hpp, benchmarks/benchmark_tests.hpp,
#   benchmarks/dashboard/args.hpp, benchmarks/dashboard/backup.hpp,
#   benchmarks/dashboard/db.hpp, benchmarks/dashboard/events.hpp,
#   benchmarks/dashboard/tui_render.hpp, benchmarks/dashboard/models.hpp
#
#   benchmarks/schema.cppm — parses fine, but ANY ASTMatcher-based check
#       (every enabled check) SIGSEGVs clang-tidy inside
#       RecursiveASTVisitor::TraverseTemplateInstantiations on the std-module
#       VarTemplateDecl `std::__desugars_to_v` (a clang-p2996 ParentMap/AST
#       traversal bug over `import std;` instantiations). Reproduces with the
#       pre-#364 7-category config and with a single readability check, so it is
#       a toolchain crash, not a Storm-code or check-config issue. Tracked with
#       the other clang-p2996 module crashes under issue #262.
is_known_unparseable() {
    local file="$1"
    case "$file" in
        src/orm/query_builder.hpp) return 0 ;;
        tests/test_models.h) return 0 ;;
        tests/test_seed_helpers.h) return 0 ;;
        tests/test_select_runner.h) return 0 ;;
        tests/test_write_runner.h) return 0 ;;
        tests/test_yaml_register.h) return 0 ;;
        tests/query/test_aggregate_fixture.h) return 0 ;;
        tests/query/test_m2m_models.h) return 0 ;;
        tests/test_parser.hpp) return 0 ;;
        tests/tools/storm_schema/models.h) return 0 ;;
        benchmarks/bench_register.h) return 0 ;;
        benchmarks/models.hpp) return 0 ;;
        benchmarks/m2m_models.hpp) return 0 ;;
        benchmarks/benchmark_tests.hpp) return 0 ;;
        benchmarks/dashboard/args.hpp) return 0 ;;
        benchmarks/dashboard/backup.hpp) return 0 ;;
        benchmarks/dashboard/db.hpp) return 0 ;;
        benchmarks/dashboard/events.hpp) return 0 ;;
        benchmarks/dashboard/tui_render.hpp) return 0 ;;
        benchmarks/dashboard/models.hpp) return 0 ;;
        benchmarks/schema.cppm) return 0 ;;
        *) return 1 ;;
    esac
}
export -f is_known_unparseable

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

    # Drop diff sections for known-unparseable files BEFORE clang-tidy-diff.py
    # sees them. clang-tidy-diff.py has no concept of our skip-list, so it would
    # otherwise run clang-tidy on textual headers that cannot parse standalone
    # (they need `import storm;` / a benchmark BMI / are reflection-annotated) and
    # attribute their header-origin diagnostics to the staged lines. Since the
    # `import std;` migration these standalone parses fail hard enough to emit
    # dozens of spurious warnings/errors (e.g. tests/test_parser.hpp,
    # benchmarks/dashboard/*.hpp), which is pure noise on the diff. Filtering at
    # the source (vs. post-hoc output grepping) keeps the summary counts honest
    # and reuses the single is_known_unparseable() source of truth. See issue #326.
    filter_skiplist_from_diff() {
        local keep=1 path
        while IFS= read -r line; do
            if [[ "$line" == "diff --git "* ]]; then
                # path is the b-side: "diff --git a/<p> b/<p>"
                path="${line##* b/}"
                if is_known_unparseable "$path"; then keep=0; else keep=1; fi
            fi
            [[ "$keep" == 1 ]] && printf '%s\n' "$line"
        done
    }

    # -timeout 240 (issue #326): tests/yaml/test_unified_yaml.cpp runs an #embed +
    # consteval JSON parse under import std; (~78s, 2.4 GB RSS) — the old 60s
    # timed it out (Terminated by signal 9 / timeout) and failed the gate.
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
    # across versions (it may return 0 even with warnings).
    DIFF_WARN=$(grep -c ": warning:" "$DIFF_OUT" || true)
    DIFF_ERR=$(grep -c ": error:" "$DIFF_OUT" || true)
    # Strip out errors from C++26 module/reflection parse failures in headers —
    # they are noise in diff mode, not signal on the staged lines.
    # Also exclude parse failures in known third-party/annotation-dependent headers
    # that cannot be parsed standalone (e.g. benchmarks/dashboard/models.hpp uses
    # storm reflection annotations which require 'import storm').
    DIFF_ERR_REAL=$(grep ": error:" "$DIFF_OUT" \
        | grep -v -E "(module|import|reflect|std::meta|consteval|undeclared identifier 'storm'|use of undeclared|benchmarks/dashboard/models\.hpp|src/orm/query_builder\.hpp)" \
        | wc -l || true)

    echo ""
    echo "$RULE"
    echo "📊 --diff summary:"
    echo "   Warnings on staged lines: $DIFF_WARN"
    echo "   Errors on staged lines:   $DIFF_ERR (real: $DIFF_ERR_REAL)"
    echo "$RULE"

    if [[ "$DIFF_WARN" -gt 0 || "$DIFF_ERR_REAL" -gt 0 ]]; then
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


# Files clang-tidy must NEVER touch — even when it can parse them cleanly.
# Used to short-circuit run_tidy() so clang-tidy --fix never mutates the file.
#
# src/orm/generator.cppm is the upstream P2168 std::generator reference
# implementation (Lewis Baker / Corentin Jabot). clang-tidy's
# readability-identifier-naming rewrites `_T → T` and `__manual_lifetime →
# _manual_lifetime` on the primary template but misses the reference
# specialization, producing ill-formed code. Storm does not own this file;
# treat it as vendored — never lint, never auto-fix.
is_always_skip_file() {
    local file="$1"
    case "$file" in
        src/orm/generator.cppm) return 0 ;;
        */src/orm/generator.cppm) return 0 ;;
        *) return 1 ;;
    esac
}
export -f is_always_skip_file

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
    # `import std;` — e.g. tests/yaml/test_unified_yaml.cpp #embeds + consteval-
    # parses the unified test-case JSON (~78s, 2.4 GB RSS). 60s was too short.
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
