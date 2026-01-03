#!/bin/bash
# Parallel Benchmark Runner for Storm ORM
# Runs multiple benchmarks in parallel using available CPU cores
#
# Usage:
#   ./bench_parallel.sh [filter] [iterations]
#
# Examples:
#   ./bench_parallel.sh select          # Run all SELECT tests in parallel
#   ./bench_parallel.sh select 1000     # Run with 1000 iterations each
#   ./bench_parallel.sh                 # List available test patterns

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Detect benchmark executable
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Try release build first, then debug
if [[ -x "$PROJECT_ROOT/build/release/benchmarks/storm_bench" ]]; then
    BENCH_EXE="$PROJECT_ROOT/build/release/benchmarks/storm_bench"
    BUILD_TYPE="Release"
elif [[ -x "$PROJECT_ROOT/build/debug/benchmarks/storm_bench" ]]; then
    BENCH_EXE="$PROJECT_ROOT/build/debug/benchmarks/storm_bench"
    BUILD_TYPE="Debug"
    echo -e "${YELLOW}WARNING: Using Debug build - results will not be representative!${NC}"
else
    echo -e "${RED}ERROR: Benchmark executable not found!${NC}"
    echo "Please build first:"
    echo "  cmake --preset ninja-release -DENABLE_BENCH=ON"
    echo "  cmake --build --preset ninja-release"
    exit 1
fi

# Detect CPU cores
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo -e "${CYAN}Detected ${CORES} CPU cores${NC}"

# Parse arguments
FILTER="${1:-}"
ITERATIONS="${2:-1000}"

# If no filter, show available test patterns
if [[ -z "$FILTER" ]]; then
    echo -e "\n${BLUE}=== Storm ORM Parallel Benchmark Runner ===${NC}"
    echo -e "Usage: $0 <filter> [iterations]"
    echo -e "\nAvailable test patterns:"
    echo -e "  ${GREEN}select${NC}          - SELECT tests (100, 1000, 10000, 100000 rows)"
    echo -e "  ${GREEN}select_join${NC}     - SELECT with JOIN tests"
    echo -e "  ${GREEN}insert_batch${NC}    - Batch INSERT tests"
    echo -e "  ${GREEN}delete_pk${NC}       - DELETE by primary key tests"
    echo -e "  ${GREEN}update_pk${NC}       - UPDATE by primary key tests"
    echo -e "  ${GREEN}aggregate${NC}       - Aggregate function tests"
    echo -e "  ${GREEN}distinct${NC}        - DISTINCT tests"
    echo -e "  ${GREEN}where${NC}           - WHERE clause tests"
    echo -e "\nExamples:"
    echo -e "  $0 select              # Run all SELECT tests in parallel"
    echo -e "  $0 select 500          # With 500 iterations"
    echo -e "  $0 insert_batch 100    # Batch INSERT tests with 100 iterations"
    echo ""
    exit 0
fi

echo -e "\n${BLUE}=== Storm ORM Parallel Benchmark ===${NC}"
echo -e "Filter: ${YELLOW}${FILTER}${NC}"
echo -e "Iterations: ${YELLOW}${ITERATIONS}${NC}"
echo -e "Build: ${YELLOW}${BUILD_TYPE}${NC}"
echo -e "Parallelism: ${YELLOW}${CORES} processes${NC}"
echo ""

# Get list of tests matching filter
TESTS=$("$BENCH_EXE" --list 2>/dev/null | grep -E "^${FILTER}" | awk '{print $1}' || true)

if [[ -z "$TESTS" ]]; then
    # Try substring match
    TESTS=$("$BENCH_EXE" --list 2>/dev/null | grep "${FILTER}" | awk '{print $1}' || true)
fi

if [[ -z "$TESTS" ]]; then
    echo -e "${RED}No tests found matching '${FILTER}'${NC}"
    echo "Run '$0' without arguments to see available patterns."
    exit 1
fi

# Count tests
TEST_COUNT=$(echo "$TESTS" | wc -l)
echo -e "${GREEN}Found ${TEST_COUNT} tests matching '${FILTER}'${NC}"
echo ""

# Create temp directory for results
RESULTS_DIR=$(mktemp -d)
trap "rm -rf $RESULTS_DIR" EXIT

# Function to run a single benchmark and save output
run_single_benchmark() {
    local test_name="$1"
    local iterations="$2"
    local result_file="$3"
    local db_file=$(mktemp)

    # Run benchmark with its own database file (thread safety)
    "$BENCH_EXE" --filter="$test_name" --iterations="$iterations" --db="$db_file" > "$result_file" 2>&1
    local status=$?

    # Cleanup database file
    rm -f "$db_file"

    return $status
}

export -f run_single_benchmark
export BENCH_EXE ITERATIONS

# Start time
START_TIME=$(date +%s.%N)

# Run benchmarks in parallel
echo -e "${CYAN}Running ${TEST_COUNT} benchmarks in parallel (max ${CORES} concurrent)...${NC}"
echo ""

# Use GNU parallel if available, otherwise fall back to xargs
if command -v parallel &> /dev/null; then
    echo "$TESTS" | parallel -j "$CORES" --bar \
        "run_single_benchmark {} $ITERATIONS $RESULTS_DIR/{}.txt"
else
    # Fallback to xargs with background jobs
    echo "$TESTS" | xargs -P "$CORES" -I {} bash -c \
        "run_single_benchmark '{}' '$ITERATIONS' '$RESULTS_DIR/{}.txt'"
fi

# End time
END_TIME=$(date +%s.%N)
ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)

echo ""
echo -e "${GREEN}=== All benchmarks completed in ${ELAPSED}s ===${NC}"
echo ""

# Aggregate and display results
echo -e "${BLUE}=== Results Summary ===${NC}"
echo ""

# Strip ANSI codes helper
strip_ansi() {
    sed 's/\x1b\[[0-9;]*m//g'
}

# Parse and display each result
for test in $TESTS; do
    result_file="$RESULTS_DIR/${test}.txt"
    if [[ -f "$result_file" ]]; then
        # Strip ANSI codes for parsing
        clean_output=$(cat "$result_file" | strip_ansi)

        # Extract key metrics from output
        storm_median=$(echo "$clean_output" | grep -A3 "Storm ORM:" | grep "Median:" | head -1 | grep -oE "[0-9]+\.[0-9]+ M ops/sec" || echo "N/A")
        raw_median=$(echo "$clean_output" | grep -A3 "Raw SQLite:" | grep "Median:" | head -1 | grep -oE "[0-9]+\.[0-9]+ M ops/sec" || echo "N/A")
        efficiency=$(echo "$clean_output" | grep "Efficiency:" | head -1 | grep -oE "[0-9]+\.[0-9]+%" || echo "N/A")
        dataset=$(echo "$clean_output" | grep "Dataset:" | head -1 | grep -oE "[0-9]+ rows" || echo "")

        echo -e "${CYAN}$test${NC} ($dataset)"
        echo -e "  Storm:      ${GREEN}$storm_median${NC}"
        echo -e "  Raw SQLite: ${YELLOW}$raw_median${NC}"
        echo -e "  Efficiency: ${GREEN}$efficiency${NC}"
        echo ""
    else
        echo -e "${RED}$test: FAILED (no output)${NC}"
    fi
done

# Create CSV summary
CSV_FILE="$PROJECT_ROOT/benchmarks/results_${FILTER}_$(date +%Y%m%d_%H%M%S).csv"
echo "test_name,dataset_size,storm_median_mops,raw_median_mops,efficiency_percent" > "$CSV_FILE"

for test in $TESTS; do
    result_file="$RESULTS_DIR/${test}.txt"
    if [[ -f "$result_file" ]]; then
        clean_output=$(cat "$result_file" | strip_ansi)
        storm_val=$(echo "$clean_output" | grep -A3 "Storm ORM:" | grep "Median:" | grep -oE "[0-9]+\.[0-9]+" | head -1 || echo "0")
        raw_val=$(echo "$clean_output" | grep -A3 "Raw SQLite:" | grep "Median:" | grep -oE "[0-9]+\.[0-9]+" | head -1 || echo "0")
        eff_val=$(echo "$clean_output" | grep "Efficiency:" | grep -oE "[0-9]+\.[0-9]+" | head -1 || echo "0")
        dataset_val=$(echo "$clean_output" | grep "Dataset:" | grep -oE "[0-9]+" | head -1 || echo "0")
        echo "$test,$dataset_val,$storm_val,$raw_val,$eff_val" >> "$CSV_FILE"
    fi
done

echo -e "${GREEN}Results saved to: ${CSV_FILE}${NC}"
