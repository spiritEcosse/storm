#!/bin/bash
# SonarCloud Check Script - Pull Request Only
# Usage:
#   ./scripts/sonarcloud-check.sh 48        # Check PR #48
#   ./scripts/sonarcloud-check.sh --pr 48   # Check PR #48 (explicit)
#   ./scripts/sonarcloud-check.sh           # Interactive: prompt for PR number

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

# Parse arguments
PR_NUMBER=""

if [ "$1" = "--pr" ]; then
    PR_NUMBER="$2"
elif [ -n "$1" ]; then
    # If it's a number, use it as PR number
    if [[ "$1" =~ ^[0-9]+$ ]]; then
        PR_NUMBER="$1"
    else
        echo -e "${RED}Error: Invalid PR number '${1}'. Must be a number.${NC}"
        echo "Usage: $0 [PR_NUMBER]"
        exit 1
    fi
fi

# Interactive mode if no arguments
if [ -z "$PR_NUMBER" ]; then
    read -p "Enter PR number: " PR_NUMBER
    if ! [[ "$PR_NUMBER" =~ ^[0-9]+$ ]]; then
        echo -e "${RED}Error: Invalid PR number. Must be a number.${NC}"
        exit 1
    fi
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

# 4. Links
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
