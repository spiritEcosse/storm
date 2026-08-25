#!/usr/bin/env python3
"""
PostToolUse hook — blocks Write/Edit when code smells are detected.

Reads the JSON event from stdin, extracts the file path, runs smell_checks,
and returns {"decision": "block", "reason": "..."} on violations.
"""

import json
import os
import sys

# Allow imports from the same directory
sys.path.insert(0, os.path.dirname(__file__))

from smell_checks import analyze_file


def main() -> None:
    try:
        event = json.load(sys.stdin)
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)  # fail open

    tool_input = event.get("tool_input", {})
    path = tool_input.get("file_path") or tool_input.get("path", "")

    if not path or not os.path.isfile(path):
        sys.exit(0)

    # Skip data/config formats — DRY rules don't apply to declarative JSON,
    # and tools like CMakePresets.json inherently repeat preset shape.
    if os.path.splitext(path)[1].lower() in {".json", ".yaml", ".yml", ".toml"}:
        sys.exit(0)

    # Skip prose: markdown/rst/text docs and implementation plans. These can
    # legitimately be long, repeat boilerplate (e.g. per-task `git add`/`git
    # commit` blocks in plans), and don't benefit from code-smell heuristics.
    if os.path.splitext(path)[1].lower() in {".md", ".rst", ".txt", ".adoc"}:
        sys.exit(0)

    try:
        violations = analyze_file(path)
    except Exception:
        sys.exit(0)  # fail open — never block Claude due to hook crash

    if violations:
        report = "\n".join(f"  • {v}" for v in violations)
        result = {
            "decision": "block",
            "reason": (
                f"Code quality violations in {path}:\n{report}\n\n"
                "Fix all violations before proceeding."
            ),
        }
        print(json.dumps(result))
        sys.exit(0)

    # No violations — allow the tool call
    sys.exit(0)


if __name__ == "__main__":
    main()
