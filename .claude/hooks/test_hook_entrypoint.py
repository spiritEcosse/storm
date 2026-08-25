"""Characterization tests for check-complexity.py — the PostToolUse hook
script wired up in ~/.claude/settings.json.

The hook is a script (hyphen in its filename), so we drive it as a
subprocess via stdin, mirroring how Claude Code invokes it. Each test
pins down one branch in main():
  - stdin JSON parsing & fail-open on bad JSON
  - tool_input.file_path / tool_input.path fallback
  - missing/nonexistent path -> exit 0
  - skip-extension shortlists (.md/.rst/.txt/.adoc, .json/.yaml/.yml/.toml)
  - analyze_file exception -> fail open
  - violation -> stdout JSON {"decision":"block","reason":...}
  - clean file -> no stdout
"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

HOOK = Path(__file__).parent / "check-complexity.py"


def _run(stdin: str, env: dict | None = None) -> tuple[int, str, str]:
    proc = subprocess.run(
        [sys.executable, str(HOOK)],
        input=stdin,
        capture_output=True,
        text=True,
        env=env,
    )
    return proc.returncode, proc.stdout, proc.stderr


def _event(path: str | Path, key: str = "file_path") -> str:
    return json.dumps({"tool_input": {key: str(path)}})


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


# ---------- block envelope ----------

def test_clean_file_no_block(tmp_path):
    target = tmp_path / "clean.py"
    _write(target, "def add(a, b):\n    return a + b\n")
    rc, out, _ = _run(_event(target))
    assert rc == 0
    assert out == ""


def test_violation_emits_block_json(tmp_path):
    target = tmp_path / "huge.py"
    _write(target, "x = 1\n" * 700)
    rc, out, _ = _run(_event(target))
    assert rc == 0, out
    payload = json.loads(out)
    assert payload["decision"] == "block"
    assert "FILE SIZE:" in payload["reason"]
    assert str(target) in payload["reason"]


# ---------- skip-extension shortlists ----------

def _assert_skipped(tmp_path: Path, ext: str) -> None:
    target = tmp_path / f"huge{ext}"
    _write(target, "line\n" * 2000)
    rc, out, _ = _run(_event(target))
    assert rc == 0
    assert out == ""


@pytest.mark.parametrize("ext", [".md", ".rst", ".txt", ".adoc"])
def test_prose_extensions_never_block(tmp_path, ext):
    _assert_skipped(tmp_path, ext)


@pytest.mark.parametrize("ext", [".json", ".yaml", ".yml", ".toml"])
def test_data_extensions_never_block(tmp_path, ext):
    _assert_skipped(tmp_path, ext)


# ---------- fail-open paths ----------

def test_bad_json_fails_open():
    rc, out, _ = _run("not json at all")
    assert rc == 0
    assert out == ""


def test_empty_stdin_fails_open():
    rc, out, _ = _run("")
    assert rc == 0
    assert out == ""


def test_missing_tool_input_fails_open():
    rc, out, _ = _run(json.dumps({}))
    assert rc == 0
    assert out == ""


def test_missing_path_field_fails_open():
    rc, out, _ = _run(json.dumps({"tool_input": {}}))
    assert rc == 0
    assert out == ""


def test_nonexistent_file_fails_open(tmp_path):
    rc, out, _ = _run(_event(tmp_path / "does_not_exist.py"))
    assert rc == 0
    assert out == ""


def test_analyze_crash_fails_open(tmp_path, monkeypatch, capsys):
    # Load check-complexity.py as a module (the hyphen prevents normal
    # import) so we can monkey-patch smell_checks.analyze_file in-process
    # and exercise the try/except around analyze_file().
    import importlib.util
    import io

    spec = importlib.util.spec_from_file_location(
        "_hook_under_test", Path(__file__).parent / "check-complexity.py"
    )
    hook = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hook)

    target = tmp_path / "f.py"
    _write(target, "x = 1\n")

    def boom(_path):
        raise RuntimeError("boom")

    monkeypatch.setattr(hook, "analyze_file", boom)
    monkeypatch.setattr("sys.stdin", io.StringIO(_event(target)))

    with pytest.raises(SystemExit) as exc:
        hook.main()
    assert exc.value.code in (0, None)
    captured = capsys.readouterr()
    assert captured.out == ""


# ---------- path-field fallback ----------

def test_path_field_fallback(tmp_path):
    # check-complexity.py prefers tool_input.file_path but falls back to
    # tool_input.path when file_path is missing.
    target = tmp_path / "big.py"
    _write(target, "x = 1\n" * 700)
    rc, out, _ = _run(_event(target, key="path"))
    assert rc == 0
    payload = json.loads(out)
    assert payload["decision"] == "block"
    assert "FILE SIZE:" in payload["reason"]


def test_file_path_takes_precedence_over_path(tmp_path):
    big = tmp_path / "big.py"
    _write(big, "x = 1\n" * 700)
    small = tmp_path / "small.py"
    _write(small, "x = 1\n")
    # Both keys present — file_path should win, so we expect a block (big).
    event = json.dumps({"tool_input": {"file_path": str(big), "path": str(small)}})
    rc, out, _ = _run(event)
    assert rc == 0
    payload = json.loads(out)
    assert payload["decision"] == "block"
    assert str(big) in payload["reason"]
