---
description: Check SonarCloud status for a specific branch or PR (pass branch name or PR number)
allowed-tools: Bash
---

Check SonarCloud status for a specific branch or pull request.

Usage:
- /sonarcloud-branch [branch-name]  - Check specific branch
- /sonarcloud-branch PR #48         - Check PR #48
- /sonarcloud-branch 48             - Check PR #48 (shorthand)
- /sonarcloud-branch                - Check current branch

Run the unified SonarCloud check script:

```bash
# Extract branch name or PR number from user's message
# If message contains "PR #" or just a number, treat as PR
# Otherwise treat as branch name
# If no argument, use current branch

./scripts/sonarcloud-check.sh "$@"
```

Note: Requires SONAR_TOKEN environment variable to be set.
