---
description: Check SonarCloud quality gate status for a pull request
allowed-tools: Bash
---

Check the SonarCloud status for a Storm ORM pull request.

Usage:
- /sonarcloud-status 48    - Check PR #48
- /sonarcloud-status       - Interactive: prompt for PR number

Run the unified SonarCloud check script:

```bash
./scripts/sonarcloud-check.sh "$@"
```

Note: Requires SONAR_TOKEN environment variable to be set.
