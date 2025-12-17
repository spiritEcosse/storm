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

## SonarCloud Check Script

Check SonarCloud quality gate status, issues, and **code duplications** for Pull Requests.

### Prerequisites

1. **GitHub CLI (`gh`)** - Required for auto-detecting PR from current branch
   ```bash
   # Install (see https://cli.github.com/)
   # Then authenticate:
   gh auth login
   ```

2. **SonarCloud Token**:
   - Go to https://sonarcloud.io/account/security
   - Generate a new token
   - Set the environment variable:
   ```bash
   export SONAR_TOKEN=your_token_here
   ```

   To make it permanent, add to your `~/.bashrc` or `~/.zshrc`:
   ```bash
   echo 'export SONAR_TOKEN=your_token_here' >> ~/.zshrc
   source ~/.zshrc
   ```

### Usage

```bash
# Auto-detect PR from current branch (recommended)
./scripts/sonarcloud-check.sh

# Override with specific PR number
./scripts/sonarcloud-check.sh 48

# Explicit PR number
./scripts/sonarcloud-check.sh --pr 48
```

### Using with Claude Code Slash Commands

Inside Claude Code, you can use slash commands that call this script:

```
/sonarcloud-status              # Auto-detect PR from current branch
/sonarcloud-status 48           # Check PR #48
/sonarcloud-branch              # Alias for sonarcloud-status
```

### Output

The script displays:
- 📊 Quality Gate status (PASSED/FAILED)
- 📈 Metrics (coverage, bugs, code smells, duplication density, etc.)
- 🐛 Top 10 issues with severity, file, line number, and effort
- 📋 Code duplications with exact line locations
- 🔗 Direct links to SonarCloud dashboard

### Example

```bash
$ ./scripts/sonarcloud-check.sh

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
