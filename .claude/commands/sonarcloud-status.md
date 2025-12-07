---
description: Check SonarCloud quality gate status and issues for current branch
allowed-tools: Bash
---

Check the current SonarCloud status for the Storm ORM project on the current git branch.

Run the unified SonarCloud check script for the current branch:

```bash
./scripts/sonarcloud-check.sh --branch $(git branch --show-current)
```

Note: Requires SONAR_TOKEN environment variable to be set.
