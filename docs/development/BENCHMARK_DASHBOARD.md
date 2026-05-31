# Benchmark Dashboard

`storm_bench_dashboard` is a real-time ANSI terminal UI that captures and visualizes `storm_bench` results in a SQLite database using the Storm ORM. It displays live metrics as benchmarks run, maintains a persistent result history, and supports backup/restore operations.

## Quick Start

### Two-Terminal Workflow

**Terminal 1** (dashboard):
```bash
./build/release/benchmarks/dashboard/storm_bench_dashboard
```

**Terminal 2** (benchmark):
```bash
STORM_BENCH_SOCKET=1 ./build/release/benchmarks/storm_bench --benchmark_filter='Storm/WHERE/.*'
```

The dashboard appears in Terminal 1, collecting and displaying results as they stream in.

### Quick Smoke Test

For rapid iteration without waiting for full benchmark runs:
```bash
STORM_BENCH_SOCKET=1 ./build/release/benchmarks/storm_bench --benchmark_filter='Storm/WHERE/.*' --benchmark_min_time=0.2s
```

## Database Schema

Results are stored in `~/.local/state/storm/dashboard/bench_results.db` with two tables:

- **`BenchRun`** — One row per benchmark session, capturing start time, filter, and environment
- **`BenchResult`** — One row per benchmark result, linking to its parent `BenchRun`

## First-Run Setup

The database schema is not auto-created. You must initialize it via Atlas migrations:

```bash
# Generate migrations (one-time, only if schema changes)
cmake --build . --target makemigrations-bench

# Apply migrations to create/update schema
atlas migrate apply --dir 'file://benchmarks/dashboard/migrations' --url 'sqlite://~/.local/state/storm/dashboard/bench_results.db'
```

The binary will print a helpful error with the exact command if the schema is missing.

## Baseline Regression Detection

When a baseline run exists in the database, each streamed result is compared in real time:

```bash
# Compare against most recent full run on the same branch + host (default)
./storm_bench_dashboard

# Compare against most recent full run on a specific branch
./storm_bench_dashboard --baseline branch:develop

# Compare against a specific run by id
./storm_bench_dashboard --baseline run:38

# Disable comparison
./storm_bench_dashboard --baseline none

# Custom regression threshold (default 5%)
./storm_bench_dashboard --regression-threshold 10
```

### Baseline selectors

| Selector | What it picks |
|----------|--------------|
| `auto` (default) | Most recent **full** run on the same branch + hostname |
| `run:<id>` | Specific run by numeric id (see `SELECT id FROM BenchRun`) |
| `branch:<name>` | Most recent full run on the named branch, any host |
| `none` | No comparison column |

`--baseline auto` skips partial/filtered runs (`is_full_run = false`) so percentage deltas are always against a comparable full run. If no matching baseline is found, the dashboard prints a notice and runs without comparison.

> **Git branch/hash are read per run, not at startup.** Each `BenchRun` row records the working-tree `branch` and `git_hash` at the moment the bench process connects, not when the daemon started (Issue #267). This means a long-lived daemon stays correct across `git checkout`/commits — `--baseline auto` always matches the branch the bench was actually run from. Because the labels come from `git rev-parse`, **the dashboard must be started from inside the storm git work tree**; if it is not, it fails loudly at startup (exit code 4) instead of stamping every run with empty git metadata.

### Delta column

Each result line shows a delta column next to the latency:

| Delta | Colour | Label |
|-------|--------|-------|
| within threshold | grey | `+1.2%` |
| ≥ threshold | red | `+6.3% REGRESS` |
| ≥ 2× threshold | red | `+11.4% SEVERE` |
| ≤ −threshold | green | `−7.1% IMPROVE` |
| no baseline row | grey | `—` |

The `—` marker appears for benchmarks not present in the baseline run (e.g. new benchmarks, or filtered runs with a different `--benchmark_filter`).

### Session header

A completed session renders one line in this form:

```
▼ [1] filter='…' · 18 results · complete 08:30:04 UTC
```

The `N results` counter is the number of **timing rows** in that run — both raw per-repetition `measurement` rows and `mean`/`median`/`stddev`/`cv` `aggregate` rows count. Big-O and RMS rows are not counted there; they fold into the per-category `Complexity:` footer instead. So a run that records, say, 18 timing rows plus 2 BigO + 2 RMS rows shows `18 results`, even though the `BenchResult` table holds 22 rows for that `run_id`.

#### Row kinds and `--benchmark_report_aggregates_only`

Each `BenchResult` carries a `row_kind`: `measurement` (one raw per-repetition row), `aggregate` (a `mean`/`median`/`stddev`/`cv` summary row), `bigo`, or `rms`. Under `--benchmark_repetitions=N` Google Benchmark emits both the raw rows and the aggregate rows; under `--benchmark_report_aggregates_only=true` it emits **only** the aggregate rows. The dashboard streams every timing row regardless of style — both `measurement` and `aggregate` rows render and count (Issue #265). Earlier the reporter silently dropped every `aggregate` row, so the aggregates-only invocation recorded no timings at all.

### Summary line

Each session header shows a running tally:

```
Summary:  18 ok  3 improve  1 regress
```

## Complexity (Big-O) Tracking

Every Storm benchmark calls `->Complexity(benchmark::oN)`, which makes Google Benchmark fit a curve across the dataset-size range and emit two extra rows per benchmark family: a `*_BigO` row (leading coefficient) and a `*_RMS` row (fit quality as a percentage error).

The dashboard captures these rows alongside regular measurement rows. When a baseline run is active, each category shows a complexity footer:

```
  Complexity:
    Storm/WHERE/where_int_gt    N → N     coef 1.21 → 1.23  +1.6%  ✓
    Storm/JOIN/join_messages    N → N²                            ✗ SHAPE
    Storm/ORDER/order_salary    NlgN → NlgN  coef 2.10 → 2.95  +40%  ⚠ DRIFT
```

Two regression types are detected:

- **Shape regression** (`✗ SHAPE`, red) — the complexity class changed (e.g. `N` → `N²`). Strongest signal; always flagged regardless of threshold.
- **Coefficient drift** (`⚠ DRIFT`, yellow) — same shape but leading coefficient grew above the threshold. Configurable via `--complexity-threshold`.
- **Coefficient improvement** (`↑ IMPROVE`, green) — coefficient shrank below the negative threshold.
- **Stable** (`✓`, green) — class unchanged, coefficient within threshold.

Use `--complexity-threshold` to adjust sensitivity (default 5%):

```bash
./storm_bench_dashboard --complexity-threshold 10
```

## CLI Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `--db PATH` | `~/.local/state/storm/dashboard/bench_results.db` | Database file path |
| `--socket PATH` | `$XDG_RUNTIME_DIR/storm-bench.sock` (with `/tmp/storm-bench-$USER.sock` fallback) | Unix domain socket for streaming results |
| `--order arrival` | `newest` | Render results in insertion order instead of newest-first |
| `--baseline SELECTOR` | `auto` | Baseline for regression comparison (see above) |
| `--regression-threshold N` | `5` | Percentage delta that counts as a per-size regression |
| `--complexity-threshold N` | `5` | Coefficient drift percentage that counts as a complexity regression |
| `--upload-backup` | (one-shot, then exit) | Push database to the backup repo via `gh release upload --clobber` |
| `--restore-backup` | (one-shot, then exit) | Pull database from the backup repo via `gh release download` |
| `--backup-repo OWNER/REPO` | `spiritEcosse/storm-bench-private` | GitHub repo for backups |
| `--backup-tag NAME` | `bench-backup-$(hostname -s)` | Release tag for backup asset |

## Keyboard Controls

| Key | Action |
|-----|--------|
| `q` | Quit |
| `r` | Refresh display from database |
| `1`–`9` | Toggle expand/collapse for session 1–9 |

## Backup Setup (Optional)

To enable backup/restore, set up a private GitHub repository and link it:

```bash
# One-time setup
gh repo create spiritEcosse/storm-bench-private --private

# Backup the database (exits after upload)
./build/release/benchmarks/dashboard/storm_bench_dashboard --upload-backup

# Restore from backup (exits after download — rename the current DB first if you want to keep it)
./build/release/benchmarks/dashboard/storm_bench_dashboard --restore-backup
```

Backups require `gh auth login` to be configured.

## Troubleshooting

### "Socket already in use" error

A stale socket file from a crashed previous run may block startup. Clean it up:

```bash
rm /run/user/$UID/storm-bench.sock          # XDG_RUNTIME_DIR
rm /tmp/storm-bench-$USER.sock               # Fallback
```

### Dashboard offline (no data appearing)

The benchmark process automatically detects a missing or offline socket and falls back to text reporter mode. No action is needed — results are still collected, just not streamed. Run the dashboard in a fresh terminal session and restart the benchmark.

### "Schema missing" error

Run the Atlas migration command:
```bash
atlas migrate apply --dir 'file://benchmarks/dashboard/migrations' --url 'sqlite://~/.local/state/storm/dashboard/bench_results.db'
```

### Backup operations fail with "permission denied"

Ensure you have authenticated:
```bash
gh auth login
```

## Environment Variable

- `STORM_BENCH_SOCKET=1` — Enable streaming to the dashboard (set in the benchmark process, not the dashboard). Defaults to disabled; CI never sets this to avoid network calls.

## Notes

- Streaming is **opt-in** via the `STORM_BENCH_SOCKET` environment variable — CI workloads do not enable it.
- The dashboard gracefully handles missing or stale sockets; benchmarks continue without interruption.
- Results are persisted in SQLite even if the dashboard is offline, using Storm ORM for type-safe queries.
