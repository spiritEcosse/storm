"""Tests for smell_checks.py — verifies .lint-skip file behavior.

These tests run against the CURRENT smell_checks.py module. Before Task 3
implements the .lint-skip reader, all 5 tests fail because the
.lint-skip mechanism does not exist yet.
"""

import importlib
import sys
from pathlib import Path

import pytest  # noqa: F401  # imported so `python3 -m pytest` discovery picks the file up via the pytest plugin chain

# Make smell_checks importable from this test file.
sys.path.insert(0, str(Path(__file__).parent))


def _write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def _big_file_with_duplicate(line_count: int) -> str:
    """Build a file that triggers both FILE SIZE and DUPLICATE violations.

    Produces exactly `line_count` newline-terminated lines:
        3 header + 6 block + (line_count - 15) filler + 6 block = line_count.
    """
    block = "\n".join(f"int dup_line_{i}() {{ return {i}; }}" for i in range(6))
    filler = "\n".join(f"int filler_{i}() {{ return {i}; }}" for i in range(line_count - 15))
    return f"// header\n// header\n// header\n{block}\n{filler}\n{block}\n"


def _setup_and_analyze(repo: Path, rel_target: str, skip_line: str | None) -> list[str]:
    """Write optional .lint-skip + target, reload module, return violations.

    Contract: smell_checks.analyze_file walks up from the target path to find
    .lint-skip — fixture relies on tmp_path being the discovery root.
    """
    if skip_line is not None:
        _write_file(repo / ".lint-skip", skip_line)
    target = repo / rel_target
    _write_file(target, _big_file_with_duplicate(700))
    import smell_checks
    # reload to drop any module-level .lint-skip cache from previous tests
    importlib.reload(smell_checks)
    return smell_checks.analyze_file(str(target))


def _split_hits(violations: list[str]) -> tuple[list[str], list[str]]:
    fs = [v for v in violations if v.startswith("FILE SIZE:")]
    dup = [v for v in violations if v.startswith("DUPLICATE:")]
    return fs, dup


def test_glob_match_suppresses_only_listed_tags(tmp_path):
    """A file matched by a .lint-skip glob has its listed tags suppressed,
    but other tags still report."""
    violations = _setup_and_analyze(tmp_path, "src/big.cppm", "src/big.cppm: file-size\n")
    fs, dup = _split_hits(violations)
    assert fs == [], f"expected FILE SIZE suppressed, got: {fs}"
    assert len(dup) >= 1, f"expected DUPLICATE reported, got: {dup}"


def test_no_glob_match_reports_all_violations(tmp_path):
    """A file NOT listed in .lint-skip has all its violations reported."""
    violations = _setup_and_analyze(tmp_path, "src/other.cppm", "src/big.cppm: file-size\n")
    fs, dup = _split_hits(violations)
    assert fs, violations
    assert dup, violations


def test_missing_lint_skip_reports_all_violations(tmp_path):
    """When .lint-skip is not found anywhere up the tree, no suppression."""
    violations = _setup_and_analyze(tmp_path, "src/big.cppm", None)
    fs, dup = _split_hits(violations)
    assert fs, violations
    assert dup, violations


def test_all_tag_expands_to_every_check(tmp_path):
    """The 'all' tag suppresses every check (file-size, duplicate, ...)."""
    violations = _setup_and_analyze(tmp_path, "src/everything.cppm", "src/everything.cppm: all\n")
    fs, dup = _split_hits(violations)
    assert fs == [], fs
    assert dup == [], dup


def test_unknown_tag_silently_ignored(tmp_path):
    """A typo in the tag list does not silently enable suppression."""
    # 'filesize' (no hyphen) is unknown — must NOT suppress file-size.
    violations = _setup_and_analyze(tmp_path, "src/typo.cppm", "src/typo.cppm: filesize\n")
    fs, _ = _split_hits(violations)
    assert fs, f"unknown tag 'filesize' should not suppress; got: {violations}"
