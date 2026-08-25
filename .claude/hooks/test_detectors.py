"""Characterization tests for the smell detectors in smell_checks.py.

These pin down the current behavior of:
  - check_file_size
  - check_duplicates
  - _check_with_lizard (via analyze_file, since it's module-private)
  - _in_skip_dir routing (via analyze_file)

The tests assert what the code does today, so future changes that alter
behavior will surface as failures rather than silent regressions.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))

import smell_checks
from smell_types import (
    MAX_FILE_LINES,
    DUPLICATE_MIN_LINES,
    MAX_COMPLEXITY,
    MAX_FUNCTION_LINES,
    MAX_PARAMETERS,
)


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


# ---------- check_file_size ----------

def test_file_size_under_limit_no_violation():
    lines = ["x = 1\n"] * (MAX_FILE_LINES - 1)
    assert smell_checks.check_file_size("any.py", lines) == []


def test_file_size_at_limit_no_violation():
    lines = ["x = 1\n"] * MAX_FILE_LINES
    assert smell_checks.check_file_size("any.py", lines) == []


def test_file_size_one_over_limit_violates():
    lines = ["x = 1\n"] * (MAX_FILE_LINES + 1)
    out = smell_checks.check_file_size("any.py", lines)
    assert len(out) == 1
    assert out[0].startswith("FILE SIZE:")
    assert f"{MAX_FILE_LINES + 1} lines" in out[0]
    assert f"limit {MAX_FILE_LINES}" in out[0]


# ---------- check_duplicates ----------

def _code_block(prefix: str, n: int) -> str:
    """n distinct code lines, each unique to `prefix` so blocks across
    prefixes are not duplicates of each other."""
    return "\n".join(f"{prefix}_stmt_{i} = call_{i}()" for i in range(n))


def test_duplicates_two_identical_six_line_blocks_violate():
    block = _code_block("dup", DUPLICATE_MIN_LINES)
    content = f"// header line A\n// header line B\n{block}\n// middle\n{block}\n"
    lines = content.splitlines(keepends=True)
    out = smell_checks.check_duplicates("any.py", lines)
    assert any(v.startswith("DUPLICATE:") for v in out), out


def test_duplicates_single_occurrence_no_violation():
    block = _code_block("once", DUPLICATE_MIN_LINES)
    lines = (block + "\n").splitlines(keepends=True)
    out = smell_checks.check_duplicates("any.py", lines)
    assert out == []


def test_duplicates_blocks_shorter_than_window_no_violation():
    short = _code_block("short", DUPLICATE_MIN_LINES - 1)
    content = f"{short}\n// sep\n{short}\n"
    lines = content.splitlines(keepends=True)
    out = smell_checks.check_duplicates("any.py", lines)
    assert out == []


def test_duplicates_comment_only_blocks_ignored():
    # Block of 6 comment lines repeated — must NOT trigger because the
    # detector filters out comment-prefixed lines from the code-line count.
    comment_block = "\n".join(f"// comment_{i}" for i in range(DUPLICATE_MIN_LINES))
    content = f"{comment_block}\nint x = 1;\n{comment_block}\n"
    lines = content.splitlines(keepends=True)
    out = smell_checks.check_duplicates("any.py", lines)
    assert out == []


# ---------- _check_with_lizard (via analyze_file on a .cpp fixture) ----------

def _analyze_cpp(tmp_path: Path, name: str, body: str) -> list[str]:
    target = tmp_path / name
    _write(target, body)
    return smell_checks.analyze_file(str(target))


def test_lizard_clean_function_no_violation(tmp_path):
    body = "int add(int a, int b) { return a + b; }\n"
    out = _analyze_cpp(tmp_path, "clean.cpp", body)
    assert out == [], out


def test_lizard_high_complexity_violates(tmp_path):
    # 12 if-branches -> cyclomatic complexity > MAX_COMPLEXITY
    branches = "\n".join(f"  if (a == {i}) return {i};" for i in range(MAX_COMPLEXITY + 2))
    body = f"int branchy(int a) {{\n{branches}\n  return -1;\n}}\n"
    out = _analyze_cpp(tmp_path, "branchy.cpp", body)
    assert any(v.startswith("COMPLEXITY:") for v in out), out


def test_lizard_long_function_violates(tmp_path):
    padding = "\n".join(f"  a += {i};" for i in range(MAX_FUNCTION_LINES + 5))
    body = f"int long_fn(int a) {{\n{padding}\n  return a;\n}}\n"
    out = _analyze_cpp(tmp_path, "long.cpp", body)
    assert any(v.startswith("LENGTH:") for v in out), out


def test_lizard_too_many_parameters_violates(tmp_path):
    params = ", ".join(f"int p{i}" for i in range(MAX_PARAMETERS + 1))
    body = f"int many_params({params}) {{ return p0; }}\n"
    out = _analyze_cpp(tmp_path, "wide.cpp", body)
    assert any(v.startswith("PARAMETERS:") for v in out), out


def test_lizard_skipped_for_non_lizard_extension(tmp_path):
    # .py is intentionally NOT in LIZARD_EXTENSIONS — even a function that
    # would otherwise violate must produce no lizard-sourced violations.
    body = "def f(" + ", ".join(f"p{i}" for i in range(MAX_PARAMETERS + 1)) + "): return p0\n"
    target = tmp_path / "wide.py"
    _write(target, body)
    out = smell_checks.analyze_file(str(target))
    assert not any(v.startswith(("COMPLEXITY:", "LENGTH:", "PARAMETERS:")) for v in out), out


# ---------- _in_skip_dir (via analyze_file) ----------

@pytest.mark.parametrize("skip_dir", ["node_modules", "build", "__pycache__", "dist"])
def test_skip_dir_suppresses_all_violations(tmp_path, skip_dir):
    target = tmp_path / skip_dir / "huge.py"
    _write(target, "x = 1\n" * (MAX_FILE_LINES + 50))
    out = smell_checks.analyze_file(str(target))
    assert out == [], out
