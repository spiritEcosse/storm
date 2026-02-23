#!/bin/bash
# SonarCloud Check Script - Branch-aware
# Usage:
#   ./scripts/sonarcloud-check.sh              # Auto-detect: branch mode on develop/master, PR mode otherwise
#   ./scripts/sonarcloud-check.sh 48           # Force PR mode for PR #48
#   ./scripts/sonarcloud-check.sh --pr 48      # Force PR mode for PR #48 (explicit)
#   ./scripts/sonarcloud-check.sh --branch develop  # Force branch mode for develop
#
# Modes:
#   Branch mode (develop/master/main):  Full project health — ALL existing issues on the branch
#   PR mode (feature branches):         New code only — waits for Sonar analysis to finish, then checks changed lines

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
PROTECTED_BRANCHES=("develop" "master" "main")

# Check if SONAR_TOKEN is set
if [[ -z "$SONAR_TOKEN" ]]; then
    echo -e "${RED}Error: SONAR_TOKEN environment variable not set${NC}" >&2
    echo "Please set it: export SONAR_TOKEN=your_token" >&2
    exit 1
fi

# Check if gh is available
if ! command -v gh &> /dev/null; then
    echo -e "${RED}Error: 'gh' CLI not found${NC}" >&2
    echo "Install it: https://cli.github.com/" >&2
    exit 1
fi

# Check if gh is authenticated
if ! gh auth status &> /dev/null; then
    echo -e "${RED}Error: 'gh' CLI not authenticated${NC}" >&2
    echo "Run: gh auth login" >&2
    exit 1
fi

# Parse arguments
MODE=""        # "branch" or "pr"
PR_NUMBER=""
BRANCH_NAME=""

if [[ "$1" == "--pr" ]]; then
    MODE="pr"
    PR_NUMBER="$2"
elif [[ "$1" == "--branch" ]]; then
    MODE="branch"
    BRANCH_NAME="$2"
elif [[ -n "$1" ]] && [[ "$1" =~ ^[0-9]+$ ]]; then
    MODE="pr"
    PR_NUMBER="$1"
elif [[ -n "$1" ]]; then
    echo -e "${RED}Error: Invalid argument '${1}'${NC}" >&2
    echo "Usage: $0 [PR_NUMBER | --pr PR_NUMBER | --branch BRANCH_NAME]" >&2
    exit 1
fi

# Auto-detect mode from current branch
if [[ -z "$MODE" ]]; then
    CURRENT_BRANCH=$(git branch --show-current 2>/dev/null)
    if [[ -z "$CURRENT_BRANCH" ]]; then
        echo -e "${RED}Error: Not in a git repository or no branch checked out${NC}" >&2
        exit 1
    fi

    IS_PROTECTED=false
    for b in "${PROTECTED_BRANCHES[@]}"; do
        if [[ "$CURRENT_BRANCH" == "$b" ]]; then
            IS_PROTECTED=true
            break
        fi
    done

    if [[ "$IS_PROTECTED" == true ]]; then
        MODE="branch"
        BRANCH_NAME="$CURRENT_BRANCH"
    else
        MODE="pr"
        echo -e "${BLUE}Detecting PR for branch: ${YELLOW}${CURRENT_BRANCH}${NC}"
        PR_NUMBER=$(gh pr view --json number -q '.number' 2>/dev/null || echo "")
        if [[ -z "$PR_NUMBER" ]]; then
            echo -e "${RED}Error: No PR found for branch '${CURRENT_BRANCH}'${NC}" >&2
            echo -e "${YELLOW}Create a PR first or specify PR number: $0 <PR_NUMBER>${NC}" >&2
            exit 1
        fi
        echo -e "${GREEN}Found PR #${PR_NUMBER}${NC}\n"
    fi
fi

# ============================================================
# BRANCH MODE — Full project health (develop/master/main)
# ============================================================
if [[ "$MODE" == "branch" ]]; then
    QUERY_PARAM="branch=${BRANCH_NAME}"
    DISPLAY_NAME="branch '${BRANCH_NAME}'"

    echo -e "${BLUE}=== SonarCloud Analysis for ${DISPLAY_NAME} (full project) ===${NC}\n"

    # 1. Quality Gate
    echo -e "${BLUE}📊 Quality Gate Status:${NC}"
    QG_RESPONSE=$(curl -s "${SONAR_API}/qualitygates/project_status?projectKey=${PROJECT_KEY}&${QUERY_PARAM}" \
        -H "Authorization: Bearer ${SONAR_TOKEN}")
    QG_STATUS=$(echo "$QG_RESPONSE" | jq -r '.projectStatus.status // "UNKNOWN"')

    if [[ "$QG_STATUS" == "OK" ]]; then
        echo -e "${GREEN}✅ PASSED${NC}\n"
    elif [[ "$QG_STATUS" == "ERROR" ]]; then
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

    # 2. Overall Metrics
    echo -e "\n${BLUE}📈 Overall Metrics:${NC}"
    METRIC_KEYS="bugs,vulnerabilities,code_smells,security_hotspots,duplicated_lines_density,coverage,ncloc"
    METRICS_RESPONSE=$(curl -s "${SONAR_API}/measures/component?component=${PROJECT_KEY}&${QUERY_PARAM}&metricKeys=${METRIC_KEYS}" \
        -H "Authorization: Bearer ${SONAR_TOKEN}")
    echo "$METRICS_RESPONSE" | jq -r '.component.measures[]? | "  \(.metric): \(.value // "N/A")"' 2>/dev/null \
        || echo "  No metrics available"

    # 3. All Issues Summary
    echo -e "\n${BLUE}🐛 Issues Summary (all existing on branch):${NC}"
    ISSUES_RESPONSE=$(curl -s "${SONAR_API}/issues/search?componentKeys=${PROJECT_KEY}&${QUERY_PARAM}&resolved=false&ps=100&s=SEVERITY&asc=false" \
        -H "Authorization: Bearer ${SONAR_TOKEN}")
    TOTAL_ISSUES=$(echo "$ISSUES_RESPONSE" | jq -r '.total // 0')
    echo -e "  Total Issues: ${YELLOW}${TOTAL_ISSUES}${NC}"

    if [[ "$TOTAL_ISSUES" -gt 0 ]]; then
        echo ""
        echo -e "${YELLOW}Issues by severity:${NC}"
        curl -s "${SONAR_API}/issues/search?componentKeys=${PROJECT_KEY}&${QUERY_PARAM}&resolved=false&facets=severities&ps=1" \
            -H "Authorization: Bearer ${SONAR_TOKEN}" | \
            jq -r '.facets[]? | select(.property == "severities") | .values[] | select(.count > 0) | "  \(.val): \(.count)"' 2>/dev/null

        echo ""
        echo -e "${YELLOW}Issues by type:${NC}"
        curl -s "${SONAR_API}/issues/search?componentKeys=${PROJECT_KEY}&${QUERY_PARAM}&resolved=false&facets=types&ps=1" \
            -H "Authorization: Bearer ${SONAR_TOKEN}" | \
            jq -r '.facets[]? | select(.property == "types") | .values[] | select(.count > 0) | "  \(.val): \(.count)"' 2>/dev/null

        echo ""
        echo -e "${YELLOW}Top 10 Issues:${NC}"
        echo "$ISSUES_RESPONSE" | jq -r '.issues[0:10]? | .[] |
            "\n[\(.severity)] \(.type): \(.message)\n  File: \(.component | split(":")[1] // .component) (Line \(.line // "N/A"))\n  Rule: \(.rule)"'
    fi

    # 4. Links
    echo -e "\n${BLUE}🔗 Links:${NC}"
    echo "  Dashboard: https://sonarcloud.io/dashboard?id=${PROJECT_KEY}&branch=${BRANCH_NAME}"
    echo "  Issues:    https://sonarcloud.io/project/issues?id=${PROJECT_KEY}&branch=${BRANCH_NAME}&resolved=false"

    # Summary
    echo -e "\n${BLUE}=== Summary ===${NC}"
    if [[ "$QG_STATUS" == "OK" ]]; then
        echo -e "${GREEN}✅ ${DISPLAY_NAME} is healthy (quality gate passed)${NC}"
    elif [[ "$QG_STATUS" == "ERROR" ]]; then
        echo -e "${RED}❌ ${DISPLAY_NAME} has quality gate failures${NC}"
        echo -e "${YELLOW}   Please fix the issues above${NC}"
    else
        echo -e "${YELLOW}⚠️  ${DISPLAY_NAME} status: ${QG_STATUS}${NC}"
        echo -e "   Note: Branch may not have been analyzed yet by SonarCloud CI."
    fi

    echo ""
    exit 0
fi

# ============================================================
# PR MODE — New code only (feature branches), waits for analysis
# ============================================================
QUERY_PARAM="pullRequest=${PR_NUMBER}"
DISPLAY_NAME="PR #${PR_NUMBER}"

echo -e "${BLUE}=== SonarCloud Analysis for ${DISPLAY_NAME} (new code only) ===${NC}\n"

# Wait for SonarCloud analysis to complete
# First do a quick check — if already done, skip the wait
QUICK_STATUS=$(curl -s "${SONAR_API}/qualitygates/project_status?projectKey=${PROJECT_KEY}&${QUERY_PARAM}" \
    -H "Authorization: Bearer ${SONAR_TOKEN}" | jq -r '.projectStatus.status // "NONE"')

ANALYSIS_DONE=false
if [[ "$QUICK_STATUS" != "NONE" && "$QUICK_STATUS" != "UNKNOWN" ]]; then
    echo -e "${GREEN}✅ Analysis already complete${NC}\n"
    ANALYSIS_DONE=true
else
    echo -e "${BLUE}⏳ Waiting for SonarCloud analysis to complete...${NC}"
    echo -e "  Polling every 15s (timeout: 5 min). Start a new Sonar scan if this takes too long."
    echo ""

    MAX_WAIT=300  # 5 minutes
    INTERVAL=15
    ELAPSED=0

    while [[ $ELAPSED -lt $MAX_WAIT ]]; do
        sleep $INTERVAL
        ELAPSED=$((ELAPSED + INTERVAL))

        POLL_STATUS=$(curl -s "${SONAR_API}/qualitygates/project_status?projectKey=${PROJECT_KEY}&${QUERY_PARAM}" \
            -H "Authorization: Bearer ${SONAR_TOKEN}" | jq -r '.projectStatus.status // "NONE"')

        if [[ "$POLL_STATUS" != "NONE" && "$POLL_STATUS" != "UNKNOWN" ]]; then
            ANALYSIS_DONE=true
            echo -e "  ${GREEN}✅ Analysis complete after ${ELAPSED}s${NC}\n"
            break
        fi

        echo -e "  Still waiting... (${ELAPSED}s elapsed, status: ${POLL_STATUS})"
    done

    if [[ "$ANALYSIS_DONE" == false ]]; then
        echo -e "${YELLOW}⚠️  Analysis not complete after ${MAX_WAIT}s. Showing last known status.${NC}"
        echo -e "  Make sure the Sonar CI job was triggered for this PR.\n"
    fi
fi

# 1. Quality Gate
echo -e "${BLUE}📊 Quality Gate Status:${NC}"
QG_RESPONSE=$(curl -s "${SONAR_API}/qualitygates/project_status?projectKey=${PROJECT_KEY}&${QUERY_PARAM}" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")
QG_STATUS=$(echo "$QG_RESPONSE" | jq -r '.projectStatus.status // "UNKNOWN"')

if [[ "$QG_STATUS" == "OK" ]]; then
    echo -e "${GREEN}✅ PASSED${NC}\n"
elif [[ "$QG_STATUS" == "ERROR" ]]; then
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

# 2. New Code Metrics
echo -e "\n${BLUE}📈 New Code Metrics:${NC}"
METRIC_KEYS="new_coverage,new_bugs,new_vulnerabilities,new_code_smells,new_security_hotspots,new_lines,new_technical_debt,new_duplicated_lines_density"
METRICS_RESPONSE=$(curl -s "${SONAR_API}/measures/component?component=${PROJECT_KEY}&${QUERY_PARAM}&metricKeys=${METRIC_KEYS}" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")
echo "$METRICS_RESPONSE" | jq -r '.component.measures[]? |
    "  \(.metric): \(.value // .periods[0].value // "N/A")"' 2>/dev/null || echo "  No metrics available"

# 3. New Issues (changed lines only)
echo -e "\n${BLUE}🐛 New Issues (changed lines only):${NC}"
ISSUES_RESPONSE=$(curl -s "${SONAR_API}/issues/search?componentKeys=${PROJECT_KEY}&${QUERY_PARAM}&resolved=false&ps=100&s=SEVERITY&asc=false" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")
TOTAL_ISSUES=$(echo "$ISSUES_RESPONSE" | jq -r '.total // 0')
echo -e "  Total New Issues: ${YELLOW}${TOTAL_ISSUES}${NC}"

if [[ "$TOTAL_ISSUES" -gt 0 ]]; then
    echo ""
    echo -e "${YELLOW}Top 10 New Issues:${NC}"
    echo "$ISSUES_RESPONSE" | jq -r '.issues[0:10]? | .[] |
        "\n[\(.severity)] \(.type): \(.message)\n  File: \(.component | split(":")[1] // .component) (Line \(.line // "N/A"))\n  Rule: \(.rule)\n  Effort: \(.effort // "N/A")"'
fi

# 4. Code Duplications (new code)
echo -e "\n${BLUE}📋 Code Duplications (new code):${NC}"
DUP_METRICS=$(curl -s "${SONAR_API}/measures/component?component=${PROJECT_KEY}&${QUERY_PARAM}&metricKeys=new_duplicated_blocks,new_duplicated_lines,new_duplicated_lines_density" \
    -H "Authorization: Bearer ${SONAR_TOKEN}")

NEW_DUP_BLOCKS=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_blocks") | .periods[0].value // "0"')
NEW_DUP_LINES=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_lines") | .periods[0].value // "0"')
NEW_DUP_DENSITY=$(echo "$DUP_METRICS" | jq -r '.component.measures[] | select(.metric == "new_duplicated_lines_density") | .periods[0].value // "0"')

echo -e "  New duplicated blocks: ${YELLOW}${NEW_DUP_BLOCKS:-0}${NC}"
echo -e "  New duplicated lines: ${YELLOW}${NEW_DUP_LINES:-0}${NC}"
echo -e "  Duplication density: ${YELLOW}${NEW_DUP_DENSITY:-0}%${NC}"

if [[ "${NEW_DUP_BLOCKS:-0}" != "0" && "${NEW_DUP_BLOCKS:-0}" != "" ]]; then
    echo ""
    echo -e "${YELLOW}Duplication details (from PR files):${NC}"

    PR_FILES=$(gh pr view "${PR_NUMBER}" --json files -q '.files[].path' 2>/dev/null | grep -E '\.(cpp|cppm|h|hpp|c)$' || true)
    if [[ -n "$PR_FILES" ]]; then
        FOUND_DUP=false
        for FILE_PATH in $PR_FILES; do
            FILE_KEY="${PROJECT_KEY}:${FILE_PATH}"
            DUP_DETAILS=$(curl -s "${SONAR_API}/duplications/show?key=${FILE_KEY}" \
                -H "Authorization: Bearer ${SONAR_TOKEN}" 2>/dev/null)
            DUP_COUNT=$(echo "$DUP_DETAILS" | jq '.duplications | length' 2>/dev/null || echo "0")
            if [[ "${DUP_COUNT:-0}" -gt 0 ]]; then
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
        if [[ "$FOUND_DUP" == false ]]; then
            echo "  No duplications found in PR source files"
        fi
    else
        echo "  No source files found in PR"
    fi
fi

# 5. Links
echo -e "\n${BLUE}🔗 Links:${NC}"
echo "  Dashboard: https://sonarcloud.io/dashboard?id=${PROJECT_KEY}&pullRequest=${PR_NUMBER}"
echo "  Issues:    https://sonarcloud.io/project/issues?pullRequest=${PR_NUMBER}&id=${PROJECT_KEY}&resolved=false"

# Summary
echo -e "\n${BLUE}=== Summary ===${NC}"
if [[ "$QG_STATUS" == "OK" ]]; then
    echo -e "${GREEN}✅ ${DISPLAY_NAME} is ready (quality gate passed)${NC}"
elif [[ "$QG_STATUS" == "ERROR" ]]; then
    echo -e "${RED}❌ ${DISPLAY_NAME} needs attention (quality gate failed)${NC}"
    echo -e "${YELLOW}   Please fix the issues above${NC}"
else
    echo -e "${YELLOW}⚠️  ${DISPLAY_NAME} status: ${QG_STATUS}${NC}"
fi

echo ""
