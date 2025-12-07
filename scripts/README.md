# Storm Scripts

Utility scripts for the Storm ORM project.

## SonarCloud Check Script

Check SonarCloud quality gate status and issues for **Pull Requests** or **Branches**.

### Setup

1. **Get your SonarCloud token**:
   - Go to https://sonarcloud.io/account/security
   - Generate a new token
   - Copy the token

2. **Set the environment variable**:
   ```bash
   export SONAR_TOKEN=your_token_here
   ```

   To make it permanent, add to your `~/.bashrc` or `~/.zshrc`:
   ```bash
   echo 'export SONAR_TOKEN=your_token_here' >> ~/.zshrc
   source ~/.zshrc
   ```

### Usage

#### Check Pull Requests
```bash
# Check PR by number (shorthand)
./scripts/sonarcloud-check.sh 48

# Check PR (explicit)
./scripts/sonarcloud-check.sh --pr 48
```

#### Check Branches
```bash
# Check specific branch
./scripts/sonarcloud-check.sh --branch develop

# Check current branch
./scripts/sonarcloud-check.sh --branch $(git branch --show-current)

# Smart detection (branch name, not a number)
./scripts/sonarcloud-check.sh develop
```

#### Interactive Mode
```bash
# Will prompt for PR or branch
./scripts/sonarcloud-check.sh
```

### Using with Claude Code Slash Commands

Inside Claude Code, you can use slash commands that call this script:

```
/sonarcloud-status              # Check current branch
/sonarcloud-branch develop      # Check specific branch
/sonarcloud-branch 48           # Check PR #48
/sonarcloud-branch PR #48       # Check PR #48 (alternative)
```

### Output

The script displays:
- ✅/❌ Quality Gate status (PASSED/FAILED)
- 📈 Metrics (coverage, bugs, code smells, duplication, etc.)
- 🐛 Top 10 issues with severity, file, line number, and effort
- 🔗 Direct links to SonarCloud dashboard

### Example

```bash
$ ./scripts/sonarcloud-check.sh 48

=== SonarCloud Analysis for PR #48 ===

📊 Quality Gate Status:
❌ FAILED

Failed Conditions:
  ❌ new_reliability_rating: 4 (threshold: 1)
  ❌ new_duplicated_lines_density: 7.0 (threshold: 3)

Passed Conditions:
  ✅ new_security_rating: 1 (threshold: 1)
  ✅ new_maintainability_rating: 1 (threshold: 1)

📈 Metrics:
  new_bugs: 0
  new_code_smells: 52
  new_coverage: N/A
  new_duplicated_lines_density: 7.0

🐛 Issues Summary:
  Total Issues: 52

Top 10 Issues:

[CRITICAL] CODE_SMELL: Refactor function to reduce Cognitive Complexity
  File: benchmarks/main.cpp (Line 42)
  Rule: cpp:S3776
  Effort: 7min

...

🔗 Links:
  Dashboard: https://sonarcloud.io/dashboard?id=spiritEcosse_storm&pullRequest=48
  Issues: https://sonarcloud.io/project/issues?pullRequest=48&...

=== Summary ===
❌ PR #48 needs attention (quality gate failed)
   Please fix the issues above
```
