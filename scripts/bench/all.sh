#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Configuration
BUILD_DIR="build/release"
BENCHMARK_DIR="${BUILD_DIR}/benchmarks"
ITERATIONS=50
MESSAGE_COUNT=1000

# Function to print section header
print_header() {
    echo ""
    echo -e "${BOLD}${CYAN}========================================================================================================${NC}"
    echo -e "${BOLD}${CYAN}$1${NC}"
    echo -e "${BOLD}${CYAN}========================================================================================================${NC}"
    echo ""
}

# Function to print table header
print_table_header() {
    local title="$1"
    echo -e "${BOLD}${BLUE}${title}${NC}"
    echo -e "${BOLD}$(printf '%-50s %15s %20s %15s' "Operation" "Avg Time" "Throughput" "vs Baseline")${NC}"
    echo "$(printf '%0.s-' {1..105})"
}

# Function to print table row
print_table_row() {
    local operation="$1"
    local avg_time="$2"
    local throughput="$3"
    local vs_baseline="$4"

    printf "%-50s %15s %20s %15s\n" "$operation" "$avg_time" "$throughput" "$vs_baseline"
}

# Function to check if benchmark exists
check_benchmark() {
    local bench="$1"
    if [ ! -f "$bench" ]; then
        echo -e "${RED}Error: Benchmark not found: $bench${NC}"
        echo -e "${YELLOW}Please build release benchmarks first:${NC}"
        echo "  cmake --preset ninja-release -DENABLE_BENCH=ON"
        echo "  cmake --build --preset ninja-release"
        return 1
    fi
    return 0
}

# Check if benchmarks are built
echo -e "${YELLOW}Checking if benchmarks are built...${NC}"
if [ ! -f "${BENCHMARK_DIR}/bench_join_performance" ]; then
    echo -e "${RED}Benchmarks not found. Building release benchmarks...${NC}"
    cmake --preset ninja-release -DENABLE_BENCH=ON 2>&1 | tail -3
    cmake --build --preset ninja-release 2>&1 | tail -5
    echo ""
fi

# Main benchmark execution
print_header "STORM ORM MICROBENCHMARK SUITE"
echo -e "Configuration:"
echo -e "  Build: ${GREEN}Release${NC}"
echo -e "  Iterations: ${GREEN}${ITERATIONS}${NC}"
echo -e "  Dataset size: ${GREEN}${MESSAGE_COUNT} messages${NC}"
echo ""

# =============================================================================
# 1. JOIN PERFORMANCE BENCHMARKS
# =============================================================================
print_header "1. JOIN PERFORMANCE BENCHMARKS"

BENCH_JOIN="${BENCHMARK_DIR}/bench_join_performance"
if check_benchmark "$BENCH_JOIN"; then
    echo -e "${YELLOW}Running JOIN benchmarks...${NC}"
    output=$($BENCH_JOIN --size=$MESSAGE_COUNT --iterations=$ITERATIONS 2>&1)

    print_table_header "JOIN Operations (${MESSAGE_COUNT} messages)"

    # Parse and display results
    baseline_throughput=$(echo "$output" | grep "SELECT (no JOIN)" -A 4 | grep "Throughput:" | awk '{print $2}')
    baseline_time=$(echo "$output" | grep "SELECT (no JOIN)" -A 3 | grep "Avg per iteration:" | awk '{print $4, $5}')

    # Show Storm ORM operations
    echo -e "${GREEN}Storm ORM Operations:${NC}"
    echo "$output" | grep -E "(SELECT|INNER JOIN \(|LEFT JOIN \(|RIGHT JOIN \()" | grep -v "Raw SQLite" | while read line; do
        operation=$(echo "$line" | sed 's/ - .*//')
        avg_time=$(echo "$output" | grep -A 3 "$operation" | grep "Avg per iteration:" | awk '{print $4, $5}')
        throughput=$(echo "$output" | grep -A 4 "$operation" | grep "Throughput:" | awk '{print $2, $3}')

        if [ "$operation" != "SELECT (no JOIN)" ] && [ ! -z "$baseline_throughput" ]; then
            current_throughput=$(echo "$throughput" | awk '{print $1}')
            if [ ! -z "$current_throughput" ] && [ "$current_throughput" != "0" ]; then
                percent=$(awk "BEGIN {printf \"%.0f\", ($current_throughput / $baseline_throughput) * 100}")
                vs_baseline="${percent}%"
            else
                vs_baseline="N/A"
            fi
        else
            vs_baseline="100% (baseline)"
        fi

        print_table_row "$operation" "$avg_time" "$throughput" "$vs_baseline"
    done

    echo ""
    echo -e "${YELLOW}Raw SQLite Operations (comparison baseline):${NC}"
    echo "$output" | grep "Raw SQLite" | while read line; do
        operation=$(echo "$line" | sed 's/ - .*//')
        avg_time=$(echo "$output" | grep -A 3 "$operation" | grep "Avg per iteration:" | awk '{print $4, $5}')
        throughput=$(echo "$output" | grep -A 4 "$operation" | grep "Throughput:" | awk '{print $2, $3}')

        if [ ! -z "$baseline_throughput" ]; then
            current_throughput=$(echo "$throughput" | awk '{print $1}')
            if [ ! -z "$current_throughput" ] && [ "$current_throughput" != "0" ]; then
                percent=$(awk "BEGIN {printf \"%.0f\", ($current_throughput / $baseline_throughput) * 100}")
                vs_baseline="${percent}%"
            else
                vs_baseline="N/A"
            fi
        else
            vs_baseline="-"
        fi

        print_table_row "$operation" "$avg_time" "$throughput" "$vs_baseline"
    done

    echo ""
    echo -e "${GREEN}✓ JOIN benchmarks completed${NC}"
fi

# =============================================================================
# 2. SQL GENERATION MICROBENCHMARKS
# =============================================================================
print_header "2. SQL GENERATION PERFORMANCE"

BENCH_SQL_GEN="${BENCHMARK_DIR}/sql_generation_microbench"
if check_benchmark "$BENCH_SQL_GEN"; then
    echo -e "${YELLOW}Running SQL generation benchmarks...${NC}"
    output=$($BENCH_SQL_GEN 2>&1)

    print_table_header "SQL Generation & Caching"

    # Parse SQL generation results
    echo "$output" | grep -E "(Compile-time|Runtime|Cache hit|Cache miss)" | while IFS= read -r line; do
        if echo "$line" | grep -q "Compile-time"; then
            operation="Compile-time SQL (static)"
            time=$(echo "$line" | grep -oP '\d+\.\d+' | head -1)
            print_table_row "$operation" "${time} ns" "N/A" "baseline"
        elif echo "$line" | grep -q "Runtime.*no cache"; then
            operation="Runtime SQL (no cache)"
            time=$(echo "$line" | grep -oP '\d+\.\d+' | head -1)
            print_table_row "$operation" "${time} ns" "N/A" "-"
        elif echo "$line" | grep -q "Cache hit"; then
            operation="Runtime SQL (cache hit)"
            time=$(echo "$line" | grep -oP '\d+\.\d+' | head -1)
            print_table_row "$operation" "${time} ns" "N/A" "-"
        elif echo "$line" | grep -q "Cache miss"; then
            operation="Runtime SQL (cache miss)"
            time=$(echo "$line" | grep -oP '\d+\.\d+' | head -1)
            print_table_row "$operation" "${time} ns" "N/A" "-"
        fi
    done

    echo ""
    echo -e "${GREEN}✓ SQL generation benchmarks completed${NC}"
fi

# =============================================================================
# 3. JOIN MICROBENCHMARKS (Detail)
# =============================================================================
print_header "3. JOIN DETAILED MICROBENCHMARKS"

BENCH_JOIN_MICRO="${BENCHMARK_DIR}/join_performance_microbench"
if check_benchmark "$BENCH_JOIN_MICRO"; then
    echo -e "${YELLOW}Running detailed JOIN microbenchmarks...${NC}"
    output=$($BENCH_JOIN_MICRO 2>&1)

    print_table_header "JOIN Implementation Details"

    # Parse detailed JOIN results
    echo "$output" | grep -E "^(SQL Generation|Column Offset|Field Extraction|Full JOIN)" | while IFS= read -r line; do
        operation=$(echo "$line" | sed 's/:.*//')
        time=$(echo "$line" | grep -oP '\d+\.\d+' | head -1)
        unit=$(echo "$line" | grep -oP '(ns|µs|ms)' | head -1)

        print_table_row "$operation" "${time} ${unit}" "N/A" "-"
    done

    echo ""
    echo -e "${GREEN}✓ JOIN microbenchmarks completed${NC}"
fi

# =============================================================================
# 4. SELECT COMPARISON BENCHMARKS
# =============================================================================
print_header "4. SELECT PERFORMANCE COMPARISON"

BENCH_SELECT="${BENCHMARK_DIR}/bench_select_comparison"
if check_benchmark "$BENCH_SELECT"; then
    echo -e "${YELLOW}Running SELECT comparison benchmarks...${NC}"
    output=$($BENCH_SELECT 2>&1)

    print_table_header "SELECT Implementation Comparison"

    # Parse SELECT comparison results
    raw_sqlite_throughput=""
    echo "$output" | grep -E "(Raw SQLite|Storm ORM|Optimization)" | while IFS= read -r line; do
        if echo "$line" | grep -q "Raw SQLite"; then
            operation="Raw SQLite SELECT"
            time=$(echo "$output" | grep -A 2 "Raw SQLite" | grep "Avg:" | awk '{print $2, $3}')
            throughput=$(echo "$output" | grep -A 3 "Raw SQLite" | grep "Throughput:" | awk '{print $2, $3}')
            raw_sqlite_throughput=$(echo "$throughput" | awk '{print $1}')
            print_table_row "$operation" "$time" "$throughput" "100% (baseline)"
        elif echo "$line" | grep -q "Storm ORM"; then
            operation="Storm ORM SELECT"
            time=$(echo "$output" | grep -A 2 "Storm ORM" | grep "Avg:" | awk '{print $2, $3}')
            throughput=$(echo "$output" | grep -A 3 "Storm ORM" | grep "Throughput:" | awk '{print $2, $3}')

            if [ ! -z "$raw_sqlite_throughput" ]; then
                current=$(echo "$throughput" | awk '{print $1}')
                percent=$(awk "BEGIN {printf \"%.0f\", ($current / $raw_sqlite_throughput) * 100}" 2>/dev/null || echo "N/A")
                print_table_row "$operation" "$time" "$throughput" "${percent}%"
            else
                print_table_row "$operation" "$time" "$throughput" "-"
            fi
        fi
    done

    echo ""
    echo -e "${GREEN}✓ SELECT comparison benchmarks completed${NC}"
fi

# =============================================================================
# SUMMARY
# =============================================================================
print_header "BENCHMARK SUMMARY"

echo -e "${BOLD}Key Performance Metrics:${NC}"
echo ""

# JOIN Performance Summary
if [ -f "$BENCH_JOIN" ]; then
    output=$($BENCH_JOIN --size=$MESSAGE_COUNT --iterations=$ITERATIONS 2>&1)
    baseline=$(echo "$output" | grep "SELECT (no JOIN)" -A 4 | grep "Throughput:" | awk '{print $2}')
    inner_single=$(echo "$output" | grep "INNER JOIN (single FK" -A 4 | grep "Throughput:" | awk '{print $2}')
    inner_multi=$(echo "$output" | grep "INNER JOIN (multi FK" -A 4 | grep "Throughput:" | awk '{print $2}')

    echo -e "  ${CYAN}JOIN Performance:${NC}"
    echo -e "    • Baseline SELECT:      ${GREEN}${baseline} rows/sec${NC}"
    echo -e "    • INNER JOIN (1 FK):    ${GREEN}${inner_single} rows/sec${NC}"
    echo -e "    • INNER JOIN (2 FK):    ${GREEN}${inner_multi} rows/sec${NC}"
    echo ""
fi

# SQL Generation Summary
if [ -f "$BENCH_SQL_GEN" ]; then
    output=$($BENCH_SQL_GEN 2>&1)
    compile_time=$(echo "$output" | grep "Compile-time" | grep -oP '\d+\.\d+' | head -1)
    cache_hit=$(echo "$output" | grep "Cache hit" | grep -oP '\d+\.\d+' | head -1)

    if [ ! -z "$compile_time" ] && [ ! -z "$cache_hit" ]; then
        speedup=$(awk "BEGIN {printf \"%.1f\", $compile_time / $cache_hit}" 2>/dev/null || echo "N/A")
        echo -e "  ${CYAN}SQL Generation:${NC}"
        echo -e "    • Compile-time:         ${GREEN}${compile_time} ns${NC}"
        echo -e "    • Cache hit:            ${GREEN}${cache_hit} ns${NC}"
        echo -e "    • Cache speedup:        ${GREEN}${speedup}x${NC}"
        echo ""
    fi
fi

echo -e "${BOLD}${GREEN}✓ All microbenchmarks completed successfully!${NC}"
echo ""
