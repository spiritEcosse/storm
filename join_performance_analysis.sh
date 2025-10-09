#!/bin/bash

set -e  # Exit on any error

echo "=== STORM ORM JOIN PERFORMANCE ANALYSIS ==="
echo "Analyzing virtual method overhead in JOIN operations"
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

# Function to colorize overhead percentage
colorize_overhead() {
    local overhead="$1"
    # Remove the '%' suffix and handle negative values
    local clean_overhead=$(echo "$overhead" | sed 's/%$//' | sed 's/-//')
    local is_negative=$(echo "$overhead" | grep -q '^-' && echo "yes" || echo "no")

    # Convert to integer for comparison (multiply by 10 to preserve one decimal place)
    local overhead_int=$(echo "$clean_overhead * 10" | awk '{printf "%.0f", $0}')

    # Negative overhead (faster) - green
    if [[ "$is_negative" == "yes" ]]; then
        echo -e "${GREEN}${overhead}${NC}"
    # Less than 5% - excellent (green)
    elif [[ $overhead_int -lt 50 ]]; then
        echo -e "${GREEN}${overhead}${NC}"
    # 5-10% - acceptable (blue)
    elif [[ $overhead_int -lt 100 ]]; then
        echo -e "${BLUE}${overhead}${NC}"
    # 10-20% - moderate (yellow)
    elif [[ $overhead_int -lt 200 ]]; then
        echo -e "${YELLOW}${overhead}${NC}"
    # > 20% - significant (red)
    else
        echo -e "${RED}${overhead}${NC}"
    fi
}

# Function to colorize throughput
colorize_throughput() {
    local throughput="$1"
    # Remove " rows/sec" suffix and convert to integer
    local clean_throughput=$(echo "$throughput" | sed 's/ rows\/sec$//')
    local throughput_int=$(echo "$clean_throughput" | awk '{printf "%.0f", $0}')

    # > 10M rows/sec - excellent (green)
    if [[ $throughput_int -gt 10000000 ]]; then
        echo -e "${GREEN}${throughput}${NC}"
    # 5-10M rows/sec - good (blue)
    elif [[ $throughput_int -gt 5000000 ]]; then
        echo -e "${BLUE}${throughput}${NC}"
    # 1-5M rows/sec - acceptable (yellow)
    elif [[ $throughput_int -gt 1000000 ]]; then
        echo -e "${YELLOW}${throughput}${NC}"
    # < 1M rows/sec - slow (red)
    else
        echo -e "${RED}${throughput}${NC}"
    fi
}

# Step 1: Check if benchmark exists
print_step "Step 1: Checking for join_performance_microbench..."
BENCHMARK_PATH="build/debug/benchmarks/join_performance_microbench"

if [[ ! -x "$BENCHMARK_PATH" ]]; then
    print_warning "join_performance_microbench not found. Building it..."

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
    print_step "Building join_performance_microbench..."
    if cmake --build --preset ninja-debug --target join_performance_microbench; then
        print_success "join_performance_microbench built successfully"
    else
        print_error "Failed to build join_performance_microbench"
        exit 1
    fi
else
    print_success "join_performance_microbench found"
fi

echo ""

# Step 2: Run the benchmark and capture output
print_step "Step 2: Running JOIN performance analysis..."
echo ""

# Parse command line arguments for the benchmark
BENCH_ARGS=""
if [[ $# -gt 0 ]]; then
    BENCH_ARGS="$@"
    print_step "Running with custom arguments: $BENCH_ARGS"
fi

BENCHMARK_OUTPUT=$(./$BENCHMARK_PATH $BENCH_ARGS 2>&1)

# Step 3: Parse and format the results
echo "╔════════════════════════════════════════════════════════════════════════════════════════╗"
echo -e "║$(printf '%*s' 44 '')${CYAN}JOIN PERFORMANCE RESULTS${NC}$(printf '%*s' 44 '')║"
echo "╠════════════════════════════════════════════════════════════════════════════════════════╣"
echo ""

# Extract configuration
echo -e "${MAGENTA}▶ TEST CONFIGURATION${NC}"
CONFIG_SECTION=$(echo "$BENCHMARK_OUTPUT" | sed -n '/^Configuration:/,/^$/p')
echo "$CONFIG_SECTION"
echo ""

# Extract and format benchmark results
echo -e "${MAGENTA}▶ PERFORMANCE COMPARISON${NC}"
echo "┌──────────────────────────────┬──────────────────┬──────────────────┬──────────────────┐"
echo "│ Operation                    │ Time per Row     │ Throughput       │ Overhead         │"
echo "├──────────────────────────────┼──────────────────┼──────────────────┼──────────────────┤"

# Parse baseline (simple SELECT)
BASELINE_TIME=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Simple SELECT (baseline):" | grep "Time per row:" | awk '{print $4}')
BASELINE_THROUGHPUT=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Simple SELECT (baseline):" | grep "Throughput:" | awk '{print $2}')

colored_throughput=$(colorize_throughput "$BASELINE_THROUGHPUT rows/sec")
echo -e "│ ${CYAN}Simple SELECT (baseline)${NC}     │ ${BASELINE_TIME} μs$(printf '%*s' $((13 - ${#BASELINE_TIME})) '') │ ${colored_throughput}$(printf '%*s' $((7 - ${#BASELINE_THROUGHPUT})) '') │ $(printf '%-16s' '---')│"

# Parse single FK JOIN
SINGLE_TIME=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Single FK JOIN:" | grep "Time per row:" | awk '{print $4}')
SINGLE_THROUGHPUT=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Single FK JOIN:" | grep "Throughput:" | awk '{print $2}')
SINGLE_OVERHEAD=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Single FK JOIN:" | grep "Overhead vs" | awk '{print $5}')

colored_single_throughput=$(colorize_throughput "$SINGLE_THROUGHPUT rows/sec")
colored_single_overhead=$(colorize_overhead "$SINGLE_OVERHEAD")
echo -e "│ ${CYAN}Single FK JOIN${NC}               │ ${SINGLE_TIME} μs$(printf '%*s' $((13 - ${#SINGLE_TIME})) '') │ ${colored_single_throughput}$(printf '%*s' $((7 - ${#SINGLE_THROUGHPUT})) '') │ ${colored_single_overhead}$(printf '%*s' $((10 - ${#SINGLE_OVERHEAD})) '')      │"

# Parse multi FK JOIN
MULTI_TIME=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Multi FK JOIN:" | grep "Time per row:" | awk '{print $4}')
MULTI_THROUGHPUT=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Multi FK JOIN:" | grep "Throughput:" | awk '{print $2}')
MULTI_OVERHEAD=$(echo "$BENCHMARK_OUTPUT" | grep -A 5 "Multi FK JOIN:" | grep "Overhead vs" | awk '{print $5}')

colored_multi_throughput=$(colorize_throughput "$MULTI_THROUGHPUT rows/sec")
colored_multi_overhead=$(colorize_overhead "$MULTI_OVERHEAD")
echo -e "│ ${CYAN}Multi FK JOIN${NC}                │ ${MULTI_TIME} μs$(printf '%*s' $((13 - ${#MULTI_TIME})) '') │ ${colored_multi_throughput}$(printf '%*s' $((7 - ${#MULTI_THROUGHPUT})) '') │ ${colored_multi_overhead}$(printf '%*s' $((10 - ${#MULTI_OVERHEAD})) '')      │"

echo "└──────────────────────────────┴──────────────────┴──────────────────┴──────────────────┘"
echo ""

# Display interpretation
echo -e "${MAGENTA}▶ OVERHEAD ANALYSIS${NC}"
INTERPRETATION=$(echo "$BENCHMARK_OUTPUT" | sed -n '/^Interpretation:/,/^$/p' | tail -n +2)
echo "$INTERPRETATION"
echo ""

# Display overhead breakdown
echo -e "${MAGENTA}▶ OVERHEAD BREAKDOWN${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"
echo "│ The measured overhead includes multiple factors:                        │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}1. Virtual method dispatch${NC} (extract_row) - ~3-6 CPU cycles              │"
echo -e "│ ${GREEN}2. Additional SQL generation${NC} (JOIN clauses) - compile-time               │"
echo -e "│ ${GREEN}3. More complex row extraction${NC} (multiple tables) - proportional to FK count │"
echo -e "│ ${GREEN}4. SQLite INNER JOIN overhead${NC} - database-level operation                 │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

# Performance characteristics legend
echo -e "${MAGENTA}▶ PERFORMANCE CHARACTERISTICS LEGEND${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"
echo "│                         Overhead Color Coding                            │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}█████${NC} < 5%      : Excellent - negligible overhead                       │"
echo -e "│ ${BLUE}█████${NC} 5-10%    : Good - acceptable overhead                            │"
echo -e "│ ${YELLOW}█████${NC} 10-20%   : Moderate - may need optimization                     │"
echo -e "│ ${RED}█████${NC} > 20%    : Significant - optimization recommended                 │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo "│                        Throughput Color Coding                           │"
echo "├──────────────────────────────────────────────────────────────────────────┤"
echo -e "│ ${GREEN}█████${NC} > 10M/sec : Excellent performance                                 │"
echo -e "│ ${BLUE}█████${NC} 5-10M/sec : Good performance                                      │"
echo -e "│ ${YELLOW}█████${NC} 1-5M/sec  : Acceptable performance                                │"
echo -e "│ ${RED}█████${NC} < 1M/sec  : Poor performance                                      │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

# Summary
echo -e "${MAGENTA}▶ SUMMARY${NC}"
echo "┌──────────────────────────────────────────────────────────────────────────┐"

# Calculate if overhead is acceptable
SINGLE_OVERHEAD_NUM=$(echo "$SINGLE_OVERHEAD" | sed 's/%$//' | sed 's/-//')
IS_NEGATIVE=$(echo "$SINGLE_OVERHEAD" | grep -q '^-' && echo "yes" || echo "no")
OVERHEAD_INT=$(echo "$SINGLE_OVERHEAD_NUM * 10" | awk '{printf "%.0f", $0}')

if [[ "$IS_NEGATIVE" == "yes" ]] || [[ $OVERHEAD_INT -lt 50 ]]; then
    echo -e "│ ${GREEN}✓ Virtual method overhead is NEGLIGIBLE${NC}                                │"
    echo "│   The IJoinStatement interface with virtual methods is an excellent     │"
    echo "│   design choice. The overhead is minimal compared to the actual work.   │"
elif [[ $OVERHEAD_INT -lt 100 ]]; then
    echo -e "│ ${BLUE}~ Virtual method overhead is ACCEPTABLE${NC}                                │"
    echo "│   The current design provides good abstraction with reasonable cost.    │"
else
    echo -e "│ ${YELLOW}⚠ Virtual method overhead is MEASURABLE${NC}                                │"
    echo "│   Consider profiling to identify specific bottlenecks.                  │"
fi

echo "│                                                                          │"
echo "│ JOIN performance relative to Storm's other operations:                  │"
echo -e "│   • Current JOIN throughput: ${colored_single_throughput}$(printf '%*s' $((32 - ${#SINGLE_THROUGHPUT})) '')  │"
echo "│   • Storm SELECT throughput: ~13M rows/sec (from CLAUDE.md)             │"
echo "│   • Raw SQLite SELECT: ~17.67M rows/sec (from CLAUDE.md)                │"
echo "└──────────────────────────────────────────────────────────────────────────┘"
echo ""

print_success "JOIN performance analysis completed!"
echo ""

# Build information
echo "╔════════════════════════════════════════════════════════════════════════════════════════╗"
echo -e "║$(printf '%*s' 41 '')${CYAN}BUILD CONFIGURATION${NC}$(printf '%*s' 41 '')║"
echo "╠════════════════════════════════════════════════════════════════════════════════════════╣"
echo "║ Compiler    : Clang with C++26 reflection support (../clang-p2996/)                    ║"
echo "║ C++ Standard: C++26 with std::meta reflection                                          ║"
echo "║ Build Type  : Debug                                                                    ║"
echo "║ Architecture: Type-erased JOIN with IJoinStatement virtual interface                   ║"
echo "╚════════════════════════════════════════════════════════════════════════════════════════╝"
echo ""

echo "To re-run this analysis:"
echo "  ./join_performance_analysis.sh"
echo ""
echo "To customize the benchmark:"
echo "  ./join_performance_analysis.sh --users=1000 --messages=10000 --iterations=20"
echo ""
echo "To run the benchmark directly:"
echo "  ./build/debug/benchmarks/join_performance_microbench --help"
