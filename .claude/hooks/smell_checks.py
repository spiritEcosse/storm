"""Routes files to language-specific analyzers and runs file-level checks.

Per-project lint exemptions live in a `.lint-skip` file at the project root.
The hook walks up from the file being analyzed until it finds a `.lint-skip`
or hits the filesystem root. Each line in `.lint-skip` is
`<glob>: <tag-list>`, where tag-list is comma- or space-separated and uses
the same vocabulary as the check itself (`file-size`, `duplicate`,
`complexity`, `length`, `parameters`, `all`).

Globs are resolved relative to `.lint-skip`'s directory and use
`pathlib.Path.glob` semantics. Unknown tags and malformed lines are silently
ignored — fail open, never block contributors over hook misconfiguration.
"""

import os
import sys
from pathlib import Path
from smell_types import (
    MAX_FILE_LINES, DUPLICATE_MIN_LINES, DUPLICATE_MIN_OCCURRENCES,
    FIXES, SKIP_DIRS, MAX_COMPLEXITY, MAX_FUNCTION_LINES, MAX_PARAMETERS,
)

CPP_EXTENSIONS = {".cpp", ".cppm", ".hpp", ".h", ".cc", ".cxx"}
PYTHON_EXTENSIONS = {".py"}
LIZARD_EXTENSIONS = CPP_EXTENSIONS | {".js", ".ts", ".java", ".go", ".rs", ".cs"}

# Tag names accepted in `.lint-skip` entries.
_KNOWN_TAGS = {
    "file-size",
    "duplicate",
    "complexity",
    "length",
    "parameters",
    "all",
}

# Maps each tag to the keyword that appears in the violation message
# produced by the corresponding check.
_VIOLATION_PREFIX = {
    "file-size": "FILE SIZE:",
    "duplicate": "DUPLICATE:",
    "complexity": "COMPLEXITY:",
    "length": "LENGTH:",
    "parameters": "PARAMETERS:",
}

# Within-process cache: skip-file directory -> {absolute_path: tag_set}
_LINT_SKIP_CACHE: dict[Path, dict[Path, set[str]]] = {}


def _in_skip_dir(path: str) -> bool:
    parts = set(Path(path).parts)
    return bool(parts & SKIP_DIRS)


def _read_lines(path: str) -> list[str] | None:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.readlines()
    except OSError:
        return None


def _find_lint_skip(start: Path) -> Path | None:
    """Walk up from `start.parent` until `.lint-skip` is found or `/` is hit."""
    current = start.parent if start.is_file() else start
    try:
        current = current.resolve()
    except OSError:
        return None
    while True:
        candidate = current / ".lint-skip"
        if candidate.is_file():
            return candidate
        if current.parent == current:
            return None
        current = current.parent


def _parse_tags(raw: str) -> set[str]:
    """Comma- or space-separated tag list. Unknown tags are dropped."""
    tags = {t.strip().lower() for t in raw.replace(",", " ").split() if t.strip()}
    tags = tags & _KNOWN_TAGS
    if "all" in tags:
        return {t for t in _KNOWN_TAGS if t != "all"}
    return tags


def _load_lint_skip(skip_path: Path) -> dict[Path, set[str]]:
    """Parse `.lint-skip` into {absolute_path: tag_set}. Caches per skip_path."""
    if skip_path in _LINT_SKIP_CACHE:
        return _LINT_SKIP_CACHE[skip_path]
    result: dict[Path, set[str]] = {}
    root = skip_path.parent
    try:
        text = skip_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        _LINT_SKIP_CACHE[skip_path] = result
        return result
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            continue
        glob_part, _, tag_part = line.partition(":")
        glob = glob_part.strip()
        tags = _parse_tags(tag_part)
        if not glob or not tags:
            continue
        try:
            for match in root.glob(glob):
                try:
                    resolved = match.resolve()
                except OSError:
                    continue
                result.setdefault(resolved, set()).update(tags)
        except (ValueError, OSError):
            continue
    _LINT_SKIP_CACHE[skip_path] = result
    return result


def _excludes_for(file_path: str) -> set[str]:
    """Return the tag set suppressed for `file_path` according to `.lint-skip`."""
    try:
        target = Path(file_path).resolve()
    except OSError:
        return set()
    skip = _find_lint_skip(target)
    if skip is None:
        return set()
    mapping = _load_lint_skip(skip)
    return mapping.get(target, set())


def _filter_excluded(violations: list[str], excludes: set[str]) -> list[str]:
    if not excludes:
        return violations
    suppressed_prefixes = tuple(
        _VIOLATION_PREFIX[tag] for tag in excludes if tag in _VIOLATION_PREFIX
    )
    if not suppressed_prefixes:
        return violations
    return [v for v in violations if not v.startswith(suppressed_prefixes)]


def check_file_size(path: str, lines: list[str]) -> list[str]:
    if len(lines) > MAX_FILE_LINES:
        return [
            f"FILE SIZE: {path} has {len(lines)} lines (limit {MAX_FILE_LINES}). "
            f"{FIXES['file_size']}"
        ]
    return []


def check_duplicates(path: str, lines: list[str]) -> list[str]:
    """Sliding-window duplicate block detection."""
    violations = []
    stripped = [ln.strip() for ln in lines]
    window = DUPLICATE_MIN_LINES
    seen: dict[tuple[str, ...], list[int]] = {}

    for i in range(len(stripped) - window + 1):
        block = tuple(stripped[i : i + window])
        code_lines = [l for l in block if l and not l.startswith(("//", "#", "/*", "*"))]
        if len(code_lines) < window // 2:
            continue
        seen.setdefault(block, []).append(i + 1)

    for block, starts in seen.items():
        if len(starts) >= DUPLICATE_MIN_OCCURRENCES:
            locations = ", ".join(f"line {s}" for s in starts)
            violations.append(
                f"DUPLICATE: {window}+ identical lines at {locations} in {path}. "
                f"{FIXES['duplicates']}"
            )
    return violations


def _check_with_lizard(path: str) -> list[str]:
    try:
        import lizard
    except ImportError:
        return []

    violations = []
    try:
        result = lizard.analyze_file(path)
    except Exception:
        return []

    for fn in result.function_list:
        if fn.cyclomatic_complexity > MAX_COMPLEXITY:
            violations.append(
                f"COMPLEXITY: {fn.name}() at {path}:{fn.start_line} — "
                f"complexity {fn.cyclomatic_complexity} (limit {MAX_COMPLEXITY}). "
                f"{FIXES['complexity']}"
            )
        if fn.length > MAX_FUNCTION_LINES:
            violations.append(
                f"LENGTH: {fn.name}() at {path}:{fn.start_line} — "
                f"{fn.length} lines (limit {MAX_FUNCTION_LINES}). "
                f"{FIXES['length']}"
            )
        params = fn.parameter_count
        if params > MAX_PARAMETERS:
            violations.append(
                f"PARAMETERS: {fn.name}() at {path}:{fn.start_line} — "
                f"{params} parameters (limit {MAX_PARAMETERS}). "
                f"{FIXES['parameters']}"
            )
    return violations


def analyze_file(path: str) -> list[str]:
    if _in_skip_dir(path):
        return []

    ext = Path(path).suffix.lower()
    lines = _read_lines(path)
    if lines is None:
        return []

    excludes = _excludes_for(path)
    # Short-circuit if every check is suppressed.
    if excludes >= (_KNOWN_TAGS - {"all"}):
        return []

    violations: list[str] = []
    violations += check_file_size(path, lines)
    violations += check_duplicates(path, lines)

    if ext in LIZARD_EXTENSIONS:
        violations += _check_with_lizard(path)

    return _filter_excluded(violations, excludes)
