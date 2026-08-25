"""Edge-case tests for the .lint-skip parser in smell_checks.py.

Covers walk-up across nested directories, comment & blank lines, comma
vs space tag separators, multiple globs unioning tags onto one file,
recursive `**` glob patterns, and the lizard ImportError fallback.

Each test reloads smell_checks to drop the module-level
_LINT_SKIP_CACHE so suppressed violations from one test don't leak.
"""

import importlib
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))

import smell_checks  # noqa: E402
from smell_types import MAX_FILE_LINES  # noqa: E402


def _reload() -> None:
    importlib.reload(smell_checks)


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def _big(tmp_path: Path, rel: str) -> Path:
    target = tmp_path / rel
    _write(target, "x = 1\n" * (MAX_FILE_LINES + 50))
    return target


def _analyze(path: Path) -> list[str]:
    _reload()
    return smell_checks.analyze_file(str(path))


def _has_file_size(violations: list[str]) -> bool:
    return any(v.startswith("FILE SIZE:") for v in violations)


# ---------- walk-up discovery ----------

def test_lint_skip_in_ancestor_directory_applies(tmp_path):
    # .lint-skip sits at repo root; target is two dirs deep. Walk-up must
    # find the skip and apply it.
    _write(tmp_path / ".lint-skip", "src/nested/deep/big.py: file-size\n")
    target = _big(tmp_path, "src/nested/deep/big.py")
    out = _analyze(target)
    assert not _has_file_size(out), out


def test_nearest_lint_skip_wins(tmp_path):
    # Both root and intermediate dirs have .lint-skip. Walk-up returns the
    # FIRST one found (nearest). The root one's entry should NOT apply.
    _write(tmp_path / ".lint-skip", "src/big.py: file-size\n")
    _write(tmp_path / "src" / ".lint-skip", "# empty\n")
    target = _big(tmp_path, "src/big.py")
    out = _analyze(target)
    # Nearest .lint-skip (in src/) is empty, so file-size violation reports.
    assert _has_file_size(out), out


# ---------- comment & blank line handling ----------

def _assert_skip_suppresses_file_size(tmp_path: Path, skip_content: str) -> None:
    _write(tmp_path / ".lint-skip", skip_content)
    target = _big(tmp_path, "big.py")
    out = _analyze(target)
    assert not _has_file_size(out), out


def test_comment_and_blank_lines_ignored(tmp_path):
    _assert_skip_suppresses_file_size(
        tmp_path,
        "# this is a comment\n"
        "\n"
        "   \n"
        "# another comment: with colon should still be a comment\n"
        "big.py: file-size\n",
    )


def test_malformed_lines_silently_ignored(tmp_path):
    # No colon, empty glob, empty tags — all dropped, but a valid line
    # later in the file still applies.
    _assert_skip_suppresses_file_size(
        tmp_path,
        "no-colon-here\n"
        ": file-size\n"
        "big.py: \n"
        "big.py: file-size\n",
    )


# ---------- tag separator variants ----------

@pytest.mark.parametrize(
    "tag_part",
    [
        "file-size,duplicate",       # comma
        "file-size duplicate",       # space
        "file-size, duplicate",      # comma + space
        "file-size,  ,duplicate",    # double comma with whitespace
        "  file-size   duplicate  ", # padding
    ],
)
def test_tag_separator_variants_parse_equivalently(tmp_path, tag_part):
    _write(tmp_path / ".lint-skip", f"big.py: {tag_part}\n")
    target = _big(tmp_path, "big.py")
    out = _analyze(target)
    assert not _has_file_size(out), (tag_part, out)


# ---------- multi-glob union ----------

def test_two_globs_matching_same_file_union_tags(tmp_path):
    # Glob A suppresses file-size, glob B suppresses duplicate; the same
    # file matches both → both tags suppressed.
    content = (
        "big.py: file-size\n"
        "*.py: duplicate\n"
    )
    _write(tmp_path / ".lint-skip", content)
    # Build a file that triggers BOTH FILE SIZE and DUPLICATE.
    block = "\n".join(f"dup_stmt_{i} = call_{i}()" for i in range(6))
    filler = "\n".join(f"filler_{i} = {i}" for i in range(MAX_FILE_LINES + 50))
    body = f"{block}\n{filler}\n{block}\n"
    target = tmp_path / "big.py"
    _write(target, body)
    out = _analyze(target)
    assert not any(v.startswith(("FILE SIZE:", "DUPLICATE:")) for v in out), out


# ---------- recursive ** glob ----------

def test_recursive_double_star_matches_nested(tmp_path):
    _write(tmp_path / ".lint-skip", "src/**/*.py: file-size\n")
    target = _big(tmp_path, "src/a/b/c/deep.py")
    out = _analyze(target)
    assert not _has_file_size(out), out


def test_double_star_does_not_match_outside_pattern(tmp_path):
    # Skip only matches under src/; a file at the root must still violate.
    _write(tmp_path / ".lint-skip", "src/**/*.py: file-size\n")
    target = _big(tmp_path, "root.py")
    out = _analyze(target)
    assert _has_file_size(out), out


# ---------- lizard ImportError fallback ----------

def test_lizard_import_error_returns_empty(tmp_path, monkeypatch):
    # Simulate `import lizard` failing inside _check_with_lizard by
    # blocking the import. The function must return [] without raising,
    # so a .cpp file that would otherwise trip COMPLEXITY etc. produces
    # no lizard-sourced violations.
    _reload()

    import builtins
    real_import = builtins.__import__

    def fake_import(name, *args, **kwargs):
        if name == "lizard":
            raise ImportError("simulated: lizard not installed")
        return real_import(name, *args, **kwargs)

    # Also drop any already-imported lizard so the function's local import
    # actually re-runs through our shim.
    monkeypatch.delitem(sys.modules, "lizard", raising=False)
    monkeypatch.setattr(builtins, "__import__", fake_import)

    # Function that would normally trip COMPLEXITY (>10 branches).
    branches = "\n".join(f"  if (a == {i}) return {i};" for i in range(12))
    body = f"int branchy(int a) {{\n{branches}\n  return -1;\n}}\n"
    target = tmp_path / "branchy.cpp"
    _write(target, body)

    out = smell_checks._check_with_lizard(str(target))
    assert out == [], out
