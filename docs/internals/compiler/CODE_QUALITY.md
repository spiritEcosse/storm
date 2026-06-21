# Code Quality — hook thresholds and `.lint-skip` policy

The repo-level lint hook (`smell_checks.py`) runs lizard against every C++ source file and flags violations of the thresholds in `~/.claude/hooks/smell_types.py`. Per-file opt-outs live in `.lint-skip` at the repo root — that file is the authoritative list of accepted exemptions. This document records the policy behind those exemptions and the rationale for the threshold values themselves.

Per-PR history lives in commit messages and GitHub issues, not here.

## Policy

Two classes of files get different treatment:

1. **Statement-class files** (`base`, `insert`, `select`, `update`, `aggregate`, `where`) — single cohesive class template. `file-size` exclusions are accepted because of the Phase 2 finding ([#264 comment 4462154303](https://github.com/spiritEcosse/storm/issues/264)): splitting these across module boundaries costs ~2% perf or breaks ADL. All other tags (`duplicate`, `complexity`, `length`) must be removed by extraction.
2. **Multi-type files** (`sqlite`, `pool`, `schema`, `utilities`, `setop`, `erase`, `distinct`, `join`, `postgresql_statement`) — every tag must be removed by extraction. No `file-size` accepts unless backed by explicit bench evidence.
3. **Test fixtures** — `tests/` is out of scope for the smell hook in [#295](https://github.com/spiritEcosse/storm/issues/295). Pre-existing test-fixture boilerplate (large `test_*.cpp` files with repeated `TYPED_TEST` setup) is accepted via `.lint-skip` to keep the hook quiet on unrelated edits.

## Threshold rationale

Decisions taken in [#293](https://github.com/spiritEcosse/storm/issues/293) (Phase 4) based on the per-tag audit in [#277](https://github.com/spiritEcosse/storm/issues/277) (Phase 3) and the refactor sweep in [#295](https://github.com/spiritEcosse/storm/issues/295) (Phase 5).

- **`DUPLICATE_MIN_LINES = 6`** — keep. The 6-line window flagged 14 file-clusters in Phase 3; 13 led to real extractions, 1 to a no-op tag drop. Raising to 10 would have missed real patterns like `count_entries` in `pool.cppm`.
- **`DUPLICATE_MIN_OCCURRENCES = 2`** — keep. Storm's duplicates are usually 2-occurrence pairs (single-row vs bulk binders, two `extract_X` overloads).
- **`MAX_FILE_LINES = 600`** — keep. Statement-class files exceed this by design; Phase 2 documented why splitting hurts perf.
- **`MAX_COMPLEXITY = 10`** — keep. Reflection-heavy `if constexpr` dispatchers legitimately approach this; the storage-class refactors in Phase 5 land at exactly 10 after extraction.
- **`MAX_FUNCTION_LINES = 60`** — keep. Same reasoning as complexity — consteval reflection helpers can grow long.
- **`MAX_NESTING_DEPTH = 4`** — keep. Zero violations reported on `src/*.cppm`; no tuning needed.
- **`MAX_PARAMETERS = 6`** — keep. Zero violations reported on `src/*.cppm`; no tuning needed.

The thresholds live in `~/.claude/hooks/smell_types.py` (global per-user config), not in this repo. Any tuning would be a deliberate, reviewed change — re-run the hook against `src/` to discover new violations, then either refactor or accept via `.lint-skip` per the policy above.
