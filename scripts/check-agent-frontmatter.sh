#!/bin/bash
# Validate Claude Code agent frontmatter (issue #543).
#
# Claude Code parses each .claude/agents/*.md file's YAML frontmatter to
# register the agent. In YAML, an unquoted scalar ends at the first line
# starting in column 0 — so a `description:` written across real newlines
# terminates early, the next line is read as a key, the parse fails, and the
# agent is dropped SILENTLY: it simply never appears in the available-agents
# list. That is what made storm-sql-reviewer and storm-buildsystem-reviewer
# undispatchable from the day they merged, leaving CLAUDE.md rule #13
# partly unenforceable.
#
# The fix, and the invariant this script enforces: keep `description:` on ONE
# physical line, escaping newlines as literal \n (the form used by every agent
# file that loads correctly).
#
# Note this is deliberately NOT a strict YAML validation — the descriptions
# contain unquoted ':' characters that a strict parser rejects, yet Claude
# Code's lenient parser accepts them. The single-line span is the property
# that actually distinguishes loading from non-loading files.
#
# Usage: check-agent-frontmatter.sh [agents-dir]   (default: .claude/agents)
# Exit:  0 = all agent files valid, 1 = at least one invalid.

set -uo pipefail

AGENTS_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.claude/agents}"

if [[ ! -d "$AGENTS_DIR" ]]; then
    echo "check-agent-frontmatter: no such directory: $AGENTS_DIR" >&2
    exit 1
fi

ERRORS=0

# Under GitHub Actions, emit a workflow annotation so the failure surfaces on
# the file itself in the PR diff rather than only in the raw job log.
report() {
    local file="$1" problem="$2"
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error file=${file#"$PWD/"}::$problem" >&2
    fi
    echo "✗ $(basename "$file"): $problem" >&2
    ERRORS=$((ERRORS + 1))
    return 0
}

shopt -s nullglob
for file in "$AGENTS_DIR"/*.md; do
    # A file without opening frontmatter is not an agent (e.g. rule-standards.md,
    # which is shared prose included by the agent files). Skip it.
    [[ "$(head -n 1 "$file")" == "---" ]] || continue

    # Line number of the closing '---' that ends the frontmatter block.
    fm_end="$(awk 'NR > 1 && /^---$/ { print NR; exit }' "$file")"
    if [[ -z "$fm_end" ]]; then
        report "$file" "frontmatter opened with '---' but never closed"
        continue
    fi

    desc_start="$(awk -v end="$fm_end" \
        'NR < end && /^description:/ { print NR; exit }' "$file")"
    if [[ -z "$desc_start" ]]; then
        report "$file" "no 'description:' field in frontmatter — the agent cannot be registered"
        continue
    fi

    # The description spans every line until the next frontmatter key or the
    # closing '---'. Anything past the first line means the YAML scalar
    # terminates early and the parse breaks.
    #
    # A frontmatter key here is a column-0 `lowercase-word:` — deliberately
    # narrow so it does NOT match the prose inside a broken multi-line
    # description: `Context:` and `<example>` fail the lowercase/leading-char
    # test, and `user:`/`assistant:` are excluded by name. Without those
    # exclusions a 28-line description measures as 3, understating the span;
    # with a fixed key whitelist instead, an unlisted key (`origin:`) would
    # make a VALID file measure to the closing '---' and false-positive.
    desc_end="$(awk -v start="$desc_start" -v end="$fm_end" '
        NR > start && NR < end &&
        /^[a-z][a-z0-9_-]*:/ &&
        !/^(user|assistant):/ { print NR; exit }' "$file")"
    [[ -n "$desc_end" ]] || desc_end="$fm_end"

    span=$((desc_end - desc_start))
    if [[ $span -gt 1 ]]; then
        report "$file" "description: spans $span physical lines — must be 1"
        echo "    Claude Code will silently DROP this agent; it will not appear" >&2
        echo "    in the available-agents list at all." >&2
        echo "    Fix: fold the description onto a single line, writing newlines" >&2
        echo "    as literal \\n escapes (see reviewer.md for the working form)." >&2
    fi
done
shopt -u nullglob

if [[ $ERRORS -gt 0 ]]; then
    echo "" >&2
    echo "check-agent-frontmatter: $ERRORS agent file(s) invalid" >&2
    exit 1
fi

exit 0
