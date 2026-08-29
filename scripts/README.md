# Storm Scripts

Utility scripts for the Storm ORM project.

## Clang-Tidy Script

Run clang-tidy with modernize checks on the Storm codebase, excluding third_party code.

### Prerequisites

- Release build with compile_commands.json: `cmake --preset ninja-release`

### Usage

```bash
# Check only (default)
./scripts/run_clang_tidy.sh

# Auto-apply fixes
./scripts/run_clang_tidy.sh --fix

# Limit parallel jobs
./scripts/run_clang_tidy.sh -j 4

# Include tests directory
./scripts/run_clang_tidy.sh --include-tests
```

### Options

| Option | Description |
|--------|-------------|
| `--fix` | Apply suggested fixes automatically (use with caution) |
| `-j N` | Number of parallel jobs (default: all cores) |
| `--include-tests` | Include tests/ directory (disabled by default due to gtest setup) |

### What it checks

- **Checks**: `modernize-*` (excluding `use-trailing-return-type`, `avoid-c-arrays`)
- **Files**: `src/*.cppm`, `benchmarks/*.cpp`
- **Excludes**: `third_party/`, test files (unless `--include-tests`)

### Notes

- Runs in parallel using all available CPU cores by default
- Filters out noisy clang-tidy meta-messages
- Handles clang-tidy crashes gracefully (skips crashed files)
- C++26 module support is limited - some warnings may be suppressed

---

## SonarCloud Quality Checks

Use the `sonar` CLI to inspect code quality. Auth is stored in the OS keychain via `sonar auth login`.

### Prerequisites

1. **`sonar` CLI** - Install and authenticate once:
   ```bash
   sonar auth login
   ```

### Usage

```bash
# List issues on develop branch
sonar list issues --project spiritEcosse_storm --branch develop --format table

# List issues on a PR (new code only)
sonar list issues --project spiritEcosse_storm --pull-request 48 --format table

# Quality gate status
sonar api get "/api/qualitygates/project_status?projectKey=spiritEcosse_storm&branch=develop"

# Quality gate status for a PR
sonar api get "/api/qualitygates/project_status?projectKey=spiritEcosse_storm&pullRequest=48"
```

### Using with Claude Code Slash Commands

Inside Claude Code, you can use slash commands:

```
/sonarcloud-status              # Auto-detect PR from current branch
/sonarcloud-status 48           # Check PR #48
/sonarcloud-branch              # Alias for sonarcloud-status
```

### Example

```bash
$ sonar list issues --project spiritEcosse_storm --pull-request 48 --format table

Detecting PR for branch: claude/my-feature-branch
Found PR #48

=== SonarCloud Analysis for PR #48 ===

📊 Quality Gate Status:
✅ PASSED

📈 Metrics:
  new_bugs: 0
  new_vulnerabilities: 0
  new_code_smells: 5
  new_duplicated_lines_density: 1.21

🐛 Issues Summary:
  Total Issues: 5

Top 10 Issues:

[MINOR] CODE_SMELL: Define each identifier in a dedicated statement.
  File: src/orm/statements/insert.cppm (Line 198)
  Rule: cpp:S1659
  Effort: 5min

...

📋 Code Duplications:
  New duplicated blocks: 4
  New duplicated lines: 44
  Duplication density: 1.21%

Duplication details (from PR files):

  src/orm/statements/base.cppm:
    Lines 105-119 in src/orm/statements/base.cppm
    ↔     Lines 130-144 in src/orm/statements/base.cppm

  src/orm/statements/distinct.cppm:
    Lines 194-214 in src/orm/statements/distinct.cppm
    ↔     Lines 283-303 in src/orm/statements/select.cppm

🔗 Links:
  Dashboard: https://sonarcloud.io/dashboard?id=spiritEcosse_storm&pullRequest=48
  Issues: https://sonarcloud.io/project/issues?pullRequest=48&...

=== Summary ===
✅ PR #48 is ready (quality gate passed)
```

### Notes

- **Auto-detection**: When run without arguments, the script uses `gh pr view` to find the PR associated with the current Git branch
- **Duplication details**: Shows exact line ranges where code is duplicated, including cross-file duplications
- **PR-specific metrics**: The summary metrics (blocks, lines, density) are specific to new code in the PR; detailed locations show all duplications in modified files

## Agent Frontmatter Check

Validates that every `.claude/agents/*.md` file has a single-line `description:` in its
YAML frontmatter.

### Why

Claude Code parses each agent file's frontmatter to register the agent. In YAML an unquoted
scalar ends at the first line starting in column 0, so a `description:` written across real
newlines terminates early, the parse fails, and **the agent is dropped silently** — it never
appears in the available-agents list and cannot be dispatched, with no warning emitted.

`storm-sql-reviewer` and `storm-buildsystem-reviewer` both shipped this way and were
undispatchable from the day they merged, which left CLAUDE.md rule #13 partly unenforceable
(issue #543). A diff review does not catch it: the broken file looks perfectly readable.

The fix and the enforced invariant: keep `description:` on **one physical line**, writing
newlines as literal `\n` escapes — the form used by every agent file that loads correctly.

### Usage

```bash
./scripts/check-agent-frontmatter.sh              # defaults to .claude/agents
./scripts/check-agent-frontmatter.sh some/dir     # validate another directory
./scripts/tests/test_check_agent_frontmatter.sh   # self-test
```

Exit code `0` = all agent files valid, `1` = at least one invalid.

### Where it runs

- **`commit.sh`** — whenever a `.claude/agents/*.md` file is staged. Runs before the
  step-count early exit, since an agent-only commit skips every C++/cmake step and is
  exactly the commit that can introduce this bug.
- **CI** — the `agent-frontmatter` job in `.github/workflows/ci.yml`, which also self-tests
  the validator. Emits `::error file=…` annotations so failures land on the file in the PR diff.

### Notes

- Not a strict YAML validation. The descriptions contain unquoted `:` characters that a strict
  parser rejects but Claude Code's lenient parser accepts — the physical line span, not YAML
  validity, is what distinguishes a loading file from a dropped one.
- A `.md` file with no opening `---` is treated as shared prose, not an agent, and skipped.
- An agent file with **no** `description:` at all is reported: it cannot be registered either.

## Change Classification (detect-changes.sh)

Classifies a list of changed file paths into `HAS_SRC_CHANGES` / `HAS_TEST_CHANGES` /
`HAS_CPP_CHANGES` / `HAS_CMAKE_CHANGES` / `HAS_BENCH_CHANGES` flags — one place shared by
`commit.sh` (pre-commit smart skips over staged files) and the `changes` job in
`.github/workflows/ci.yml` (skips the `test` / `coverage` / `clang-p2996-host-layout` jobs
on a PR that doesn't touch src/tests/cmake), so the two never classify the same file
differently.

### Usage

```bash
git diff --cached --name-only | ./scripts/detect-changes.sh
# HAS_SRC_CHANGES=true
# HAS_TEST_CHANGES=false
# HAS_CPP_CHANGES=true
# HAS_CMAKE_CHANGES=false
# HAS_BENCH_CHANGES=false

./scripts/tests/test_detect_changes.sh   # self-test
```

Output is `KEY=true|false` lines meant to be `eval`'d into shell variables:
```bash
eval "$(git diff --cached --name-only | ./scripts/detect-changes.sh)"
```

### Where it runs

- **`commit.sh`** — over staged files, to skip format/tidy/tests/coverage when nothing
  they could affect changed.
- **CI** — the `changes` job in `.github/workflows/ci.yml`, which also self-tests the
  script before using its output. `run_heavy` (the job's output) additionally forces a
  full run whenever the diff touches CI plumbing itself (`.github/workflows/`, `scripts/`,
  `.githooks/`, `CMakePresets.json`, `commit.sh`), regardless of the flags above.
