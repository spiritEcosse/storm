# Phase 4 of #264 — Hook tuning & stop-gap removal

**Issue:** [#293](https://github.com/spiritEcosse/storm/issues/293)
**Parent:** [#264](https://github.com/spiritEcosse/storm/issues/264) (Phases 1–3 closed)
**Branch:** `feature/293-hook-phase4`
**Date:** 2026-05-20

## Context

Phase 3 (#277) closed 2026-05-20 with 14 merged PRs (#278–#292). The audit data is clear:

- 13 files had `duplicate` blocks extracted into named helpers
- 1 file (`src/db/sqlite.cppm`) had its `duplicate` tag dropped — no real blocks under it
- 0 files needed the "accept the duplicate, document it" path

The thresholds in `~/.claude/hooks/smell_types.py` drove ~117 real refactors. They were calibrated correctly. `smell_checks.py` carries a self-removal note at the top of the file saying once #264's real-refactor phases finish, the `LINT-EXCLUDE-FILE` mechanism "SHOULD BE REMOVED."

Phase 4 closes that loop:

1. Validate, via a PR-level audit table, that the thresholds caught real debt rather than noise.
2. Replace the in-source `// LINT-EXCLUDE-FILE: …` directive with a single `.lint-skip` at the repo root.
3. Strip the directive-handling code from the global hook.
4. Record the threshold-keep decision in `docs/development/CODE_QUALITY.md`.

The hook stays global (in `~/.claude/hooks/`). Storm contributes only `.lint-skip` + documentation. Moving the hook into the repo was considered and rejected — it's a separate decision (contributor coverage), and Phase 4's job is to close the stop-gap, not expand the hook's role.

## Goals

- Per-file calibration moves from `.cppm` source comments to a single repo-root file.
- Global hook becomes leaner — one skip pathway, not two.
- The audit table documents *why* the thresholds were kept, so future drift can be checked against evidence.

## Non-goals

- Raising or lowering any threshold constant in `smell_types.py`. The Phase 3 outcome says they're calibrated; Phase 4 confirms and stops.
- Per-extension thresholds (`MAX_FILE_LINES_CPPM`, etc.). Not needed once Storm's surviving exclusions live in `.lint-skip`.
- Function-/block-scoped suppression (`// NOLINT(file-size)`). The surviving exclusions are whole-file by nature; finer granularity is YAGNI for now.
- Moving the hook into the repo. Separate concern, separate issue if ever pursued.

## Architecture

Two systems swap atomically in a single PR:

| Before | After |
|---|---|
| Per-file `// LINT-EXCLUDE-FILE: <tags>` comment scanned from the first 30 lines | Single `.lint-skip` file at repo root, one entry per file as `<glob>: <tag-list>` |
| `smell_checks.py` carries `EXCLUDE_DIRECTIVES`, `_VIOLATION_PREFIX`, `_file_excludes`, `_filter_excluded` + a 12-line stop-gap doc-comment | `smell_checks.py` carries `_find_lint_skip`, `_load_lint_skip`, `_excludes_for`; doc-comment removed |
| Stop-gap status — "should be removed once #264 finishes" | Standard mechanism — explicit project opt-in via repo file |

Same vocabulary (`file-size`, `duplicate`, `complexity`, `length`, `parameters`, `all`). Same suppression semantics (whole-file opt-out per tag). Only the carrier changes — project file instead of per-file C++ comment.

## `.lint-skip` file format

Plain text at repo root. One file per line. Globs use Python's `pathlib.Path.glob` semantics, resolved relative to the directory holding `.lint-skip`.

```
# Storm — files calibrated under #264. Each line is `<glob>: <tag-list>`.
# Tags: file-size, duplicate, complexity, length, parameters, all.
# Globs are relative to this file (the repo root) and use POSIX glob syntax.
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

Nine entries — exactly the 9 surviving `LINT-EXCLUDE-FILE` lines in `src/*.cppm` as of 2026-05-20.

Parsing rules:

- Lines starting with `#` are comments. Blank lines ignored.
- Whitespace around `:` and around tags is insignificant.
- Tags are comma- or space-separated.
- Unknown tags are silently ignored (matches today's directive policy — a typo doesn't enable everything).
- `all` expands to every tag.
- Malformed lines (no `:`, unparseable glob) are silently ignored. Rationale: a hook crash blocks contributors; PR review catches malformed lines.

Glob resolution:

- Each glob is resolved relative to `.lint-skip`'s directory.
- A file matches an entry iff its absolute path matches the glob (`pathlib.Path.glob` semantics).
- Multiple matching entries union their tags. (Future-proofing for category defaults like `src/**/*.cppm: file-size`; not used today.)

## Hook changes

**File:** `~/.claude/hooks/smell_checks.py`

**Remove:**

- Top-of-file stop-gap doc-comment (lines 1–12).
- `EXCLUDE_DIRECTIVES` set (constant).
- `_VIOLATION_PREFIX` dict (constant).
- `_file_excludes(lines)` function.
- `_filter_excluded(violations, excludes)` function.
- The `excludes = _file_excludes(lines)` line in `analyze_file` and its short-circuit on the "all excludes" case.

**Add:**

- Module-level cache `_LINT_SKIP_CACHE: dict[Path, dict[Path, set[str]]]` keyed by the directory containing `.lint-skip`. Within-process memoization only — the hook runs fresh per Write/Edit.
- `_find_lint_skip(start: Path) -> Path | None` — walks up from `start.parent` until it finds `.lint-skip` or reaches the filesystem root. Returns the path to the file or `None`.
- `_load_lint_skip(skip_path: Path) -> dict[Path, set[str]]` — parses the file, returns `{absolute_path: tag_set}`. Uses `pathlib.Path.glob` resolved against `skip_path.parent`. Caches per `skip_path`.
- `_excludes_for(file_path: str) -> set[str]` — finds the nearest `.lint-skip` walking up from `file_path`, loads it, returns the union of tag sets from all matching globs. Expands `all` before returning.

**Rewire `analyze_file`:** replace `excludes = _file_excludes(lines)` with `excludes = _excludes_for(path)`. The filter loop is folded inline (only one call site after the rewrite).

**Same valid-tag vocabulary, same suppression semantics, same "unknown tag silently ignored" behavior.** Net diff: roughly −30 lines from the hook source, single skip pathway.

## Tests

**New file:** `~/.claude/hooks/test_smell_checks.py`

Uses `pytest` and `tmp_path` for fixture-style temp dirs. Five test cases:

1. **`test_glob_match_suppresses_only_listed_tags`** — temp project with `.lint-skip` saying `src/big.cppm: file-size`. Write a 700-line `src/big.cppm` containing a 6-line duplicate block. Assert that `analyze_file` returns the `DUPLICATE:` violation but not the `FILE SIZE:` violation.
2. **`test_no_glob_match_reports_all_violations`** — same fixture, but analyze `src/other.cppm` (not listed in `.lint-skip`). Assert both `FILE SIZE:` and `DUPLICATE:` are reported.
3. **`test_missing_lint_skip_reports_all_violations`** — no `.lint-skip` anywhere in the parent chain. Assert no suppression, all violations reported. Confirms the "walk up to /" lookup terminates gracefully.
4. **`test_all_tag_expands_to_every_check`** — `.lint-skip` saying `src/everything.cppm: all`. File has both `FILE SIZE:` and `DUPLICATE:` violations. Assert zero violations returned.
5. **`test_unknown_tag_silently_ignored`** — `.lint-skip` saying `src/typo.cppm: filesize` (missing hyphen). File has `FILE SIZE:` violation. Assert it IS reported — a typo must not silently enable suppression.

Invoked from anywhere via `python -m pytest ~/.claude/hooks/`. Storm's `commit.sh` does not run these tests — they're hook-developer tests, not project tests.

## CODE_QUALITY.md additions

Append two sections to `docs/development/CODE_QUALITY.md`:

### § Phase 4 audit (PR-level)

A 14-row table. Columns: PR, file, hook tags reported pre-PR (`file-size`/`duplicate`/`complexity`/`length`), outcome (`extracted` / `tag-dropped`), helper(s) introduced, bench/sanitizer verdict (from per-PR CI runs — expected "no regression" because each Phase 3 PR was gated by the standard CLAUDE.md verification rules before merge).

Sourced from:
- The merged-PR commit messages of #278–#292 (already capture helper names).
- The existing per-file table at the top of `CODE_QUALITY.md` (already lists extractions).
- The Phase 2 finding in #264 comment 4462154303 (justifies the surviving `file-size, complexity, length` exclusions on statement-class files).

### § Threshold decisions

One paragraph per constant in `smell_types.py` (7 constants: `MAX_COMPLEXITY`, `MAX_FUNCTION_LINES`, `MAX_NESTING_DEPTH`, `MAX_PARAMETERS`, `MAX_FILE_LINES`, `DUPLICATE_MIN_LINES`, `DUPLICATE_MIN_OCCURRENCES`). Each paragraph:

- How many times the constant fired against `src/*.cppm` under Phase 3.
- How many fires led to a real extraction.
- How many were accepted as intentional (with a pointer to the surviving `.lint-skip` entry, if any).
- Decision: keep at current value.

The threshold-decision section is the textual companion to the "remove stop-gap" call. It justifies why no constant in `smell_types.py` needs tuning, despite the issue body of #264 suggesting `DUPLICATE_MIN_LINES` 6→10 as a candidate.

## The PR

Single PR on `feature/293-hook-phase4`. Five change clusters in one commit (or one commit per cluster, reviewer's choice — they must merge together, not separately):

| Cluster | Diff size | Notes |
|---|---|---|
| 9 `src/*.cppm` files | −9 lines (drop `// LINT-EXCLUDE-FILE: …`) | Mechanical; no behavior change because `.lint-skip` ships in the same PR. |
| `.lint-skip` at repo root | +15 lines | New file. 9 entries + 4 comment lines + 1 blank + section header. |
| `~/.claude/hooks/smell_checks.py` | net −30 lines | Remove directive code, add skip-file reader. NOT in this repo — applied to user's home dir. |
| `~/.claude/hooks/test_smell_checks.py` | +80 lines | New file. Same caveat. |
| `docs/development/CODE_QUALITY.md` | +30 lines | Audit table + threshold paragraphs. |

The two changes outside this repo (`smell_checks.py` rewrite + new test file) are captured as a unified diff in the PR body so they can be reviewed alongside the in-repo changes, then applied by the user to `~/.claude/hooks/` after merge.

## Error handling

- **Malformed `.lint-skip` line** → ignore the line, do not log, continue. Hook crashes block edits; PR review catches malformed lines.
- **Unknown tag** → ignored (matches today's directive policy — a typo doesn't silently enable everything).
- **Glob matches no files** → silently fine; `.lint-skip` may carry future-proofing entries.
- **`.lint-skip` not found while walking up to `/`** → no suppression. Same behavior as a project without the file.
- **`.lint-skip` is readable but empty** → no suppression. Treated as "project opted in to default thresholds with zero exemptions."

## Backward compatibility

None. The two systems do not coexist. The PR strips `LINT-EXCLUDE-FILE` support from the hook, removes the 9 directives from `src/*.cppm`, and adds `.lint-skip` in one atomic change.

Any half-merged state would either:
- Suppress nothing (hook in flight, `.cppm` directives gone, `.lint-skip` not yet shipped) → broken contributor workflow.
- Double-suppress (both systems active) → no harm, but the existence of two skip mechanisms is exactly the problem this issue closes.

The atomic swap is safer than a transition window. There is one external surface area (`smell_checks.py`) used by exactly one consumer (this user's Claude Code setup), so the swap can be coordinated without backward-compat concerns.

## Verification before commit

Per CLAUDE.md mandatory safety rules:

1. **Bench (Release)** — run after the `.cppm` edits. Storm's bench measures SELECT/INSERT/UPDATE/DELETE throughput; the changes here are comment removals only, so the expected outcome is zero delta. Confirm.
2. **ASAN+UBSAN (`ninja-asan-ubsan`)** — comment removals can't introduce memory or UB issues, but the rule says always run after code changes. Confirm.
3. **TSAN (`ninja-tsan`)** — same rationale. Confirm.
4. **`commit.sh` pre-commit hook** — format, clang-tidy, tests, coverage. Standard.

The hook-side changes (`smell_checks.py` + `test_smell_checks.py`) are out-of-repo and verified by `python -m pytest ~/.claude/hooks/` — listed in the PR body for the reviewer.

## SonarCloud gate

Per `/sonarcloud-status` policy: zero issues on new code, zero duplication on new code. The PR is mostly removals + a small new file + documentation; surface area for SonarCloud findings is minimal. If it flags anything, fix and re-push until clean.

## Acceptance criteria

1. Audit table appended to `docs/development/CODE_QUALITY.md` with 14 rows backed by merged PR commit messages.
2. Threshold-decisions section appended, one paragraph per constant, justifying "keep" with numbers.
3. `.lint-skip` created at repo root with the 9 entries above.
4. All 9 `// LINT-EXCLUDE-FILE: …` lines removed from `src/*.cppm`.
5. `~/.claude/hooks/smell_checks.py` updated — directive code removed, skip-file reader added. Captured as diff in the PR body.
6. `~/.claude/hooks/test_smell_checks.py` created with 5 tests, all passing.
7. Bench (Release), ASAN+UBSAN, TSAN — no regressions vs `develop`.
8. SonarCloud gate passes (zero new issues, zero new duplication).
9. CI passes on the PR (ninja-debug, ninja-asan-ubsan, ninja-tsan).
10. Issue #293 closed via `gh issue close 293` after merge.

## Related

- [#264](https://github.com/spiritEcosse/storm/issues/264) — parent umbrella, closed; this is the last unfinished item from its acceptance criteria.
- [#277](https://github.com/spiritEcosse/storm/issues/277) — Phase 3, closed 2026-05-20. Provides the audit data.
- [#275](https://github.com/spiritEcosse/storm/pull/275) — Phase 2 (postgresql.cppm split).
- [#276](https://github.com/spiritEcosse/storm/pull/276) — rationale rewrite on the 14 `LINT-EXCLUDE-FILE` files.
- Hook source (out of repo): `~/.claude/hooks/check-complexity.py`, `~/.claude/hooks/smell_checks.py`, `~/.claude/hooks/smell_types.py`.
