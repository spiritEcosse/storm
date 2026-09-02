#!/bin/bash
# Tests for scripts/lib/clang_tidy_skiplist.sh (issue #550).
#
# Two gaps this guards against:
#
#   1. DIFF_WARN in scripts/run_clang_tidy.sh --diff was counted unfiltered
#      while DIFF_ERR went through a post-hoc message-text regex — an
#      asymmetry that let toolchain/third-party parse noise block a commit
#      as a "warning" with no way to distinguish it from a real one.
#
#   2. That regex was unanchored (matched anywhere on a diagnostic line) and
#      had already once matched a genuine `unknown type name 'concept'` error
#      reported against a real Storm file (REAL_STORM_FILE below) — the same
#      shape of message it existed to suppress from toolchain headers.
#
# #550's fix retires the message-text regex entirely and relies solely on
# filter_skiplist_from_diff() dropping a file's WHOLE diff section by name
# (via is_known_unparseable()/is_always_skip_file()) before clang-tidy-diff.py
# ever runs on it — so filtering can never again be fooled by message content,
# only by file identity. These tests exercise that source-level mechanism
# directly, with no clang-tidy binary or compile_commands.json required.
#
# scenario_diff_toolchain_error_line_is_genuine and its warning-line sibling
# embed the EXACT diagnostic text captured from a real
# `clang-tidy -p build/release shared/models.h` run on this repo (not a
# fabricated string) — the memory note on mutation-testing guards: a
# synthetic fixture has false-greened this repo's guards before.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LIB="$REPO_ROOT/scripts/lib/clang_tidy_skiplist.sh"

# The real, ordinary Storm module file used throughout as "a genuine file
# that must never be classified as known-unparseable" — the direct #518
# regression target (see the module comment above).
REAL_STORM_FILE="src/orm/fields.cppm"

PASS=0
FAIL=0
FAILED_TESTS=()
CURRENT_TAG=""

fail() {
    local msg="$1"
    echo "  FAIL: $msg"
    FAIL=$((FAIL+1))
    FAILED_TESTS+=("$CURRENT_TAG")
    return 0
}

pass() {
    local msg="$1"
    echo "  PASS: $msg"
    PASS=$((PASS+1))
    return 0
}

if [[ ! -f "$LIB" ]]; then
    echo "FATAL: $LIB is missing"
    exit 1
fi
# shellcheck source=../lib/clang_tidy_skiplist.sh
source "$LIB"

# --- Fixture + assertion helpers ---
#
# One diff-section builder + one filter runner + two content assertions,
# shared by every scenario below that exercises filter_skiplist_from_diff, so
# the "diff --git a/<p> b/<p> ... @@ ... <content>" boilerplate lives once.

# diff_section <path> <content-line> — one synthetic `git diff --cached`
# section naming <path>, with <content-line> as its single added line.
diff_section() {
    local path="$1" content="$2"
    printf 'diff --git a/%s b/%s\nindex 1111111..2222222 100644\n--- a/%s\n+++ b/%s\n@@ -1,1 +1,2 @@\n existing line\n+%s\n' \
        "$path" "$path" "$path" "$path" "$content"
    return 0
}

# run_filter <section...> — concatenates the given diff sections and pipes
# them through filter_skiplist_from_diff, printing the result.
run_filter() {
    printf '%s\n' "$@" | filter_skiplist_from_diff
    return 0
}

assert_contains() {
    local haystack="$1" needle="$2" what="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        pass "$what"
    else
        fail "$what — expected to find '$needle'"
    fi
    return 0
}

assert_lacks() {
    local haystack="$1" needle="$2" what="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        pass "$what"
    else
        fail "$what — unexpectedly found '$needle'"
    fi
    return 0
}

# --- Scenarios ---

# A real, ordinary Storm module file must never be classified as
# known-unparseable. This is the direct regression guard for the #518
# incident: REAL_STORM_FILE is exactly the file the old message regex
# swallowed alongside toolchain noise.
scenario_real_storm_file_is_not_known_unparseable() {
    if is_known_unparseable "$REAL_STORM_FILE"; then
        fail "$REAL_STORM_FILE classified as known-unparseable"
    else
        pass "$REAL_STORM_FILE is NOT known-unparseable"
    fi
    return 0
}

# A representative sample of the documented skip list must still classify
# as known-unparseable after the extraction — the refactor must be behavior-
# preserving for every entry, not just the ones exercised elsewhere.
scenario_documented_entries_stay_classified() {
    local f
    for f in shared/models.h shared/query_builder.hpp tests/test_models.h \
             benchmarks/schema.cppm python/bindings.cpp \
             tests/crud/test_composite_pk_crud_body.h; do
        if is_known_unparseable "$f"; then
            pass "$f still classified as known-unparseable"
        else
            fail "$f no longer classified as known-unparseable"
        fi
    done
    return 0
}

# fuzz/*.cpp and fuzz/fuzz_models.h must be classified as known-unparseable
# (#550 review finding): they import storm the same way the test/benchmark
# headers do, have no compile_commands.json entry (ENABLE_FUZZING is off by
# default even under ninja-release), and — before this fix — were silently
# saved from blocking commits only by the retired message regex's generic
# `module`/`import`/`use of undeclared` alternatives, not by an explicit,
# reviewed entry. Retiring that regex without listing these files here would
# have made any staged fuzz/ edit permanently uncommittable.
scenario_fuzz_files_are_known_unparseable() {
    local f
    for f in fuzz/fuzz_batch_insert.cpp fuzz/fuzz_connection_string.cpp \
             fuzz/fuzz_like_pattern.cpp fuzz/fuzz_where_int.cpp \
             fuzz/fuzz_where_string.cpp fuzz/fuzz_models.h; do
        if is_known_unparseable "$f"; then
            pass "$f classified as known-unparseable"
        else
            fail "$f NOT classified as known-unparseable — staged edits to it would block forever"
        fi
    done
    return 0
}

# is_always_skip_file matches the vendored file by both its bare and prefixed
# path forms, and must not match an ordinary file.
scenario_always_skip_file_matches_generator() {
    if is_always_skip_file "src/orm/generator.cppm" && is_always_skip_file "vendor/src/orm/generator.cppm"; then
        pass "generator.cppm matched in both bare and prefixed forms"
    else
        fail "generator.cppm not matched in one of its path forms"
    fi
    if is_always_skip_file "$REAL_STORM_FILE"; then
        fail "$REAL_STORM_FILE incorrectly matched as always-skip"
    else
        pass "$REAL_STORM_FILE is NOT always-skip"
    fi
    return 0
}

# filter_skiplist_from_diff must drop the WHOLE diff section for a
# known-unparseable file (shared/models.h), including any diagnostic-shaped
# text embedded in its hunk — proving the drop is keyed by file identity in
# the "diff --git" header, never by scanning hunk content for messages. A
# second section for a real Storm file confirms the drop is per-file, not
# global (the rest of the diff must survive).
scenario_filter_drops_known_unparseable_file_section() {
    local output
    output=$(run_filter \
        "$(diff_section "shared/models.h" "some content")" \
        "$(diff_section "$REAL_STORM_FILE" "export module storm.orm.fields;")")

    assert_lacks "$output" "shared/models.h" "shared/models.h diff section fully dropped"
    assert_contains "$output" "$REAL_STORM_FILE" "sibling section for a real file survives"
    return 0
}

# The mutation-testing guard proper: the EXACT real error line captured from
# `clang-tidy -p build/release shared/models.h` on this repo — a genuine
# `unknown type name 'concept'` toolchain diagnostic — when it appears inside
# a REAL Storm file's diff section (REAL_STORM_FILE, not on the skip list),
# must survive filter_skiplist_from_diff untouched. This is the precise
# scenario the old message-based regex got wrong: it matched this exact text
# and would have dropped it even here, on a genuine Storm file.
scenario_diff_toolchain_error_line_is_genuine() {
    local real_error output
    real_error="/Users/ihor/projects/storm/storm/../clang-p2996/build/include/c++/v1/meta:440:1: error: unknown type name 'concept' [clang-diagnostic-error]"
    output=$(run_filter "$(diff_section "$REAL_STORM_FILE" "$real_error")")

    assert_contains "$output" "unknown type name 'concept'" \
        "real 'concept' error on a non-skiplisted Storm file survives filtering"
    return 0
}

# Same guard for a genuine WARNING line — the exact readability warning text
# captured from the same real clang-tidy run against shared/models.h — proving
# the fix (dropping the asymmetric filter, not adding one for warnings) still
# lets a real warning on a non-skiplisted file through untouched.
scenario_diff_real_warning_line_is_genuine() {
    local real_warning output
    real_warning="/Users/ihor/projects/storm/storm/${REAL_STORM_FILE}:70:12: warning: enum 'Color' uses a larger base type ('int', size: 4 bytes) than necessary for its value set, consider using 'std::uint8_t' (1 byte) as the base type to reduce its size [performance-enum-size]"
    output=$(run_filter "$(diff_section "$REAL_STORM_FILE" "$real_warning")")

    assert_contains "$output" "performance-enum-size" \
        "real warning on a non-skiplisted Storm file survives filtering"
    return 0
}

# filter_skiplist_from_diff must also drop an always-skip vendored file
# (generator.cppm), independent of the known-unparseable list.
scenario_filter_drops_always_skip_file_section() {
    local output
    output=$(run_filter "$(diff_section "src/orm/generator.cppm" "_T unused;")")

    assert_lacks "$output" "generator.cppm" "generator.cppm diff section fully dropped"
    return 0
}

# Regression guard for #550 gap 1/2 directly against the shipped script text:
# the retired message-based regex alternatives must not reappear in
# run_clang_tidy.sh, and DIFF_WARN/DIFF_ERR must be computed the same way
# (no "_REAL" filtered variant reintroducing the asymmetry).
scenario_script_has_no_message_based_regex() {
    local script contents
    script="$REPO_ROOT/scripts/run_clang_tidy.sh"
    contents="$(cat "$script")"

    assert_lacks "$contents" "DIFF_ERR_REAL" \
        "no DIFF_ERR_REAL (the asymmetric filtered count) in run_clang_tidy.sh"
    assert_lacks "$contents" "undeclared identifier 'storm'" \
        "the retired message-text regex is gone from run_clang_tidy.sh"
    return 0
}

# DIFF_WARN and DIFF_ERR must be computed the same way (#550 gap 1) — both a
# `grep -c ": warning:"` and a `grep -c ": error:"` over $DIFF_OUT, with no
# intervening `grep -v` filter on either. Asserts the symmetry directly
# rather than just the absence of one old variable name.
scenario_diff_warn_and_err_computed_symmetrically() {
    local script warn_line err_line
    script="$REPO_ROOT/scripts/run_clang_tidy.sh"
    warn_line="$(grep -F 'DIFF_WARN=$(grep -c ": warning:"' "$script")"
    err_line="$(grep -F 'DIFF_ERR=$(grep -c ": error:"' "$script")"

    if [[ -n "$warn_line" && -n "$err_line" ]]; then
        pass "DIFF_WARN and DIFF_ERR both computed as plain grep -c, no filter step between them"
    else
        fail "expected symmetric unfiltered grep -c for both DIFF_WARN and DIFF_ERR"
    fi
    return 0
}

SCENARIOS=(
    real_storm_file_is_not_known_unparseable
    documented_entries_stay_classified
    fuzz_files_are_known_unparseable
    always_skip_file_matches_generator
    filter_drops_known_unparseable_file_section
    diff_toolchain_error_line_is_genuine
    diff_real_warning_line_is_genuine
    filter_drops_always_skip_file_section
    script_has_no_message_based_regex
    diff_warn_and_err_computed_symmetrically
)

echo "Testing clang_tidy_skiplist.sh + run_clang_tidy.sh --diff filtering (#550)"
echo "================================================"

for tag in "${SCENARIOS[@]}"; do
    CURRENT_TAG="$tag"
    echo ""
    echo "Scenario: $tag"
    # A typo'd SCENARIOS entry (no matching scenario_<tag> function) must fail
    # loudly, not silently drop the scenario from the run — a dispatch that
    # never happened must never read as a pass.
    if ! declare -F "scenario_$tag" > /dev/null; then
        fail "scenario_$tag is not defined — check SCENARIOS for a typo"
        continue
    fi
    "scenario_$tag"
done

echo ""
echo "================================================"
echo "Passed: $PASS, Failed: $FAIL"

if [[ $FAIL -gt 0 ]]; then
    echo "Failed scenarios: ${FAILED_TESTS[*]}"
    exit 1
fi
exit 0
