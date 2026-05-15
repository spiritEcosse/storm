# Pre-Commit Hook

Storm's pre-commit hook (`commit.sh`) runs format → clang-tidy → tests → coverage.
This page documents the **clang-tidy** stage in detail, because it has three
modes with different scopes.

## The three clang-tidy modes

| Mode | What it scans | Where it runs | Blocks on |
|---|---|---|---|
| `--diff` *(default)* | Lines staged in the current commit (`git diff --cached`) | Local pre-commit | New warnings on staged lines |
| `--full` | Whole files that are staged | Local pre-commit (opt-in) | Any warning in any staged file |
| `--all` | Every C++ file in the tree | Scheduled CI (Mondays 04:00 UTC) | Nothing — report only |

### `--diff` mode (default)

The pre-commit default since Issue #262. It pipes `git diff -U0 --cached` into
`clang-tidy-diff.py` from the clang-p2996 toolchain. The diagnostic engine only
emits warnings on lines the staged commit actually touches.

**Mechanically:** clang-tidy always parses the whole translation unit — C++
type resolution needs the full file. `--diff` mode does not change that. It
sets `-line-filter` on the clang-tidy invocation, so clang-tidy still walks
every AST node but suppresses the diagnostic when the source location is
outside the staged hunks. `--diff` and `--full` therefore have roughly the
same per-file CPU cost — the difference is output policy, not parse scope.

**Why this is the default:** an author who edits one function in a 600-line
module shouldn't have to fix unrelated accumulated drift before they can commit
their change. Drift cleanup happens on its own schedule (see `--all`).

```bash
./scripts/run_clang_tidy.sh --diff          # check-only
./scripts/run_clang_tidy.sh --diff --fix    # apply auto-fixes, then re-check
```

What can still block your commit in `--diff` mode:

- A warning on a line you added or changed.
- A new warning that a fix on an adjacent line uncovered.

What will **not** block your commit:

- Pre-existing warnings on lines you didn't touch — even in the same file.

### `--full` mode

The pre-#262 default. Scans whole files that are staged. Useful when you are
deliberately cleaning up drift in a specific module and want clang-tidy to
report everything in that file, not just your delta.

```bash
./scripts/run_clang_tidy.sh --full --fix          # local one-off
STORM_TIDY_FULL=1 git commit -m "..."             # force --full in pre-commit
```

`--full` is **not** the pre-commit default any more. It blocks the commit on
any pre-existing drift in a file you happen to touch, which punishes the
wrong author. Use `--diff` for everyday work.

### `--all` mode

Whole-tree sweep. Runs on the `clang-tidy weekly sweep` workflow every Monday
at 04:00 UTC, and on demand via `workflow_dispatch`. Output goes to the
workflow run's **Summary** tab.

```bash
./scripts/run_clang_tidy.sh --all                 # run locally (slow)
```

**Report-only.** The sweep does **not** gate merges on `develop`. Its job is
to surface accumulated drift within a week instead of within months — the gap
that let 48 warnings accumulate over ~3 months before Issue #262.

Gating `--all` is an opt-in goal once the existing baseline is clean.

## Parse failures on `src/*.cppm`

Since the 2026-05-11 clang-p2996 rebuild, every `src/*.cppm` parses cleanly
under clang-tidy. If clang-tidy reports `Found compiler error` on a `src/`
module file, **`run_clang_tidy.sh` will now fail loudly** with:

```
❌ <file> (PARSE FAILURE — toolchain or build state is broken)
   clang-tidy could not parse this file. Re-run cmake --preset
   ninja-release and rebuild before retrying. See Issue #262.
```

The cause is almost always one of:

1. Stale `build/release/` from before a clang-p2996 update — `rm -rf build/release && cmake --preset ninja-release && cmake --build --preset ninja-release`.
2. A regression in the clang-p2996 binary — file an upstream issue and fall back to `--full` with the affected `.cppm` added back to `is_known_unparseable`'s skip list (in `scripts/run_clang_tidy.sh`).

**Silent skipping is gone.** The unconditional `*.cppm` skip in
`is_known_unparseable` was the root cause of Issue #262 — clang-tidy could
parse those files for months before anyone noticed, and the accumulated drift
all surfaced at once when the build state changed.

Skip list now covers only files that genuinely can't be parsed standalone:

- `tests/*`, `benchmarks/*`, `fuzz/*`, `shared/*` — import Storm modules
- `src/orm/query_builder.hpp` — pseudo-module header that needs `import storm;`

## Author workflow

### When your PR introduces a new warning

`--diff` will catch it on commit. Either fix the warning, or — if the warning
is wrong and the check needs an exception — update `.clang-tidy` in the same
PR with a clear inline comment explaining why.

### When the weekly sweep reports new drift

The sweep is report-only. There is no automatic ticket. If you see new entries
when checking the Summary tab, open a focused cleanup PR or file an issue —
similar to how PR #263 cleaned the post-#262 baseline.

### Bypassing pre-commit

Don't. Use `STORM_TIDY_FULL=1` to widen the scope, or fix the warning, or
adjust `.clang-tidy`. `--no-verify` is forbidden by repo convention
(see [CLAUDE.md](../../CLAUDE.md) Critical Safety Rules).

## Related

- [Issue #262](https://github.com/spiritEcosse/storm/issues/262) — structural fix for silent drift
- [PR #263](https://github.com/spiritEcosse/storm/pull/263) — cleanup of the 48 accumulated warnings
- [FORMATTING.md](FORMATTING.md) — clang-format / cmake-format stages of the same hook
