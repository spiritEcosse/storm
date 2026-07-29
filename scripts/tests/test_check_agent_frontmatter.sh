#!/bin/bash
# Tests for scripts/check-agent-frontmatter.sh (issue #543).
#
# The validator guards against the bug that made storm-sql-reviewer and
# storm-buildsystem-reviewer silently undispatchable: a `description:` field
# spanning multiple physical lines breaks Claude Code's frontmatter parse, and
# the agent is dropped with no warning.
#
# Strategy: write synthetic agent .md files into a temp dir, run the validator
# against that dir, and assert the exit code and the message. Each scenario is
# a function named scenario_<tag>; the dispatcher below handles the temp-dir
# boilerplate so scenarios only encode their fixture and assertions.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECKER="$REPO_ROOT/scripts/check-agent-frontmatter.sh"

PASS=0
FAIL=0
FAILED_TESTS=()

# Per-scenario state, populated by the dispatcher before calling the scenario.
TMP=""
CURRENT_TAG=""
OUT=""
RC=0

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

# --- Fixtures ---
#
# One writer for every fixture shape; `kind` selects the frontmatter body so
# the shared heredoc boilerplate lives in exactly one place.
#   good      - single-line \n-escaped description (the form that loads)
#   multiline - description spanning real newlines (the #543 bug)
#   no-desc   - frontmatter present, description absent
#   plain     - no frontmatter at all
#   other-key - valid, but with a trailing key outside the common set
#               (rule-standards.md uses `origin:`) — guards the span scan
#               against being written as a fixed key whitelist
write_agent() {
    local kind="$1" path="$2"

    if [[ "$kind" == "plain" ]]; then
        printf '# Just a markdown doc\n\nNot an agent — no frontmatter.\n' > "$path"
        return 0
    fi

    {
        echo "---"
        echo "name: fixture-agent"
        case "$kind" in
            good|other-key)
                printf 'description: Does a thing. Examples:\\n\\n<example>\\nContext: A context.\\nuser: "do it"\\n</example>\n'
                ;;
            multiline)
                printf 'description: Does a thing. Examples:\n\n<example>\nContext: A context.\nuser: "do it"\n</example>\n'
                ;;
            no-desc) ;;
            *)
                # A typo'd kind would otherwise write a description-less file
                # and surface as a confusing assertion failure downstream.
                echo "write_agent: unknown kind '$kind'" >&2
                exit 1
                ;;
        esac
        if [[ "$kind" == "other-key" ]]; then
            echo "origin: ECC"
        else
            echo "model: opus"
        fi
        echo "---"
        echo ""
        echo "Body text here."
    } > "$path"
    return 0
}

# Run the validator over a directory, capturing output and exit code.
run_checker() {
    local dir="${1:-$TMP}"
    OUT="$("$CHECKER" "$dir" 2>&1)"
    RC=$?
    return 0
}

# --- Assertion helpers ---

assert_rc_zero() {
    local what="$1"
    if [[ $RC -eq 0 ]]; then
        pass "$what"
    else
        fail "$what — expected exit 0, got $RC. Output: $OUT"
    fi
    return 0
}

assert_rc_nonzero() {
    local what="$1"
    if [[ $RC -ne 0 ]]; then
        pass "$what"
    else
        fail "$what — expected non-zero exit, got 0. Output: $OUT"
    fi
    return 0
}

assert_output_contains() {
    local needle="$1" what="$2"
    if [[ "$OUT" == *"$needle"* ]]; then
        pass "$what"
    else
        fail "$what — output lacked '$needle'. Output: $OUT"
    fi
    return 0
}

# --- Scenarios ---

scenario_all_single_line_passes() {
    write_agent good "$TMP/good.md"
    write_agent good "$TMP/also-good.md"
    run_checker
    assert_rc_zero "exit 0 when every description is single-line"
    return 0
}

scenario_multiline_description_fails() {
    write_agent good "$TMP/good.md"
    write_agent multiline "$TMP/bad.md"
    run_checker
    assert_rc_nonzero "non-zero exit when a description spans multiple lines"
    return 0
}

scenario_failure_names_the_offending_file() {
    write_agent multiline "$TMP/bad.md"
    run_checker
    assert_output_contains "bad.md" "error message names the offending file"
    return 0
}

scenario_failure_explains_the_fix() {
    write_agent multiline "$TMP/bad.md"
    run_checker
    # The message must tell the reader HOW to fix it, not just that it broke.
    assert_output_contains '\n' "error message explains the \\n-escape fix"
    return 0
}

scenario_missing_description_fails() {
    write_agent no-desc "$TMP/no-desc.md"
    run_checker
    assert_rc_nonzero "non-zero exit when an agent has no description at all"
    return 0
}

scenario_non_agent_markdown_ignored() {
    write_agent good "$TMP/good.md"
    write_agent plain "$TMP/rule-standards.md"
    run_checker
    assert_rc_zero "a frontmatter-less .md is not treated as an agent"
    return 0
}

scenario_empty_dir_passes() {
    run_checker
    assert_rc_zero "exit 0 on a directory with no agent files"
    return 0
}

# A valid agent whose trailing key is not `model:`/`color:` must still pass.
# Writing the span scan as a fixed key whitelist regresses exactly here.
scenario_uncommon_trailing_key_passes() {
    write_agent other-key "$TMP/origin-key.md"
    run_checker
    assert_rc_zero "a single-line description ending at an uncommon key passes"
    return 0
}

# The reported span must be the TRUE line count. Prose inside a broken
# description (`user:`, `Context:`) must not truncate the measurement.
scenario_reported_span_is_accurate() {
    # 6 description lines: the `description:` line + 5 continuation lines.
    {
        echo "---"
        echo "name: fixture-agent"
        echo "description: Does a thing. Examples:"
        echo ""
        echo "<example>"
        echo "Context: A context."
        echo 'user: "do it"'
        echo "</example>"
        echo "model: opus"
        echo "---"
    } > "$TMP/bad.md"
    run_checker
    assert_output_contains "spans 6 physical lines" "reported span counts past example prose"
    return 0
}

# The GitHub Actions annotation must go to STDERR, alongside the human-readable
# error, so the two cannot be separated in the log. Asserting on the split
# streams is the point: `run_checker` folds them with 2>&1 and would pass either
# way, which is why this branch was previously untested.
scenario_gha_annotation_on_stderr() {
    write_agent multiline "$TMP/bad.md"
    local on_stdout on_stderr
    on_stdout="$(GITHUB_ACTIONS=true "$CHECKER" "$TMP" 2>/dev/null)"
    on_stderr="$(GITHUB_ACTIONS=true "$CHECKER" "$TMP" 2>&1 >/dev/null)"
    OUT="stdout=[$on_stdout] stderr=[$on_stderr]"
    if [[ "$on_stderr" == *"::error file="* && "$on_stdout" != *"::error"* ]]; then
        pass "::error annotation goes to stderr, not stdout"
    else
        fail "annotation stream wrong. $OUT"
    fi
    return 0
}

# Without GITHUB_ACTIONS the annotation must not appear at all — a local run
# should show only the human-readable message.
scenario_no_annotation_outside_gha() {
    write_agent multiline "$TMP/bad.md"
    OUT="$(env -u GITHUB_ACTIONS "$CHECKER" "$TMP" 2>&1)"
    if [[ "$OUT" != *"::error"* && "$OUT" == *"bad.md"* ]]; then
        pass "no ::error annotation emitted outside GitHub Actions"
    else
        fail "unexpected annotation in local run. Output: $OUT"
    fi
    return 0
}

# The regression guard proper: the real repo must stay clean.
scenario_real_repo_agents_pass() {
    run_checker "$REPO_ROOT/.claude/agents"
    assert_rc_zero "the repo's own .claude/agents/ passes"
    return 0
}

SCENARIOS=(
    all_single_line_passes
    multiline_description_fails
    failure_names_the_offending_file
    failure_explains_the_fix
    missing_description_fails
    non_agent_markdown_ignored
    empty_dir_passes
    uncommon_trailing_key_passes
    reported_span_is_accurate
    gha_annotation_on_stderr
    no_annotation_outside_gha
    real_repo_agents_pass
)

echo "Testing check-agent-frontmatter.sh"
echo "================================================"

if [[ ! -x "$CHECKER" ]]; then
    echo "FATAL: $CHECKER is missing or not executable"
    exit 1
fi

for tag in "${SCENARIOS[@]}"; do
    CURRENT_TAG="$tag"
    echo ""
    echo "Scenario: $tag"
    TMP="$(mktemp -d)"
    "scenario_$tag"
    rm -rf "$TMP"
done

echo ""
echo "================================================"
echo "Passed: $PASS, Failed: $FAIL"

if [[ $FAIL -gt 0 ]]; then
    echo "Failed scenarios: ${FAILED_TESTS[*]}"
    exit 1
fi
exit 0
