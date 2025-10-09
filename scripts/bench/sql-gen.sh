#!/bin/bash

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Change to project root (two levels up from scripts/bench/)
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Source table drawing library
source "${PROJECT_ROOT}/scripts/table_utils.sh"

echo "=== STORM ORM SQL GENERATION PERFORMANCE ANALYSIS ==="
echo "Analyzing compile-time SQL generation and cache optimization performance"
echo ""

# Colors for output formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Function to print colored output
print_step() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to colorize cache hit status
colorize_cache_status() {
    local status="$1"
    if [[ "$status" == "Likely" ]]; then
        echo -e "${GREEN}✓ Cache Hit${NC}"
    else
        echo -e "${YELLOW}✗ Cache Miss${NC}"
    fi
}

# Function to colorize time based on performance
colorize_time() {
    local time="$1"
    # Remove any non-numeric characters for comparison
    local clean_time=$(echo "$time" | sed 's/[^0-9.]//g')

    # Use bc for floating point comparison
    if (( $(echo "$clean_time < 50" | bc -l) )); then
        echo -e "${GREEN}${time}${NC}"
    elif (( $(echo "$clean_time < 100" | bc -l) )); then
        echo -e "${BLUE}${time}${NC}"
    elif (( $(echo "$clean_time < 200" | bc -l) )); then
        echo -e "${YELLOW}${time}${NC}"
    else
        echo -e "${RED}${time}${NC}"
    fi
}

# Function to colorize speedup
colorize_speedup() {
    local speedup="$1"
    # Remove the 'x' suffix for comparison
    local clean_speedup=$(echo "$speedup" | sed 's/x$//')

    if (( $(echo "$clean_speedup > 10" | bc -l) )); then
        echo -e "${GREEN}${speedup}${NC}"
    elif (( $(echo "$clean_speedup > 5" | bc -l) )); then
        echo -e "${BLUE}${speedup}${NC}"
    elif (( $(echo "$clean_speedup > 2" | bc -l) )); then
        echo -e "${YELLOW}${speedup}${NC}"
    else
        echo -e "${RED}${speedup}${NC}"
    fi
}

# Step 1: Check if benchmark exists
print_step "Step 1: Checking for sql_generation_microbench..."
BENCHMARK_PATH="build/debug/benchmarks/sql_generation_microbench"

if [[ ! -x "$BENCHMARK_PATH" ]]; then
    print_warning "sql_generation_microbench not found. Building it..."

    # Configure with benchmarking enabled
    print_step "Configuring build with benchmarking enabled..."
    if cmake --preset ninja-debug -DENABLE_TESTS=ON -DENABLE_BENCH=ON; then
        print_success "CMake configuration completed"
    else
        print_error "CMake configuration failed"
        exit 1
    fi

    echo ""

    # Build the specific benchmark
    print_step "Building sql_generation_microbench..."
    if cmake --build --preset ninja-debug --target sql_generation_microbench; then
        print_success "sql_generation_microbench built successfully"
    else
        print_error "Failed to build sql_generation_microbench"
        exit 1
    fi
else
    print_success "sql_generation_microbench found"
fi

echo ""

# Step 2: Run the benchmark and capture output
print_step "Step 2: Running SQL generation performance analysis..."
echo ""

BENCHMARK_OUTPUT=$(./$BENCHMARK_PATH 2>&1)

# Step 3: Parse and format the results
echo "╔════════════════════════════════════════════════════════════════════════════════════════╗"
echo -e "║$(pad_string "${CYAN} SQL GENERATION MICRO-BENCHMARK RESULTS${NC}" 88 "center")║"
echo "╠════════════════════════════════════════════════════════════════════════════════════════╣"
echo ""

# Parse batch size performance table
echo -e "${MAGENTA}▶ BATCH SIZE PERFORMANCE ANALYSIS${NC}"
echo "┌────────────┬─────────────────────┬─────────────────┬──────────────┐"
echo "│ Batch Size │ SQL Gen Time (μs)   │ Cache Status    │ SQL Length   │"
echo "├────────────┼─────────────────────┼─────────────────┼──────────────┤"

# Extract batch size data
echo "$BENCHMARK_OUTPUT" | grep -E "^[[:space:]]*[0-9]+ \|" | while IFS='|' read -r batch_size gen_time cache_hit sql_length; do
    # Trim whitespace
    batch_size=$(echo "$batch_size" | xargs)
    gen_time=$(echo "$gen_time" | xargs)
    cache_hit=$(echo "$cache_hit" | xargs)
    sql_length=$(echo "$sql_length" | xargs)

    # Colorize values
    colored_time=$(colorize_time "$gen_time")
    colored_cache=$(colorize_cache_status "$cache_hit")

    # Format batch size with color based on common sizes
    if [[ "$batch_size" == "1" || "$batch_size" == "10" || "$batch_size" == "25" || "$batch_size" == "50" ]]; then
        colored_batch="${CYAN}${batch_size}${NC}"
    else
        colored_batch="$batch_size"
    fi

    # Output formatted row
    batch_padded=$(pad_string "$colored_batch" 10 "right")
    time_padded=$(pad_string "$colored_time μs" 19 "right")
    cache_padded=$(pad_string "$colored_cache" 15 "center")
    length_padded=$(pad_string "$sql_length" 12 "right")

    echo -e "│ ${batch_padded} │ ${time_padded} │ ${cache_padded} │ ${length_padded} │"
done

echo "└────────────┴─────────────────────┴─────────────────┴──────────────┘"
echo ""

# Parse cache effectiveness test results
echo -e "${MAGENTA}▶ CACHE EFFECTIVENESS TEST (100 iterations per batch size)${NC}"
echo "┌────────────┬──────────────┬──────────────┬──────────────┬──────────────┐"
echo "│ Batch Size │ Average (μs) │ Min (μs)     │ Max (μs)     │ Speedup      │"
echo "├────────────┼──────────────┼──────────────┼──────────────┼──────────────┤"

# Extract cache effectiveness data
echo "$BENCHMARK_OUTPUT" | grep -A4 "Batch size [0-9]" | while read -r line; do
    if [[ "$line" =~ Batch[[:space:]]size[[:space:]]([0-9]+) ]]; then
        batch_size="${BASH_REMATCH[1]}"
        # Read the next 4 lines
        read -r avg_line
        read -r min_line
        read -r max_line
        read -r speedup_line

        # Extract values
        avg_time=$(echo "$avg_line" | grep -oE '[0-9]+\.[0-9]+' | head -1)
        min_time=$(echo "$min_line" | grep -oE '[0-9]+\.[0-9]+' | head -1)
        max_time=$(echo "$max_line" | grep -oE '[0-9]+\.[0-9]+' | head -1)
        speedup=$(echo "$speedup_line" | grep -oE '[0-9]+\.[0-9]+x' | head -1)

        # Colorize values
        colored_avg=$(colorize_time "$avg_time")
        colored_min=$(colorize_time "$min_time")
        colored_max=$(colorize_time "$max_time")
        colored_speedup=$(colorize_speedup "$speedup")
        colored_batch="${CYAN}${batch_size}${NC}"

        # Output formatted row
        batch_padded=$(pad_string "$colored_batch" 10 "center")
        avg_padded=$(pad_string "$colored_avg" 12 "right")
        min_padded=$(pad_string "$colored_min" 12 "right")
        max_padded=$(pad_string "$colored_max" 12 "right")
        speedup_padded=$(pad_string "$colored_speedup" 12 "center")

        echo -e "│ ${batch_padded} │ ${avg_padded} │ ${min_padded} │ ${max_padded} │ ${speedup_padded} │"
    fi
done

echo "└────────────┴──────────────┴──────────────┴──────────────┴──────────────┘"
echo ""

# Display optimization impact analysis
echo -e "${MAGENTA}▶ OPTIMIZATION IMPACT ANALYSIS${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"
echo "│                     Key Optimizations Implemented                        │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}✓${NC} Compile-time SQL prefix generation (ConstexprString)                  │"
echo -e "│ ${GREEN}✓${NC} Pre-computed field names and placeholders                             │"
echo -e "│ ${GREEN}✓${NC} Thread-local 8-entry cache with round-robin replacement               │"
echo -e "│ ${GREEN}✓${NC} Memory pre-allocation using exact size calculation                    │"
echo -e "│ ${GREEN}✓${NC} Value template reuse for bulk INSERT operations                       │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

# Performance characteristics legend
echo -e "${MAGENTA}▶ PERFORMANCE CHARACTERISTICS LEGEND${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"
echo "│                         Time Color Coding                                │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}█████${NC} < 50μs    : Excellent (likely cache hit)                          │"
echo -e "│ ${BLUE}█████${NC} 50-100μs  : Good (optimized generation)                           │"
echo -e "│ ${YELLOW}█████${NC} 100-200μs : Acceptable (cache miss, small batch)                 │"
echo -e "│ ${RED}█████${NC} > 200μs   : Slow (large batch generation)                         │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo "│                        Speedup Color Coding                              │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}█████${NC} > 10x     : Excellent cache effectiveness                         │"
echo -e "│ ${BLUE}█████${NC} 5-10x     : Good cache performance                                │"
echo -e "│ ${YELLOW}█████${NC} 2-5x      : Moderate cache benefit                                │"
echo -e "│ ${RED}█████${NC} < 2x      : Poor cache utilization                                │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

# Summary statistics
echo -e "${MAGENTA}▶ SUMMARY STATISTICS${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"

# Calculate some summary stats from the output
CACHE_HIT_COUNT=$(echo "$BENCHMARK_OUTPUT" | grep -c "Likely" || true)
CACHE_MISS_COUNT=$(echo "$BENCHMARK_OUTPUT" | grep -c "Unlikely" || true)
TOTAL_TESTS=$((CACHE_HIT_COUNT + CACHE_MISS_COUNT))

if [[ $TOTAL_TESTS -gt 0 ]]; then
    CACHE_HIT_RATE=$(echo "scale=1; ($CACHE_HIT_COUNT * 100) / $TOTAL_TESTS" | bc)
    echo -e "│ Cache Hit Rate: ${GREEN}${CACHE_HIT_RATE}%${NC} (${CACHE_HIT_COUNT}/${TOTAL_TESTS} operations)$(printf '%*s' $((36 - ${#CACHE_HIT_RATE} - ${#CACHE_HIT_COUNT} - ${#TOTAL_TESTS})) '')│"
else
    echo "│ Cache Hit Rate: N/A                                                      │"
fi

echo "│                                                                          │"
echo "│ Common Batch Sizes (1, 10, 25, 50):                                     │"
echo -e "│   • Optimized for ${GREEN}maximum cache reuse${NC}                                  │"
echo -e "│   • ${CYAN}Pre-allocated in thread-local cache${NC}                                │"
echo -e "│   • ${BLUE}Sub-50μs generation time when cached${NC}                              │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

print_success "SQL generation performance analysis completed!"
echo ""

# Build information
echo "╔════════════════════════════════════════════════════════════════════════════════════════╗"
echo -e "║$(pad_string "${CYAN} BUILD CONFIGURATION${NC}" 88 "center")║"
echo "╠════════════════════════════════════════════════════════════════════════════════════════╣"
echo "║ Compiler    : Clang with C++26 reflection support (../clang-p2996/)                    ║"
echo "║ C++ Standard: C++26 with std::meta reflection                                          ║"
echo "║ Build Type  : Debug                                                                    ║"
echo "║ Optimization: Compile-time SQL generation with thread-local caching                    ║"
echo "╚════════════════════════════════════════════════════════════════════════════════════════╝"
echo ""

echo "To re-run this analysis:"
echo "  ./sql_generation_analysis.sh"
echo ""
echo "To run the benchmark directly:"
echo "  ./build/debug/benchmarks/sql_generation_microbench"