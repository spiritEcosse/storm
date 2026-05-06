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

### Summary line

Each session header shows a running tally:

```
Summary:  18 ok  3 improve  1 regress
```

## CLI Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `--db PATH` | `~/.local/state/storm/dashboard/bench_results.db` | Database file path |
| `--socket PATH` | `$XDG_RUNTIME_DIR/storm-bench.sock` (with `/tmp/storm-bench-$USER.sock` fallback) | Unix domain socket for streaming results |
| `--order arrival` | `newest` | Render results in insertion order instead of newest-first |
| `--baseline SELECTOR` | `auto` | Baseline for regression comparison (see above) |
| `--regression-threshold N` | `5` | Percentage delta that counts as a regression |
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
