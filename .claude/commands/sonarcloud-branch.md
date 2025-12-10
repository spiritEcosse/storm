---
description: Check SonarCloud status for a pull request (alias for sonarcloud-status)
allowed-tools: Bash
---

Check SonarCloud status for a pull request.

Usage:
- /sonarcloud-branch 48    - Check PR #48
- /sonarcloud-branch       - Interactive: prompt for PR number

Run the unified SonarCloud check script:

```bash
./scripts/sonarcloud-check.sh "$@"
```

Note: Requires SONAR_TOKEN environment variable to be set.
