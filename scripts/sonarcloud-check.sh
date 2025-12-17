#!/bin/bash
# SonarCloud Check Script - Pull Request Only
# Usage:
#   ./scripts/sonarcloud-check.sh           # Auto-detect PR from current branch
#   ./scripts/sonarcloud-check.sh 48        # Check PR #48 (override)
#   ./scripts/sonarcloud-check.sh --pr 48   # Check PR #48 (explicit override)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_KEY="spiritEcosse_storm"
SONAR_API="https://sonarcloud.io/api"

# Check if SONAR_TOKEN is set
if [ -z "$SONAR_TOKEN" ]; then
    echo -e "${RED}Error: SONAR_TOKEN environment variable not set${NC}"
    echo "Please set it: export SONAR_TOKEN=your_token"
    exit 1
fi

# Check if gh is available
if ! command -v gh &> /dev/null; then
    echo -e "${RED}Error: 'gh' CLI not found${NC}"
    echo "Install it: https://cli.github.com/"
    exit 1
fi

# Check if gh is authenticated
if ! gh auth status &> /dev/null; then
    echo -e "${RED}Error: 'gh' CLI not authenticated${NC}"
    echo "Run: gh auth login"
    exit 1
fi

# Parse arguments
PR_NUMBER=""

if [ "$1" = "--pr" ]; then
    PR_NUMBER="$2"
elif [ -n "$1" ]; then
    # If it's a number, use it as PR number
    if [[ "$1" =~ ^[0-9]+$ ]]; then
        PR_NUMBER="$1"
    else
        echo -e "${RED}Error: Invalid PR number '${1}'. Must be a number.${NC}" >&2
        echo "Usage: $0 [PR_NUMBER]" >&2
        exit 1
    fi
fi

# Auto-detect PR from current branch if not provided
if [[ -z "$PR_NUMBER" ]]; then
    CURRENT_BRANCH=$(git branch --show-current 2>/dev/null)
    if [ -z "$CURRENT_BRANCH" ]; then
        echo -e "${RED}Error: Not in a git repository or no branch checked out${NC}" >&2
        exit 1
    fi

    echo -e "${BLUE}Detecting PR for branch: ${YELLOW}${CURRENT_BRANCH}${NC}"

    # Get PR number for current branch
    PR_NUMBER=$(gh pr view --json number -q '.number' 2>/dev/null || echo "")

    if [ -z "$PR_NUMBER" ]; then
        echo -e "${RED}Error: No PR found for branch '${CURRENT_BRANCH}'${NC}" >&2
        echo -e "${YELLOW}Create a PR first or specify PR number: $0 <PR_NUMBER>${NC}" >&2
        exit 1
    fi

    echo -e "${GREEN}Found PR #${PR_NUMBER}${NC}\n"
fi

# Set query parameters
QUERY_PARAM="pullRequest=${PR_NUMBER}"
DISPLAY_NAME="PR #${PR_NUMBER}"
DASHBOARD_PARAM="pullRequest=${PR_NUMBER}"

echo -e "${BLUE}=== SonarCloud Analysis for ${DISPLAY_NAME} ===${NC}\n"

# 1. Quality Gate Status
echo -e "${BLUE}📊 Quality Gate Status:${NC}"
QG_RESPONSE=$(curl -s "${SONAR_API}/qualitygates/project_status?projectKey=${PROJECT_KEY}&${QUERY_PARAM}" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")

QG_STATUS=$(echo "$QG_RESPONSE" | jq -r '.projectStatus.status // "UNKNOWN"')

if [ "$QG_STATUS" = "OK" ]; then
    echo -e "${GREEN}✅ PASSED${NC}\n"
elif [ "$QG_STATUS" = "ERROR" ]; then
    echo -e "${RED}❌ FAILED${NC}\n"

    echo -e "${YELLOW}Failed Conditions:${NC}"
    echo "$QG_RESPONSE" | jq -r '.projectStatus.conditions[] | select(.status == "ERROR") |
        "  ❌ \(.metricKey): \(.actualValue) (threshold: \(.errorThreshold))"'

    echo -e "\n${GREEN}Passed Conditions:${NC}"
    echo "$QG_RESPONSE" | jq -r '.projectStatus.conditions[] | select(.status == "OK") |
        "  ✅ \(.metricKey): \(.actualValue) (threshold: \(.errorThreshold))"'
else
    echo -e "${YELLOW}⚠️  ${QG_STATUS}${NC}\n"
fi

# 2. Metrics
echo -e "\n${BLUE}📈 Metrics:${NC}"
METRIC_KEYS="new_coverage,new_bugs,new_vulnerabilities,new_code_smells,new_security_hotspots,new_lines,new_technical_debt,new_duplicated_lines_density"
METRICS_RESPONSE=$(curl -s "${SONAR_API}/measures/component?component=${PROJECT_KEY}&${QUERY_PARAM}&metricKeys=${METRIC_KEYS}" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")

echo "$METRICS_RESPONSE" | jq -r '.component.measures[]? |
    "  \(.metric): \(.value // .periods[0].value // "N/A")"' 2>/dev/null || echo "  No metrics available"

# 3. Issues Summary
echo -e "\n${BLUE}🐛 Issues Summary:${NC}"
ISSUES_RESPONSE=$(curl -s "${SONAR_API}/issues/search?componentKeys=${PROJECT_KEY}&${QUERY_PARAM}&resolved=false&ps=100&s=SEVERITY&asc=false" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")

TOTAL_ISSUES=$(echo "$ISSUES_RESPONSE" | jq -r '.total // 0')
echo -e "  Total Issues: ${YELLOW}${TOTAL_ISSUES}${NC}"

if [ "$TOTAL_ISSUES" -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Top 10 Issues:${NC}"
    echo "$ISSUES_RESPONSE" | jq -r '.issues[0:10]? | .[] |
        "\n[\(.severity)] \(.type): \(.message)\n  File: \(.component | split(":")[1] // .component) (Line \(.line // "N/A"))\n  Rule: \(.rule)\n  Effort: \(.effort // "N/A")"'
fi

# 4. Code Duplications
echo -e "\n${BLUE}📋 Code Duplications:${NC}"
DUP_METRICS=$(curl -s "${SONAR_API}/measures/component?component=${PROJECT_KEY}&${QUERY_PARAM}&metricKeys=new_duplicated_blocks,new_duplicated_lines,new_duplicated_lines_density" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")

NEW_DUP_BLOCKS=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_blocks") | .periods[0].value // "0"')
NEW_DUP_LINES=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_lines") | .periods[0].value // "0"')
NEW_DUP_DENSITY=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_lines_density") | .periods[0].value // "0"')

echo -e "  New duplicated blocks: ${YELLOW}${NEW_DUP_BLOCKS:-0}${NC}"
echo -e "  New duplicated lines: ${YELLOW}${NEW_DUP_LINES:-0}${NC}"
echo -e "  Duplication density: ${YELLOW}${NEW_DUP_DENSITY:-0}%${NC}"

# Get files with duplications in the PR using GitHub API
if [ "${NEW_DUP_BLOCKS:-0}" != "0" ] && [ "${NEW_DUP_BLOCKS:-0}" != "" ]; then
    echo ""
    echo -e "${YELLOW}Duplication details (from PR files):${NC}"

    # Check if gh is available
    if command -v gh &> /dev/null; then
        # Get source files changed in the PR
        PR_FILES=$(gh pr view "${PR_NUMBER}" --json files -q '.files[].path' 2>/dev/null | grep -E '\.(cpp|cppm|h|hpp|c)$' || true)

        if [ -n "$PR_FILES" ]; then
            FOUND_DUP=false
            for FILE_PATH in $PR_FILES; do
                FILE_KEY="${PROJECT_KEY}:${FILE_PATH}"
                DUP_DETAILS=$(curl -s "${SONAR_API}/duplications/show?key=${FILE_KEY}" \
                    -H "Authorization: Bearer ${SONAR_TOKEN}" 2>/dev/null)

                # Check if there are duplications
                DUP_COUNT=$(echo "$DUP_DETAILS" | jq '.duplications | length' 2>/dev/null || echo "0")
                if [ "${DUP_COUNT:-0}" -gt 0 ]; then
                    FOUND_DUP=true
                    echo -e "\n  ${BLUE}${FILE_PATH}:${NC}"
                    echo "$DUP_DETAILS" | jq -r '
                        .files as $files |
                        .duplications[]? |
                        .blocks as $blocks |
                        ($blocks | map(
                            ($files[._ref] | .name // "unknown") as $fname |
                            "    Lines \(.from)-\(.from + .size - 1) in \($fname)"
                        ) | join("\n    ↔ "))' 2>/dev/null
                fi
            done

            if [ "$FOUND_DUP" = false ]; then
                echo "  No duplications found in PR source files"
            fi
        else
            echo "  No source files found in PR"
        fi
    else
        echo -e "  ${YELLOW}Install 'gh' CLI to see duplication details for PR files${NC}"
        echo "  Run: gh auth login"
    fi
fi

# 5. Links
echo -e "\n${BLUE}🔗 Links:${NC}"
echo "  Dashboard: https://sonarcloud.io/dashboard?id=${PROJECT_KEY}&${DASHBOARD_PARAM}"
echo "  Issues: https://sonarcloud.io/project/issues?${DASHBOARD_PARAM}&id=${PROJECT_KEY}&resolved=false"

# Summary
echo -e "\n${BLUE}=== Summary ===${NC}"
if [ "$QG_STATUS" = "OK" ]; then
    echo -e "${GREEN}✅ ${DISPLAY_NAME} is ready (quality gate passed)${NC}"
elif [ "$QG_STATUS" = "ERROR" ]; then
    echo -e "${RED}❌ ${DISPLAY_NAME} needs attention (quality gate failed)${NC}"
    echo -e "${YELLOW}   Please fix the issues above${NC}"
else
    echo -e "${YELLOW}⚠️  ${DISPLAY_NAME} status: ${QG_STATUS}${NC}"
fi

echo ""
