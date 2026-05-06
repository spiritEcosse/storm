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

## CLI Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `--db PATH` | `~/.local/state/storm/dashboard/bench_results.db` | Database file path |
| `--socket PATH` | `$XDG_RUNTIME_DIR/storm-bench.sock` (with `/tmp/storm-bench-$USER.sock` fallback) | Unix domain socket for streaming results |
| `--order arrival` | `newest` | Render results in insertion order instead of newest-first |
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
