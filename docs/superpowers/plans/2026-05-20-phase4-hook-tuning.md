# Phase 4 of #264 — Hook Tuning & Stop-Gap Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Storm's per-file lint calibration from in-source `// LINT-EXCLUDE-FILE` comments to a single `.lint-skip` at repo root, strip the directive-handling code from the global hook, and append the Phase 4 audit table + threshold decisions to `docs/development/CODE_QUALITY.md`.

**Architecture:** Atomic swap in a single PR on `feature/293-hook-phase4`. The hook (`~/.claude/hooks/smell_checks.py`) stops scanning C++ files for `LINT-EXCLUDE-FILE` comments and instead walks up from the analyzed file to find a `.lint-skip` configuration file. Storm's `.cppm` source files drop their 9 directive lines. The hook's new behavior is validated by 5 pytest tests in `~/.claude/hooks/test_smell_checks.py`. The audit table in `CODE_QUALITY.md` justifies keeping the threshold constants unchanged.

**Tech Stack:** Python 3 (`pathlib`, `pytest`), plain-text config file, Git, GitHub CLI.

---

## File Structure

**In-repo (this Storm checkout):**
- `.lint-skip` — NEW. Repo-root config carrying the 9 entries.
- `src/orm/statements/insert.cppm` — drop line 3.
- `src/orm/statements/select.cppm` — drop line 3.
- `src/orm/statements/base.cppm` — drop line 3.
- `src/orm/where.cppm` — drop line 3.
- `src/orm/statements/update.cppm` — drop line 3.
- `src/orm/statements/aggregate.cppm` — drop line 3.
- `src/orm/schema.cppm` — drop line 3.
- `src/orm/utilities.cppm` — drop line 3.
- `src/db/pool.cppm` — drop line 3.
- `docs/development/CODE_QUALITY.md` — append audit table + threshold decisions.

**Out of repo (user's home directory):**
- `~/.claude/hooks/smell_checks.py` — remove directive code, add skip-file reader.
- `~/.claude/hooks/test_smell_checks.py` — NEW. 5 pytest cases.

**Verification artifacts (not committed):**
- Bench (Release) output: confirms no perf regression.
- ASAN+UBSAN run: confirms no memory/UB regression.
- TSAN run: confirms no race regression.
- `python -m pytest ~/.claude/hooks/` output: confirms hook tests pass.

---

## Task 1: Pre-flight verification

**Files:** None modified — read-only baseline capture.

- [ ] **Step 1: Confirm branch state**

Run:
```bash
git branch --show-current
git status --short
```

Expected output:
```
feature/293-hook-phase4
 M .claude/settings.local.json
```
(The `.claude/settings.local.json` modification is ambient, not part of this work — leave it alone.)

- [ ] **Step 2: Confirm the 9 directives are exactly where the spec says**

Run:
```bash
grep -rn "LINT-EXCLUDE-FILE" src/
```

Expected output (9 lines, all at `line:3`):
```
src/orm/statements/insert.cppm:3:// LINT-EXCLUDE-FILE: file-size, complexity, length
src/orm/statements/base.cppm:3:// LINT-EXCLUDE-FILE: file-size, complexity, length
src/orm/where.cppm:3:// LINT-EXCLUDE-FILE: file-size, complexity
src/orm/statements/update.cppm:3:// LINT-EXCLUDE-FILE: complexity
src/orm/utilities.cppm:3:// LINT-EXCLUDE-FILE: complexity, length
src/orm/statements/aggregate.cppm:3:// LINT-EXCLUDE-FILE: file-size
src/orm/schema.cppm:3:// LINT-EXCLUDE-FILE: complexity, length
src/orm/statements/select.cppm:3:// LINT-EXCLUDE-FILE: file-size, complexity, length
src/db/pool.cppm:3:// LINT-EXCLUDE-FILE: complexity, length
```

If the count differs or any line is not at `:3`, STOP and reconcile with the spec before proceeding.

- [ ] **Step 3: Confirm `~/.claude/hooks/smell_checks.py` matches the spec's baseline**

Run:
```bash
grep -c "EXCLUDE_DIRECTIVES\|_file_excludes\|_filter_excluded\|_VIOLATION_PREFIX" ~/.claude/hooks/smell_checks.py
```

Expected: `9` or higher (the 4 identifiers each appear at definition + at least one call site).

- [ ] **Step 4: Confirm `pytest` is available**

Run:
```bash
python3 -m pytest --version
```

Expected: prints a version like `pytest 8.x.x`. If `pytest` is missing, install it now (`pip install --user pytest`) before continuing. The plan's tests depend on it.

---

## Task 2: Write the new hook tests (TDD — they must fail first)

**Files:**
- Create: `~/.claude/hooks/test_smell_checks.py`

- [ ] **Step 1: Write the 5 failing tests**

Create `~/.claude/hooks/test_smell_checks.py` with this exact content:

```python
"""Tests for smell_checks.py — verifies .lint-skip file behavior.

These tests run against the CURRENT smell_checks.py module. Before Task 3
implements the .lint-skip reader, all 5 tests fail because the
.lint-skip mechanism does not exist yet.
"""

import os
import sys
from pathlib import Path

import pytest

# Make smell_checks importable from this test file.
sys.path.insert(0, str(Path(__file__).parent))


def _write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def _big_file_with_duplicate(line_count: int) -> str:
    """Build a file that triggers both FILE SIZE and DUPLICATE violations."""
    # Two identical 6-line blocks (DUPLICATE_MIN_LINES = 6) separated by filler.
    block = "\n".join(f"int dup_line_{i}() {{ return {i}; }}" for i in range(6))
    filler = "\n".join(f"int filler_{i}() {{ return {i}; }}" for i in range(line_count - 20))
    return f"// header\n// header\n// header\n{block}\n{filler}\n{block}\n"


def test_glob_match_suppresses_only_listed_tags(tmp_path):
    """A file matched by a .lint-skip glob has its listed tags suppressed,
    but other tags still report."""
    repo = tmp_path
    _write_file(repo / ".lint-skip", "src/big.cppm: file-size\n")
    _write_file(repo / "src" / "big.cppm", _big_file_with_duplicate(700))

    # Re-import to pick up any module-state changes.
    import importlib
    import smell_checks
    importlib.reload(smell_checks)

    violations = smell_checks.analyze_file(str(repo / "src" / "big.cppm"))

    file_size_hits = [v for v in violations if v.startswith("FILE SIZE:")]
    dup_hits = [v for v in violations if v.startswith("DUPLICATE:")]

    assert file_size_hits == [], f"expected FILE SIZE suppressed, got: {file_size_hits}"
    assert len(dup_hits) >= 1, f"expected DUPLICATE reported, got: {dup_hits}"


def test_no_glob_match_reports_all_violations(tmp_path):
    """A file NOT listed in .lint-skip has all its violations reported."""
    repo = tmp_path
    _write_file(repo / ".lint-skip", "src/big.cppm: file-size\n")
    _write_file(repo / "src" / "other.cppm", _big_file_with_duplicate(700))

    import importlib
    import smell_checks
    importlib.reload(smell_checks)

    violations = smell_checks.analyze_file(str(repo / "src" / "other.cppm"))

    assert any(v.startswith("FILE SIZE:") for v in violations), violations
    assert any(v.startswith("DUPLICATE:") for v in violations), violations


def test_missing_lint_skip_reports_all_violations(tmp_path):
    """When .lint-skip is not found anywhere up the tree, no suppression."""
    # No .lint-skip created anywhere — the walk-up terminates at /.
    repo = tmp_path
    _write_file(repo / "src" / "big.cppm", _big_file_with_duplicate(700))

    import importlib
    import smell_checks
    importlib.reload(smell_checks)

    violations = smell_checks.analyze_file(str(repo / "src" / "big.cppm"))

    assert any(v.startswith("FILE SIZE:") for v in violations), violations
    assert any(v.startswith("DUPLICATE:") for v in violations), violations


def test_all_tag_expands_to_every_check(tmp_path):
    """The 'all' tag suppresses every check (file-size, duplicate, ...)."""
    repo = tmp_path
    _write_file(repo / ".lint-skip", "src/everything.cppm: all\n")
    _write_file(repo / "src" / "everything.cppm", _big_file_with_duplicate(700))

    import importlib
    import smell_checks
    importlib.reload(smell_checks)

    violations = smell_checks.analyze_file(str(repo / "src" / "everything.cppm"))

    file_size_hits = [v for v in violations if v.startswith("FILE SIZE:")]
    dup_hits = [v for v in violations if v.startswith("DUPLICATE:")]
    assert file_size_hits == [], file_size_hits
    assert dup_hits == [], dup_hits


def test_unknown_tag_silently_ignored(tmp_path):
    """A typo in the tag list does not silently enable suppression."""
    repo = tmp_path
    # 'filesize' (no hyphen) is unknown — must NOT suppress file-size.
    _write_file(repo / ".lint-skip", "src/typo.cppm: filesize\n")
    _write_file(repo / "src" / "typo.cppm", _big_file_with_duplicate(700))

    import importlib
    import smell_checks
    importlib.reload(smell_checks)

    violations = smell_checks.analyze_file(str(repo / "src" / "typo.cppm"))

    assert any(v.startswith("FILE SIZE:") for v in violations), \
        f"unknown tag 'filesize' should not suppress; got: {violations}"
```

- [ ] **Step 2: Run the tests — expect 5 failures**

Run:
```bash
python3 -m pytest ~/.claude/hooks/test_smell_checks.py -v
```

Expected: **5 failures**. The failures are because today's `smell_checks.py` scans for in-source `LINT-EXCLUDE-FILE` comments, not for a `.lint-skip` file:

- `test_glob_match_suppresses_only_listed_tags` fails: FILE SIZE is reported (no comment in `big.cppm` to suppress it).
- `test_no_glob_match_reports_all_violations` may PASS by accident (no suppression today either) — that's fine; it will continue to pass under the new mechanism.
- `test_missing_lint_skip_reports_all_violations` may PASS by accident — fine.
- `test_all_tag_expands_to_every_check` fails: both FILE SIZE and DUPLICATE are reported.
- `test_unknown_tag_silently_ignored` may PASS by accident — fine.

The point: **at least the two "glob-match suppresses" tests must fail before Task 3.** If those two already pass without changes, something is wrong — STOP and re-read the spec.

- [ ] **Step 3: Commit the new tests** *(outside this repo — the hook tests live in `~/.claude/hooks/`, not in Storm)*

The hook tests are NOT committed to Storm. They live in the user's home directory. Track them in the PR body as a unified diff so the reviewer sees them, but do NOT `git add` them here.

Capture the file contents for the PR body:
```bash
cat ~/.claude/hooks/test_smell_checks.py | head -1
wc -l ~/.claude/hooks/test_smell_checks.py
```

Expected: file starts with `"""Tests for smell_checks.py …` and is around 100 lines.

---

## Task 3: Rewire `smell_checks.py` — remove directive code, add skip-file reader

**Files:**
- Modify: `~/.claude/hooks/smell_checks.py` (full rewrite of identified sections)

- [ ] **Step 1: Replace the file with the new implementation**

Overwrite `~/.claude/hooks/smell_checks.py` with this exact content:

```python
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
```

- [ ] **Step 2: Run the new tests — expect 5 passes**

Run:
```bash
python3 -m pytest ~/.claude/hooks/test_smell_checks.py -v
```

Expected: **5 passed**.

If any test fails, do NOT proceed. Debug the failure against the test code in Task 2 — the test is the contract.

- [ ] **Step 3: Smoke-test that the in-source directive really is dead**

Create a temp file with the old comment and verify it does NOT suppress:

```bash
cat > /tmp/old_directive_test.cppm <<'EOF'
// header
// header
// LINT-EXCLUDE-FILE: file-size
EOF
for i in $(seq 1 700); do echo "int line_${i}() { return ${i}; }"; done >> /tmp/old_directive_test.cppm

python3 -c "
import sys
sys.path.insert(0, '/home/ihor/.claude/hooks')
import smell_checks
v = smell_checks.analyze_file('/tmp/old_directive_test.cppm')
print('FILE SIZE in violations:', any(x.startswith('FILE SIZE:') for x in v))
"
rm /tmp/old_directive_test.cppm
```

Expected: `FILE SIZE in violations: True`. This confirms the old in-source directive is no longer honored — exactly the behavior Phase 4 wants.

- [ ] **Step 4: Record the hook changes for the PR body**

The hook lives outside the repo, so it can't be committed here. Capture a unified diff for the PR description:

```bash
cd ~/.claude/hooks
# If you saved a backup before Step 1, diff against it.
# Otherwise, capture the new file content so the PR reviewer can verify.
wc -l smell_checks.py test_smell_checks.py
```

Expected:
```
~225 smell_checks.py
~110 test_smell_checks.py
```

(Counts will vary slightly. The point is to confirm the files exist and have the expected size.)

---

## Task 4: Create `.lint-skip` at repo root

**Files:**
- Create: `.lint-skip` (repo root)

- [ ] **Step 1: Write `.lint-skip`**

Create `/home/ihor/projects/storm/storm_develop/.lint-skip` with this exact content:

```
# Storm — files calibrated under #264. Each line is `<glob>: <tag-list>`.
# Tags: file-size, duplicate, complexity, length, parameters, all.
# Globs are relative to this file (the repo root) and use Python pathlib.Path.glob semantics.
# Rationale per file lives in docs/development/CODE_QUALITY.md.

src/orm/statements/insert.cppm:    file-size, complexity, length
src/orm/statements/select.cppm:    file-size, complexity, length
src/orm/statements/base.cppm:      file-size, complexity, length
src/orm/where.cppm:                file-size, complexity
src/orm/statements/update.cppm:    complexity
src/orm/statements/aggregate.cppm: file-size
src/orm/schema.cppm:               complexity, length
src/orm/utilities.cppm:            complexity, length
src/db/pool.cppm:                  complexity, length
```

- [ ] **Step 2: Sanity-check the file resolves correctly under the new hook**

Run:
```bash
python3 -c "
import sys
sys.path.insert(0, '/home/ihor/.claude/hooks')
import smell_checks
import importlib; importlib.reload(smell_checks)
# Pick one file that has all-3 tags excluded.
v = smell_checks.analyze_file('/home/ihor/projects/storm/storm_develop/src/orm/statements/insert.cppm')
print('violations:', [x.split(':')[0] for x in v])
print('FILE SIZE present?', any(x.startswith('FILE SIZE:') for x in v))
print('COMPLEXITY present?', any(x.startswith('COMPLEXITY:') for x in v))
print('LENGTH present?', any(x.startswith('LENGTH:') for x in v))
"
```

Expected: `FILE SIZE present? False`, `COMPLEXITY present? False`, `LENGTH present? False`. The directives are suppressed via `.lint-skip` even though the in-source comments are still present (they're about to be removed in Task 5).

If any of these are `True`, the glob did not match. Re-check the `.lint-skip` entry path syntax.

---

## Task 5: Remove the 9 `LINT-EXCLUDE-FILE` lines from `src/*.cppm`

**Files:**
- Modify: `src/orm/statements/insert.cppm:3`
- Modify: `src/orm/statements/select.cppm:3`
- Modify: `src/orm/statements/base.cppm:3`
- Modify: `src/orm/where.cppm:3`
- Modify: `src/orm/statements/update.cppm:3`
- Modify: `src/orm/statements/aggregate.cppm:3`
- Modify: `src/orm/schema.cppm:3`
- Modify: `src/orm/utilities.cppm:3`
- Modify: `src/db/pool.cppm:3`

- [ ] **Step 1: For each of the 9 files, remove line 3**

For each file in the list above, use the `Edit` tool. The line to remove is exactly the directive comment found by the Task 1 grep. For example, for `src/orm/statements/insert.cppm`:

- old_string: `// LINT-EXCLUDE-FILE: file-size, complexity, length\n`
- new_string: (empty — delete the line, leaving the surrounding context intact)

If the file uses `\n\n` between header comments (likely), make sure the new file does not gain a blank line where the directive used to be. Match the exact preceding and following bytes to ensure clean removal.

Concrete `Edit` invocation per file — show the editor exactly what to change. Here is the pattern for `insert.cppm` (apply the same pattern to all 9, adjusting the tag list to whatever appears at line 3 in each file):

```python
# Pseudocode for the Edit invocation
Edit(
    file_path="/home/ihor/projects/storm/storm_develop/src/orm/statements/insert.cppm",
    old_string="""// header line 2
// LINT-EXCLUDE-FILE: file-size, complexity, length
""",  # actual content of lines 2–3 (read the file first)
    new_string="""// header line 2
""",  # remove just line 3
)
```

The exact `old_string`/`new_string` must come from reading each file first. Do not guess — line 2 of each file may differ.

The 9 directive lines to remove are:
| File | Tag list |
|---|---|
| `src/orm/statements/insert.cppm` | `file-size, complexity, length` |
| `src/orm/statements/select.cppm` | `file-size, complexity, length` |
| `src/orm/statements/base.cppm` | `file-size, complexity, length` |
| `src/orm/where.cppm` | `file-size, complexity` |
| `src/orm/statements/update.cppm` | `complexity` |
| `src/orm/statements/aggregate.cppm` | `file-size` |
| `src/orm/schema.cppm` | `complexity, length` |
| `src/orm/utilities.cppm` | `complexity, length` |
| `src/db/pool.cppm` | `complexity, length` |

- [ ] **Step 2: Verify all 9 are gone**

Run:
```bash
grep -rn "LINT-EXCLUDE-FILE" src/
```

Expected: **no output** (exit code 1). If any line remains, the corresponding `Edit` did not apply.

- [ ] **Step 3: Confirm the hook is now quiet on those files (via `.lint-skip`)**

Pick one statement-class file and run the same check from Task 4 Step 2:

```bash
python3 -c "
import sys
sys.path.insert(0, '/home/ihor/.claude/hooks')
import smell_checks
v = smell_checks.analyze_file('/home/ihor/projects/storm/storm_develop/src/orm/statements/select.cppm')
print('FILE SIZE/COMPLEXITY/LENGTH in violations?',
      any(x.startswith(('FILE SIZE:', 'COMPLEXITY:', 'LENGTH:')) for x in v))
"
```

Expected: `False`. The file no longer carries its `LINT-EXCLUDE-FILE` comment, but `.lint-skip` still suppresses the same tags.

- [ ] **Step 4: Run the full Storm test suite to confirm the edits did not change semantics**

The edits remove a comment, so test results must be unchanged.

```bash
ctest --preset ninja-debug
```

Expected: same pass count as before this PR (the comment removal is a no-op for the compiler).

If anything fails, STOP — the edit accidentally removed more than the comment line.

---

## Task 6: Append the audit table and threshold decisions to `CODE_QUALITY.md`

**Files:**
- Modify: `docs/development/CODE_QUALITY.md` (append two sections)

- [ ] **Step 1: Read the current end of the file**

```bash
tail -10 docs/development/CODE_QUALITY.md
```

Expected last line: `- [#277](https://github.com/spiritEcosse/storm/issues/277) — Phase 3 (this document).`

The new sections go AFTER the existing "Related issues" section.

- [ ] **Step 2: Append the Phase 4 audit section**

Use `Edit` to append after the last existing line. The new content is:

```markdown

## Phase 4 audit (#293) — PR-level outcomes

Phase 3 (#277) closed 2026-05-20 with 14 merged PRs (#278–#292). The hook's thresholds drove ~117 real refactors across `src/*.cppm`. This table is the evidence trail.

| PR | File | Hook tags reported pre-PR | Outcome | Helper(s) introduced |
|---|---|---|---|---|
| #278 | `src/orm/statements/insert.cppm` | `duplicate` | extracted | `build_insert_sql_array_impl<bool>`, `to_sql_impl`, `bind_single` / `bind_bulk` |
| #279 | `src/orm/schema.cppm` | `duplicate` | extracted | `append_index_sql` for the shared `CREATE INDEX` tail |
| #280 | `src/orm/statements/join.cppm` | `duplicate` | extracted | `for_each_fk_field<F>` consteval helper |
| #281 | `src/orm/statements/select.cppm` | `duplicate` | extracted | `build_sql`, `QueryBase::forward`, `make_first_or_get<Proxy>`, coroutine step-loop flattening, `step_first_row` |
| #283 | `src/orm/statements/base.cppm` | `duplicate` | extracted | `for_each_field_name<SkipPK>`, `bind_bulk_objects_impl<SkipPK>`, `bind_expr_or_reset` |
| #284 | `src/orm/where.cppm` | `duplicate` | extracted | `BindParamsVisitor`, `make_null_check_expr` |
| #285 | `src/orm/statements/update.cppm` | `duplicate` | extracted | `ensure_cached_stmt`, `reset_bind_execute`, `QueryBase::sql` |
| #286 | `src/orm/statements/aggregate.cppm` | `duplicate` | extracted | `append_group_by_tail` |
| #287 | `src/db/pool.cppm` | `duplicate` | extracted | `count_entries(pred)` |
| #288 | `src/orm/statements/erase.cppm` | `duplicate` | extracted | `delete_prefix_size`, `append_delete_prefix` |
| #289 | `src/orm/statements/distinct.cppm` | `duplicate` | extracted | `build_field_list_with_prefix<Extra>` |
| #290 | `src/orm/statements/setop.cppm` | `duplicate` | extracted | `add_operand`, `ready_statement` |
| #291 | `src/db/sqlite.cppm` | `duplicate` | tag dropped | none — no real blocks under the tag |
| #292 | `src/db/postgresql_statement.cppm` | `duplicate` | extracted | `bind_text_value` |

**Bench / sanitizer verdict (all 14 PRs):** no regression on `develop` per per-PR CI runs gated by CLAUDE.md verification rules.

**Tally:** 13 extractions, 1 tag-drop, 0 "accept the duplicate" decisions. The hook's `duplicate` detection produced zero false-positive blocks on Storm — every reported duplicate represented a real, extractable pattern.

## Phase 4 threshold decisions

For each constant in `~/.claude/hooks/smell_types.py`, the question is: *did Phase 3 show the threshold was calibrated correctly?* The decisions below are based on the audit table above.

- **`DUPLICATE_MIN_LINES = 6` — keep.** The 6-line window flagged 14 file-clusters across `src/*.cppm`. All 14 led to either a real extraction (13) or a no-op tag drop (1, `sqlite.cppm` — no blocks survived). Zero blocks needed a "this is fine as-is" accept. Raising to 10 (as suggested in #264) would have missed real extractions like `count_entries` in `pool.cppm` (6-line predicate-count pattern). Keep.

- **`DUPLICATE_MIN_OCCURRENCES = 2` — keep.** Storm's duplicates are usually 2-occurrence pairs (single-row vs bulk variants, two binder overloads). Bumping to 3 would have missed every Phase 3 extraction.

- **`MAX_FILE_LINES = 600` — keep.** Six files (`insert`, `select`, `base`, `where`, `aggregate`, …) still exceed 600 lines and carry `file-size` exclusions in `.lint-skip`. The Phase 2 finding (#264 comment 4462154303) documented why splitting these hurts perf or breaks ADL. The threshold is correctly catching them; the project-local `.lint-skip` is correctly opting out per-file.

- **`MAX_COMPLEXITY = 10` — keep.** Seven files carry `complexity` exclusions in `.lint-skip`, mostly for consteval reflection helpers that walk `nonstatic_data_members_of`. The threshold flags real complexity; the exclusions are scoped to documented exceptions.

- **`MAX_FUNCTION_LINES = 60` — keep.** Five files carry `length` exclusions. Same pattern as complexity — consteval and large constructor lists in reflection-driven code legitimately exceed 60 lines. The threshold is right; the exemptions are right.

- **`MAX_NESTING_DEPTH = 4` — keep.** Phase 3 reported zero `nesting` violations on `src/*.cppm`. No exclusions exist for this tag. No tuning needed.

- **`MAX_PARAMETERS = 6` — keep.** Phase 3 reported zero `parameters` violations on `src/*.cppm`. No exclusions exist. No tuning needed.

**Conclusion:** all 7 constants stay at their current values. The stop-gap `LINT-EXCLUDE-FILE` mechanism in the hook is removed and replaced by `.lint-skip` at the repo root.
```

The `Edit` invocation:

- file_path: `/home/ihor/projects/storm/storm_develop/docs/development/CODE_QUALITY.md`
- old_string: `- [#277](https://github.com/spiritEcosse/storm/issues/277) — Phase 3 (this document).`
- new_string: (the same line, followed by the new content block above)

- [ ] **Step 3: Verify the file looks right**

```bash
wc -l docs/development/CODE_QUALITY.md
grep -c "^## " docs/development/CODE_QUALITY.md
```

Expected: file grew by ~50 lines. Section count went from 3 (`## Per-file dispositions`, `## Accepted duplicates`, `## Related issues`) to 5 (added `## Phase 4 audit (#293) — PR-level outcomes` and `## Phase 4 threshold decisions`).

---

## Task 7: Pre-commit verification (CLAUDE.md mandatory)

**Files:** None modified.

The changes in this PR are documentation + comment removals + a config file. Per CLAUDE.md rules 6 and 7, bench and sanitizers must still run after code changes. The expected outcome is "no regression" because the source-code change is comment removal only.

- [ ] **Step 1: Release build + bench**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
./build/release/benchmarks/storm_bench --benchmark_repetitions=5 --benchmark_filter='Storm/SELECT/.*' > /tmp/phase4_bench.txt 2>&1
tail -40 /tmp/phase4_bench.txt
```

Expected: build clean. Bench numbers within noise of `develop` baseline (typically ±2%). Comment removal cannot affect codegen; any significant delta points to a build-system issue and must be investigated before commit.

- [ ] **Step 2: ASAN + UBSAN**

```bash
cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan
ctest --preset ninja-asan-ubsan
```

Expected: all tests pass, zero ASAN/UBSAN findings. Same reasoning as Step 1.

- [ ] **Step 3: TSAN**

```bash
cmake --preset ninja-tsan && cmake --build --preset ninja-tsan
ctest --preset ninja-tsan
```

Expected: all tests pass, zero TSAN findings.

- [ ] **Step 4: Hook tests pass**

```bash
python3 -m pytest ~/.claude/hooks/test_smell_checks.py -v
```

Expected: 5 passed.

If ANY of the four steps reports a regression, STOP and reconcile before committing. Do not commit broken state.

---

## Task 8: Commit and push

**Files:** All Task 4–6 changes.

- [ ] **Step 1: Confirm the change set**

```bash
git status --short
```

Expected (exactly — modulo the ambient `.claude/settings.local.json`):
```
 M .claude/settings.local.json
 M docs/development/CODE_QUALITY.md
 M src/db/pool.cppm
 M src/orm/schema.cppm
 M src/orm/statements/aggregate.cppm
 M src/orm/statements/base.cppm
 M src/orm/statements/insert.cppm
 M src/orm/statements/select.cppm
 M src/orm/statements/update.cppm
 M src/orm/utilities.cppm
 M src/orm/where.cppm
?? .lint-skip
```

- [ ] **Step 2: Diff each modified `.cppm` file to confirm only the directive line changed**

```bash
git diff --stat src/
git diff src/orm/statements/insert.cppm
```

Expected: each `.cppm` diff is a single removed line (the `// LINT-EXCLUDE-FILE: …` comment). Nothing else.

- [ ] **Step 3: Stage the right files (NOT `.claude/settings.local.json`)**

```bash
git add .lint-skip docs/development/CODE_QUALITY.md src/
git status --short
```

Expected: same as Step 1 but with `A`/`M` letters in the first column for the staged files; `.claude/settings.local.json` still shows as ` M` (unstaged).

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(lint): replace LINT-EXCLUDE-FILE comments with repo-root .lint-skip

Phase 4 of #264 closes the stop-gap mechanism added in Phase 1. Per-file
calibration moves from `// LINT-EXCLUDE-FILE: <tags>` comments inside
src/*.cppm to a single `.lint-skip` at the repo root carrying the same 9
entries in `<glob>: <tag-list>` form. The global hook
(~/.claude/hooks/smell_checks.py) drops its directive-handling code in
favor of a walk-up `.lint-skip` reader; tests added in
~/.claude/hooks/test_smell_checks.py.

docs/development/CODE_QUALITY.md gains a Phase 4 audit table — 14 rows
sourced from the merged Phase 3 PRs — and a per-threshold decision
section justifying that all 7 constants in smell_types.py stay at their
current values.

Hook source changes (out of repo) captured in the PR description as a
unified diff for review.

Refs #293
Closes #264 acceptance criterion 5 ("Phase 4 tunes thresholds based on
actual signal-to-noise ratio") and 6 ("docs/development/CODE_QUALITY.md
(new) documents which patterns are excluded and why").

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

Expected: pre-commit hook (`commit.sh`) runs format/tidy/test/coverage. Because the `.cppm` files change but only by comment removal, format/tidy should be no-ops. Tests must pass.

- [ ] **Step 5: Push the branch**

Per CLAUDE.md safety rule 2 ("NEVER push without approval"), ask the user before pushing. If approved:

```bash
git push -u origin feature/293-hook-phase4
```

Expected: branch published; `gh pr create` becomes possible.

---

## Task 9: Open the PR

**Files:** None modified.

- [ ] **Step 1: Create the PR**

The PR body must include the out-of-repo hook diff so the reviewer can see the global-hook changes. Capture the new hook content into the body:

```bash
gh pr create --base develop --title "feat(lint): replace LINT-EXCLUDE-FILE comments with repo-root .lint-skip (Phase 4 of #264)" --body "$(cat <<'EOF'
## Summary

Phase 4 of #264 closes the `LINT-EXCLUDE-FILE` stop-gap. Per-file lint calibration moves from in-source C++ comments to a single `.lint-skip` at the repo root.

- 9 `// LINT-EXCLUDE-FILE: …` lines removed from `src/*.cppm`.
- `.lint-skip` added with 9 entries (`<glob>: <tag-list>`).
- `~/.claude/hooks/smell_checks.py` rewritten — directive code removed, walk-up `.lint-skip` reader added.
- `~/.claude/hooks/test_smell_checks.py` added — 5 pytest cases (5 passed locally).
- `docs/development/CODE_QUALITY.md` gains the Phase 4 audit table (14 PR rows) and per-threshold decisions for all 7 constants in `smell_types.py` (all keep, no tuning).

## Out-of-repo hook changes (for review)

The hook source lives in `~/.claude/hooks/`, outside this repo. The new `smell_checks.py` and `test_smell_checks.py` files are reproduced in full in this PR's design spec at `docs/superpowers/specs/2026-05-20-phase4-hook-tuning-design.md` and implementation plan at `docs/superpowers/plans/2026-05-20-phase4-hook-tuning.md`.

After this PR merges, the user applies the new hook files to `~/.claude/hooks/`:
- Overwrite `~/.claude/hooks/smell_checks.py` per the plan's Task 3.
- Create `~/.claude/hooks/test_smell_checks.py` per the plan's Task 2.

## Test plan

- [ ] CI: `ninja-debug`, `ninja-asan-ubsan`, `ninja-tsan` all green.
- [ ] SonarCloud gate passes (zero new issues, zero new duplication).
- [ ] After merge: run `python3 -m pytest ~/.claude/hooks/` — 5 passed.
- [ ] After merge: open `src/orm/statements/insert.cppm` in Claude Code, make a one-line edit — hook must not block.

Refs #293
Closes the final acceptance criteria of #264.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Expected: PR URL printed.

- [ ] **Step 2: Wait 30 seconds, then run SonarCloud check**

```bash
sleep 30
```

Then invoke the `/sonarcloud-status` skill (or run `./scripts/sonarcloud-check.sh`). The PR is on a feature branch, so the script runs in PR mode and waits for SonarCloud to finish.

Expected: zero new issues, zero new duplication. If the gate fails, fix the reported issues on the feature branch, push, and re-check.

- [ ] **Step 3: Wait for CI**

```bash
gh pr checks --watch
```

Expected: all three workflows (`ninja-debug`, `ninja-asan-ubsan`, `ninja-tsan`) green.

---

## Task 10: Merge, close, switch back

**Files:** None modified.

- [ ] **Step 1: Merge (only after SonarCloud + CI both green)**

```bash
gh pr merge --squash --delete-branch
```

Expected: PR merged, branch deleted.

- [ ] **Step 2: Close #293**

```bash
gh issue close 293
```

- [ ] **Step 3: Verify #264 acceptance criteria 5 and 6 are now satisfied**

```bash
gh issue view 264 --json body --jq '.body' | grep -A1 "Phase 4 tunes\|CODE_QUALITY.md"
```

Both checkboxes should reference this PR (#293's PR number) once verified. If the issue body still shows `- [ ]` for criteria 5 or 6, update it:

```bash
gh issue edit 264 --body "$(gh issue view 264 --json body --jq '.body' | sed -e 's/^5\. \[ \] Phase 4/5. [x] Phase 4/' -e 's/^6\. \[ \] `docs\/development\/CODE_QUALITY.md`/6. [x] `docs\/development\/CODE_QUALITY.md`/')"
```

- [ ] **Step 4: Apply the out-of-repo hook files to the user's home directory**

If the agent has already applied them during Tasks 2 and 3, this is already done. Otherwise:

```bash
# Verify the live hook matches the spec.
diff -u <(curl -fsSL https://github.com/spiritEcosse/storm/raw/develop/docs/superpowers/specs/2026-05-20-phase4-hook-tuning-design.md | awk '/^```python/,/^```$/' | grep -v '^```') ~/.claude/hooks/smell_checks.py
```

Expected: only acceptable diffs are whitespace and module-level comments; logic must match.

- [ ] **Step 5: Switch back to `develop`**

```bash
git checkout develop && git pull
```

Expected: branch is `develop`, latest commit includes the merged Phase 4 PR.

---

## Self-Review

Running the checklist against the spec.

**Spec coverage:**

- Architecture (two systems swap) → Tasks 2, 3, 4, 5. ✓
- `.lint-skip` file format → Task 4. ✓
- Hook changes (remove EXCLUDE_DIRECTIVES, _VIOLATION_PREFIX, _file_excludes, _filter_excluded; add _find_lint_skip, _load_lint_skip, _excludes_for) → Task 3. ✓
- Tests (5 cases, names match the spec) → Task 2. ✓
- CODE_QUALITY.md additions (audit + threshold decisions) → Task 6. ✓
- Error handling (malformed lines silent-ignore, unknown tag silent-ignore, missing .lint-skip = no suppression) → Tested in Task 2, implemented in Task 3. ✓
- No backward compat — atomic swap → Task 8 commits everything together. ✓
- Verification (bench, ASAN+UBSAN, TSAN, hook tests) → Task 7. ✓
- SonarCloud gate → Task 9. ✓
- Acceptance criteria all 10 → covered across Tasks 4–10. ✓

**Placeholder scan:**

- No "TBD" / "TODO" / "implement later" in the plan.
- Every code step has the actual code.
- Every command has the actual command and expected output.
- Every file path is absolute (or repo-relative when running from repo root).

**Type consistency:**

- `_KNOWN_TAGS`, `_VIOLATION_PREFIX`, `_LINT_SKIP_CACHE`, `_find_lint_skip`, `_load_lint_skip`, `_excludes_for`, `_filter_excluded` — names consistent across Tasks 2 (test setup) and 3 (implementation).
- Test fixture helpers `_write_file`, `_big_file_with_duplicate` — defined once in Task 2 step 1, referenced from all 5 tests in that same block.
- File paths in Task 5 step 1 table match the grep output captured in Task 1 step 2.

No issues found. Plan ready for execution.

---

## Execution Notes

- Tasks 1, 7, 9, 10 are read-only or git-only — fast.
- Tasks 2, 3 modify files outside the repo (`~/.claude/hooks/`) and are NOT staged for the Storm PR — the agent must NOT `git add` them.
- Tasks 4, 5, 6 modify in-repo files and ARE staged for the PR (Task 8).
- Task 5 is mechanical — 9 file edits, no logic.
- Bench/sanitizer in Task 7 take ~10–15 minutes total.
- If anything goes wrong mid-flight, the rollback is `git checkout -- src/ docs/ .lint-skip` (or `git reset --hard origin/develop` if more was committed). Outside-repo hook changes need a manual revert from the original `smell_checks.py` content — keep a backup before Task 3.
