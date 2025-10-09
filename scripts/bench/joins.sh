#!/bin/bash

# JOIN Performance Analysis: Storm ORM vs Raw SQLite
# Beautiful color-coded comparison table

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Change to project root (two levels up from scripts/bench/)
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
WHITE='\033[1;37m'
GRAY='\033[0;90m'
NC='\033[0m'
BOLD='\033[1m'
DIM='\033[2m'

# Color functions for performance ratios
color_ratio() {
    local ratio=$1
    local value=$2

    if (( $(echo "$ratio >= 70" | bc -l) )); then
        echo -e "${GREEN}${value}${NC}"
    elif (( $(echo "$ratio >= 50" | bc -l) )); then
        echo -e "${YELLOW}${value}${NC}"
    else
        echo -e "${RED}${value}${NC}"
    fi
}

print_header() {
    echo -e "${BOLD}${CYAN}"
    echo "╔════════════════════════════════════════════════════════════════════════════════╗"
    echo "║                                                                                ║"
    echo "║            🚀 Storm ORM vs Raw SQLite JOIN Performance Analysis 🚀             ║"
    echo "║                                                                                ║"
    echo "╚════════════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_test_info() {
    local size=$1
    local iterations=$2

    echo -e "${BOLD}${WHITE}Test Configuration:${NC}"
    echo -e "  ${CYAN}├─${NC} Messages:   ${BOLD}$size${NC}"
    echo -e "  ${CYAN}├─${NC} Iterations: ${BOLD}$iterations${NC}"
    echo -e "  ${CYAN}└─${NC} Build:      ${BOLD}Release (-O3 -march=native)${NC}"
    echo ""
}

# Check if benchmark exists
if [ ! -f "build/release/benchmarks/bench_join_performance" ]; then
    echo -e "${YELLOW}⚠️  Benchmark not found. Building...${NC}"
    echo ""

    if [ ! -d "build/release" ]; then
        cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON
    fi

    cmake --build --preset ninja-release --target bench_join_performance

    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ Build failed!${NC}"
        exit 1
    fi
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
fi

# Parse arguments
SIZE=${1:-1000}
ITERATIONS=${2:-100}

clear
print_header
print_test_info "$SIZE" "$ITERATIONS"

echo -e "${BOLD}${MAGENTA}Running benchmarks...${NC}"
echo ""

# Run benchmark and capture output
./build/release/benchmarks/bench_join_performance --size=$SIZE --iterations=$ITERATIONS > /tmp/join_analysis.txt 2>&1

# Parse and display results
echo ""
echo -e "${BOLD}${CYAN}╔════════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${CYAN}║                          Performance Comparison Table                          ║${NC}"
echo -e "${BOLD}${CYAN}╚════════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Source table drawing library
source "${PROJECT_ROOT}/scripts/table_utils.sh"

# Parse throughput values from benchmark output using simple arrays
declare -a storm_inner_single storm_inner_multi storm_left_single storm_left_multi storm_right_single storm_right_multi
declare -a raw_inner_single raw_inner_multi raw_left_single raw_left_multi raw_right_single raw_right_multi

current_op=""
is_storm=1

while IFS= read -r line; do
    if [[ "$line" =~ "INNER JOIN (single FK" ]]; then
        current_op="inner_single"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ "INNER JOIN (multi FK" ]]; then
        current_op="inner_multi"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ "LEFT JOIN (single FK" ]]; then
        current_op="left_single"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ "LEFT JOIN (multi FK" ]]; then
        current_op="left_multi"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ "RIGHT JOIN (single FK" ]]; then
        current_op="right_single"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ "RIGHT JOIN (multi FK" ]]; then
        current_op="right_multi"
        [[ "$line" =~ "Raw SQLite" ]] && is_storm=0 || is_storm=1
    elif [[ "$line" =~ Throughput:[[:space:]]*([0-9]+) ]] && [[ -n "$current_op" ]]; then
        value="${BASH_REMATCH[1]}"
        if [[ $is_storm -eq 1 ]]; then
            eval "storm_${current_op}=$value"
        else
            eval "raw_${current_op}=$value"
        fi
    fi
done < /tmp/join_analysis.txt

# Define column widths (total including spaces and borders)
widths=(33 14 14 14)

# Define operations and their variable names
declare -a ops_names=("INNER JOIN (single FK)" "INNER JOIN (multi FK)" "LEFT JOIN (single FK)" "LEFT JOIN (multi FK)" "RIGHT JOIN (single FK)" "RIGHT JOIN (multi FK)")
declare -a ops_vars=("inner_single" "inner_multi" "left_single" "left_multi" "right_single" "right_multi")

# Draw table header
draw_table_top "${widths[@]}"

# Header row
header=("JOIN Operation" "${CYAN}Storm ORM${NC}" "${CYAN}Raw SQLite${NC}" "${CYAN}Efficiency${NC}")
draw_table_row widths header

draw_table_middle "${widths[@]}"

# Calculate totals
total_storm=0
total_raw=0
total_ratio=0
count=0
best_ratio=0
best_op=""

# Data rows
for i in "${!ops_names[@]}"; do
    op="${ops_names[i]}"
    op_var="${ops_vars[i]}"

    eval "storm=\$storm_${op_var}"
    eval "raw=\$raw_${op_var}"

    if [[ -n "$storm" && -n "$raw" ]]; then
        ratio=$(awk "BEGIN {printf \"%.1f\", ($storm * 100) / $raw}")

        total_storm=$((total_storm + storm))
        total_raw=$((total_raw + raw))
        total_ratio=$(awk "BEGIN {printf \"%.1f\", $total_ratio + $ratio}")
        count=$((count + 1))

        # Track best performer
        if (( $(awk "BEGIN {print ($ratio > $best_ratio) ? 1 : 0}") )); then
            best_ratio=$ratio
            best_op=$op
        fi

        # Color code ratio
        if (( $(awk "BEGIN {print ($ratio >= 70) ? 1 : 0}") )); then
            color=$GREEN
        elif (( $(awk "BEGIN {print ($ratio >= 50) ? 1 : 0}") )); then
            color=$YELLOW
        else
            color=$RED
        fi

        # Format values with ANSI-aware padding
        storm_m=$(awk "BEGIN {printf \"%.2f\", $storm / 1000000}")
        raw_m=$(awk "BEGIN {printf \"%.2f\", $raw / 1000000}")

        row_values=("$op" "${CYAN}${storm_m}M${NC}" "${CYAN}${raw_m}M${NC}" "${color}${ratio}%${NC}")
        draw_table_row widths row_values

        # Draw separator after INNER and LEFT
        if [[ $i -eq 1 || $i -eq 3 ]]; then
            draw_table_middle "${widths[@]}"
        fi
    fi
done

# Summary row
draw_table_middle "${widths[@]}"

avg_ratio=$(awk "BEGIN {printf \"%.1f\", $total_ratio / $count}")
avg_storm_m=$(awk "BEGIN {printf \"%.2f\", $total_storm / ($count * 1000000)}")
avg_raw_m=$(awk "BEGIN {printf \"%.2f\", $total_raw / ($count * 1000000)}")

# Color code average
if (( $(awk "BEGIN {print ($avg_ratio >= 70) ? 1 : 0}") )); then
    avg_color=$GREEN
elif (( $(awk "BEGIN {print ($avg_ratio >= 50) ? 1 : 0}") )); then
    avg_color=$YELLOW
else
    avg_color=$RED
fi

avg_row=("Average" "${CYAN}${avg_storm_m}M${NC}" "${CYAN}${avg_raw_m}M${NC}" "${avg_color}${avg_ratio}%${NC}")
draw_table_row widths avg_row

draw_table_bottom "${widths[@]}"

# Print statistics
echo ""
echo -e "${BOLD}${CYAN}📊 Performance Statistics:${NC}"
echo -e "   ${CYAN}├─${NC} Total Operations:   ${WHITE}${count}${NC}"
echo -e "   ${CYAN}├─${NC} Average Efficiency: ${avg_color}${avg_ratio}%${NC}"
echo -e "   ${CYAN}└─${NC} Best Performer:    ${GREEN}${best_op} (${best_ratio}%)${NC}"

echo ""
echo -e "${BOLD}${CYAN}Legend:${NC}"
echo -e "  ${GREEN}█${NC} ${GREEN}≥70%${NC}  Excellent performance (close to raw SQLite)"
echo -e "  ${YELLOW}█${NC} ${YELLOW}50-70%${NC} Good performance (room for optimization)"
echo -e "  ${RED}█${NC} ${RED}<50%${NC}  Needs optimization"

echo ""
echo -e "${BOLD}${WHITE}Usage:${NC}"
echo -e "  ${CYAN}./join_performance_analysis.sh${NC} [size] [iterations]"
echo -e ""
echo -e "  ${GRAY}Examples:${NC}"
echo -e "    ${CYAN}./join_performance_analysis.sh${NC}              ${GRAY}# Default: 1000 messages, 100 iterations${NC}"
echo -e "    ${CYAN}./join_performance_analysis.sh${NC} 10000 50     ${GRAY}# 10K messages, 50 iterations${NC}"
echo -e "    ${CYAN}./join_performance_analysis.sh${NC} 100000 10    ${GRAY}# 100K messages, 10 iterations${NC}"
echo ""
