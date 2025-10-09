#!/bin/bash

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Change to project root (two levels up from scripts/bench/)
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Source table drawing library
source "${PROJECT_ROOT}/scripts/table_utils.sh"

echo "=== STORM ORM PERFORMANCE BENCHMARK SUITE (RELEASE MODE) ==="
echo "Building and running comprehensive CRUD operation performance tests"
echo "Using optimized Release build for accurate production performance measurements"
echo ""

# Colors for output formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

# Function to format numbers in human-readable format
format_number() {
    local num=$1
    local operation_type="${2:-inserts}"  # Default to "inserts", can be "deletes"

    # Handle empty or non-numeric input (including decimal numbers)
    if [[ -z "$num" ]] || ! [[ "$num" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        echo "$num"
        return
    fi

    # Convert to integer for comparison (handle both int and float)
    local int_num=${num%.*}

    if [[ $int_num -ge 1000000000 ]]; then
        # For billions, use 2 decimal places
        local billions=$((int_num / 10000000))  # Get first 4 digits
        local decimal=$((billions % 100))      # Last 2 digits for decimal
        local whole=$((billions / 100))        # Whole part
        printf "%d.%02dB %s/sec" "$whole" "$decimal" "$operation_type"
    elif [[ $int_num -ge 1000000 ]]; then
        # For millions, use 2 decimal places - use arithmetic for locale independence
        local millions=$((int_num / 10000))  # Get first 4 digits
        local decimal=$((millions % 100))    # Get last 2 digits for decimal
        local whole=$((millions / 100))      # Get whole part
        printf "%d.%02dM %s/sec" "$whole" "$decimal" "$operation_type"
    elif [[ $int_num -ge 1000 ]]; then
        # For thousands - use 1 decimal place for better precision
        local thousands=$((int_num / 100))   # Get first 3-4 digits
        local decimal=$((thousands % 10))    # Last digit for decimal
        local whole=$((thousands / 10))      # Whole part
        if [[ $whole -ge 1000 ]]; then
            # If rounding pushes us to 1000K, convert to millions
            printf "1.00M %s/sec" "$operation_type"
        else
            printf "%d.%01dK %s/sec" "$whole" "$decimal" "$operation_type"
        fi
    else
        # Less than 1000, show as-is
        printf "%d %s/sec" "$int_num" "$operation_type"
    fi
}

# Function to format numbers with color-coded performance indicators
format_number_with_color() {
    local num=$1
    local operation_type="${2:-inserts}"  # Default to "inserts", can be "deletes"
    local formatted=$(format_number "$num" "$operation_type")

    # Color code based on performance tiers
    if [[ $num -ge 10000000 ]]; then      # 10M+ = Excellent (Green)
        echo -e "${GREEN}$formatted${NC}"
    elif [[ $num -ge 1000000 ]]; then     # 1M+ = Good (Blue)
        echo -e "${BLUE}$formatted${NC}"
    elif [[ $num -ge 500000 ]]; then      # 500K+ = Acceptable (Yellow)
        echo -e "${YELLOW}$formatted${NC}"
    else                                  # <500K = Poor (Red)
        echo -e "${RED}$formatted${NC}"
    fi
}

# Function to calculate percentage relative to baseline
calculate_percentage() {
    local value=$1
    local baseline=$2

    if [[ -n "$value" && -n "$baseline" && "$baseline" != "0" ]]; then
        # Use bc for floating-point arithmetic, then round to nearest integer
        echo "scale=2; ($value * 100) / $baseline" | bc | awk '{printf "%.0f", $1}'
    else
        echo "0"
    fi
}

# Function to calculate overall performance percentage across all metrics
calculate_overall_percentage() {
    local single_insert=$1
    local batch_insert=$2
    local single_delete=$3
    local bulk_delete=$4
    local single_update=$5
    local batch_update=$6
    local select=$7
    local baseline_single_insert=$8
    local baseline_batch_insert=$9
    local baseline_single_delete=${10}
    local baseline_bulk_delete=${11}
    local baseline_single_update=${12}
    local baseline_batch_update=${13}
    local baseline_select=${14}

    local count=0
    local total_percentage=0

    # Only include metrics that are available for both benchmark and baseline
    if [[ -n "$single_insert" && "$single_insert" =~ ^[0-9]+$ && -n "$baseline_single_insert" && "$baseline_single_insert" != "0" ]]; then
        local pct=$(calculate_percentage "$single_insert" "$baseline_single_insert")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$batch_insert" && "$batch_insert" =~ ^[0-9]+$ && -n "$baseline_batch_insert" && "$baseline_batch_insert" != "0" ]]; then
        local pct=$(calculate_percentage "$batch_insert" "$baseline_batch_insert")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$single_delete" && "$single_delete" =~ ^[0-9]+$ && -n "$baseline_single_delete" && "$baseline_single_delete" != "0" ]]; then
        local pct=$(calculate_percentage "$single_delete" "$baseline_single_delete")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$bulk_delete" && "$bulk_delete" =~ ^[0-9]+$ && -n "$baseline_bulk_delete" && "$baseline_bulk_delete" != "0" ]]; then
        local pct=$(calculate_percentage "$bulk_delete" "$baseline_bulk_delete")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$single_update" && "$single_update" =~ ^[0-9]+$ && -n "$baseline_single_update" && "$baseline_single_update" != "0" ]]; then
        local pct=$(calculate_percentage "$single_update" "$baseline_single_update")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$batch_update" && "$batch_update" =~ ^[0-9]+$ && -n "$baseline_batch_update" && "$baseline_batch_update" != "0" ]]; then
        local pct=$(calculate_percentage "$batch_update" "$baseline_batch_update")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    if [[ -n "$select" && "$select" =~ ^[0-9]+$ && -n "$baseline_select" && "$baseline_select" != "0" ]]; then
        local pct=$(calculate_percentage "$select" "$baseline_select")
        total_percentage=$((total_percentage + pct))
        count=$((count + 1))
    fi

    # Calculate average percentage
    if [[ $count -gt 0 ]]; then
        echo $((total_percentage / count))
    else
        echo "0"
    fi
}

# Function to run benchmark and format throughput output in real-time
run_and_format_benchmark() {
    local executable="$1"
    local benchmark_name="$2"
    local use_color="${3:-false}"

    if [[ -x "$executable" ]]; then
        echo -e "${BLUE}$benchmark_name:${NC}"
        "$executable" | while IFS= read -r line; do
            if [[ "$line" =~ ^[[:space:]]*Throughput:[[:space:]]*([0-9]+)[[:space:]]*inserts/sec ]]; then
                local raw_number="${BASH_REMATCH[1]}"
                if [[ "$use_color" == "true" ]]; then
                    local formatted="$(format_number_with_color "$raw_number")"
                else
                    local formatted="$(format_number "$raw_number")"
                fi
                echo "${line/Throughput: $raw_number inserts\/sec/Throughput: $formatted}"
            else
                echo "$line"
            fi
        done
        echo ""
    else
        print_error "$executable not available"
    fi
}

# Step 1: Configure CMake with benchmarking enabled (Release mode)
print_step "Step 1: Configuring Release build with benchmarking enabled..."
if cmake --preset ninja-release -DENABLE_TESTS=ON -DENABLE_BENCH=ON; then
    print_success "CMake Release configuration completed"
else
    print_error "CMake Release configuration failed"
    exit 1
fi

echo ""

# Step 2: Build benchmark infrastructure (Release mode)
print_step "Step 2: Building optimized benchmark executables..."

# Build SQLite benchmarks (these usually work without issues)
print_step "Building SQLite and sqlite_orm benchmarks (Release)..."
if cmake --build --preset ninja-release --target bench_sqlite --target bench_sqlite_orm; then
    print_success "SQLite benchmarks built successfully (optimized)"
else
    print_warning "Some SQLite benchmarks may have failed to build"
fi

# Build Storm benchmarks (may need individual building due to clang-scan-deps issues)
print_step "Building Storm ORM benchmarks (Release)..."
if cmake --build --preset ninja-release --target bench_storm; then
    print_success "Storm ORM benchmarks built successfully (optimized)"
else
    print_warning "Building Storm benchmarks individually due to potential clang-scan-deps issues..."

    # Try building Storm library first
    cmake --build --preset ninja-release --target storm || true

    # Clean temporary files that might cause issues
    rm -f build/release/benchmarks/CMakeFiles/bench_storm*/bench_storm*.cpp.o.ddi.tmp 2>/dev/null || true

    # Build Storm benchmark
    if cmake --build --preset ninja-release --target bench_storm; then
        print_success "bench_storm built successfully (optimized)"
    else
        print_error "Failed to build bench_storm"
    fi
fi

echo ""

# Step 3: Verify all executables exist
print_step "Step 3: Verifying Release benchmark executables..."
EXECUTABLES=("bench_sqlite" "bench_sqlite_orm" "bench_storm")
MISSING_EXECUTABLES=()

for exe in "${EXECUTABLES[@]}"; do
    if [[ -x "build/release/benchmarks/$exe" ]]; then
        print_success "$exe: Ready (optimized)"
    else
        print_error "$exe: Missing or not executable"
        MISSING_EXECUTABLES+=("$exe")
    fi
done

if [[ ${#MISSING_EXECUTABLES[@]} -gt 0 ]]; then
    print_warning "Some executables are missing, performance comparison may be incomplete"
fi

echo ""

# Step 4: Run benchmarks and collect results
print_step "Step 4: Running performance benchmarks..."
echo ""

echo "=== STORM ORM PERFORMANCE COMPARISON RESULTS ==="
echo "Testing database operations with different approaches:"
echo ""

# Extract and display performance metrics from all benchmarks
echo -e "${YELLOW}Extracting performance metrics for 10,000 operations...${NC}"
echo ""

# Add debug mode (uncomment the following line to enable debug output)
# DEBUG_MODE=1

# Store benchmark results in arrays for sorting
declare -a benchmark_names
declare -a performance_percentages
declare -a single_inserts
declare -a batch_inserts
declare -a single_deletes
declare -a bulk_deletes
declare -a single_updates
declare -a batch_updates
declare -a selects

# Store baseline (sqlite_orm) performance for percentage calculation - all 7 metrics
BASELINE_SINGLE_INSERT=""
BASELINE_BATCH_INSERT=""
BASELINE_SINGLE_DELETE=""
BASELINE_BULK_DELETE=""
BASELINE_SINGLE_UPDATE=""
BASELINE_BATCH_UPDATE=""
BASELINE_SELECT=""

# Raw SQLite metrics
if [[ -x "build/release/benchmarks/bench_sqlite" ]]; then
    SQLITE_OUTPUT=$(./build/release/benchmarks/bench_sqlite)

    SQLITE_SINGLE=$(echo "$SQLITE_OUTPUT" | grep -A4 "Raw SQLite (prepared statements) - Single INSERT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    SQLITE_BATCH=$(echo "$SQLITE_OUTPUT" | grep -A7 "Raw SQLite - Batch INSERT 10000 records (batch size 1000)" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    SQLITE_DELETE_SINGLE=$(echo "$SQLITE_OUTPUT" | grep -A4 "Raw SQLite (prepared statements) - Single DELETE 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    SQLITE_DELETE_BULK=$(echo "$SQLITE_OUTPUT" | grep -A7 "Raw SQLite - Batch DELETE 10000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    SQLITE_UPDATE_SINGLE=$(echo "$SQLITE_OUTPUT" | grep -A4 "Raw SQLite (prepared statements) - Single UPDATE 1000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | head -1)
    SQLITE_UPDATE_BATCH=$(echo "$SQLITE_OUTPUT" | grep -A7 "Raw SQLite - Batch UPDATE 1000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | sort -n | tail -1)
    SQLITE_SELECT=$(echo "$SQLITE_OUTPUT" | grep -A4 "Raw SQLite - SELECT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ rows\/sec//' | head -1)

    # Raw SQLite is not used as baseline due to timing precision issues

    # Debug output
    if [[ -n "$DEBUG_MODE" ]]; then
        echo "DEBUG Raw SQLite - Single: '$SQLITE_SINGLE', Batch: '$SQLITE_BATCH', Delete Single: '$SQLITE_DELETE_SINGLE', Delete Bulk: '$SQLITE_DELETE_BULK', Update Single: '$SQLITE_UPDATE_SINGLE', Update Batch: '$SQLITE_UPDATE_BATCH', Select: '$SQLITE_SELECT'" >&2
    fi

    # Raw SQLite percentage calculated later after sqlite_orm baseline is set
    SQLITE_PERCENTAGE=""

    # Store Raw SQLite results
    benchmark_names+=("Raw SQLite (prepared)")
    performance_percentages+=("$SQLITE_PERCENTAGE")
    single_inserts+=("$SQLITE_SINGLE")
    batch_inserts+=("$SQLITE_BATCH")
    single_deletes+=("$SQLITE_DELETE_SINGLE")
    bulk_deletes+=("$SQLITE_DELETE_BULK")
    single_updates+=("$SQLITE_UPDATE_SINGLE")
    batch_updates+=("$SQLITE_UPDATE_BATCH")
    selects+=("$SQLITE_SELECT")
else
    benchmark_names+=("Raw SQLite (prepared)")
    performance_percentages+=("0")
    single_inserts+=("")
    batch_inserts+=("")
    single_deletes+=("")
    bulk_deletes+=("")
    single_updates+=("")
    batch_updates+=("")
    selects+=("")
fi

# sqlite_orm metrics
if [[ -x "build/release/benchmarks/bench_sqlite_orm" ]]; then
    SQLITEORM_OUTPUT=$(./build/release/benchmarks/bench_sqlite_orm)

    SQLITEORM_SINGLE=$(echo "$SQLITEORM_OUTPUT" | grep -A4 "sqlite_orm - Single INSERT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    SQLITEORM_BATCH=$(echo "$SQLITEORM_OUTPUT" | grep -A7 "sqlite_orm - Batch INSERT 10000 records (batch size 100)" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    SQLITEORM_DELETE_SINGLE=$(echo "$SQLITEORM_OUTPUT" | grep -A4 "sqlite_orm - Single DELETE 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    SQLITEORM_DELETE_BULK=$(echo "$SQLITEORM_OUTPUT" | grep -A7 "sqlite_orm - Batch DELETE 10000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    SQLITEORM_UPDATE_SINGLE=$(echo "$SQLITEORM_OUTPUT" | grep -A4 "sqlite_orm - Single UPDATE 1000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | head -1)
    SQLITEORM_UPDATE_BATCH=$(echo "$SQLITEORM_OUTPUT" | grep -A7 "sqlite_orm - Batch UPDATE 1000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | sort -n | tail -1)
    SQLITEORM_SELECT=$(echo "$SQLITEORM_OUTPUT" | grep -A4 "sqlite_orm - SELECT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ rows\/sec//' | head -1)

    # Set sqlite_orm as baseline for ORM comparison - all 7 metrics
    if [[ -n "$SQLITEORM_SINGLE" && "$SQLITEORM_SINGLE" =~ ^[0-9]+$ ]]; then
        BASELINE_SINGLE_INSERT="$SQLITEORM_SINGLE"
    fi
    if [[ -n "$SQLITEORM_BATCH" && "$SQLITEORM_BATCH" =~ ^[0-9]+$ ]]; then
        BASELINE_BATCH_INSERT="$SQLITEORM_BATCH"
    fi
    if [[ -n "$SQLITEORM_DELETE_SINGLE" && "$SQLITEORM_DELETE_SINGLE" =~ ^[0-9]+$ ]]; then
        BASELINE_SINGLE_DELETE="$SQLITEORM_DELETE_SINGLE"
    fi
    if [[ -n "$SQLITEORM_DELETE_BULK" && "$SQLITEORM_DELETE_BULK" =~ ^[0-9]+$ ]]; then
        BASELINE_BULK_DELETE="$SQLITEORM_DELETE_BULK"
    fi
    if [[ -n "$SQLITEORM_UPDATE_SINGLE" && "$SQLITEORM_UPDATE_SINGLE" =~ ^[0-9]+$ ]]; then
        BASELINE_SINGLE_UPDATE="$SQLITEORM_UPDATE_SINGLE"
    fi
    if [[ -n "$SQLITEORM_UPDATE_BATCH" && "$SQLITEORM_UPDATE_BATCH" =~ ^[0-9]+$ ]]; then
        BASELINE_BATCH_UPDATE="$SQLITEORM_UPDATE_BATCH"
    fi
    if [[ -n "$SQLITEORM_SELECT" && "$SQLITEORM_SELECT" =~ ^[0-9]+$ ]]; then
        BASELINE_SELECT="$SQLITEORM_SELECT"
    fi

    # sqlite_orm is the baseline (100%)
    SQLITEORM_PERCENTAGE="100"

    # Debug output
    if [[ -n "$DEBUG_MODE" ]]; then
        echo "DEBUG sqlite_orm - Single: '$SQLITEORM_SINGLE', Batch: '$SQLITEORM_BATCH', Delete Single: '$SQLITEORM_DELETE_SINGLE', Delete Bulk: '$SQLITEORM_DELETE_BULK', Update Single: '$SQLITEORM_UPDATE_SINGLE', Update Batch: '$SQLITEORM_UPDATE_BATCH', Select: '$SQLITEORM_SELECT'" >&2
    fi

    # Store sqlite_orm results
    benchmark_names+=("sqlite_orm (v1.9.1)")
    performance_percentages+=("$SQLITEORM_PERCENTAGE")
    single_inserts+=("$SQLITEORM_SINGLE")
    batch_inserts+=("$SQLITEORM_BATCH")
    single_deletes+=("$SQLITEORM_DELETE_SINGLE")
    bulk_deletes+=("$SQLITEORM_DELETE_BULK")
    single_updates+=("$SQLITEORM_UPDATE_SINGLE")
    batch_updates+=("$SQLITEORM_UPDATE_BATCH")
    selects+=("$SQLITEORM_SELECT")
else
    benchmark_names+=("sqlite_orm (v1.9.1)")
    performance_percentages+=("0")
    single_inserts+=("")
    batch_inserts+=("")
    single_deletes+=("")
    bulk_deletes+=("")
    single_updates+=("")
    batch_updates+=("")
    selects+=("")
fi

# Storm ORM metrics
if [[ -x "build/release/benchmarks/bench_storm" ]]; then
    STORM_OUTPUT=$(./build/release/benchmarks/bench_storm)

    STORM_SINGLE=$(echo "$STORM_OUTPUT" | grep -A4 "Storm ORM - Single INSERT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    STORM_BATCH=$(echo "$STORM_OUTPUT" | grep -A7 "Storm ORM - Batch INSERT 10000 records (batch size 1000)" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ inserts\/sec//' | head -1)
    STORM_DELETE_SINGLE=$(echo "$STORM_OUTPUT" | grep -A4 "Storm ORM - Single DELETE 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    STORM_DELETE_BULK=$(echo "$STORM_OUTPUT" | grep -A7 "Storm ORM - Batch DELETE 10000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ deletes\/sec//' | head -1)
    STORM_UPDATE_SINGLE=$(echo "$STORM_OUTPUT" | grep -A4 "Storm ORM - Single UPDATE 1000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | head -1)
    STORM_UPDATE_BATCH=$(echo "$STORM_OUTPUT" | grep -A7 "Storm ORM - Batch UPDATE 1000 records" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ updates\/sec//' | sort -n | tail -1)
    STORM_SELECT=$(echo "$STORM_OUTPUT" | grep -A4 "Storm ORM - SELECT 10000 records:" | grep "Throughput:" | sed 's/.*Throughput: //' | sed 's/ rows\/sec//' | head -1)

    # Calculate overall performance percentage across all 7 metrics
    STORM_PERCENTAGE=$(calculate_overall_percentage \
        "$STORM_SINGLE" "$STORM_BATCH" "$STORM_DELETE_SINGLE" "$STORM_DELETE_BULK" "$STORM_UPDATE_SINGLE" "$STORM_UPDATE_BATCH" "$STORM_SELECT" \
        "$BASELINE_SINGLE_INSERT" "$BASELINE_BATCH_INSERT" "$BASELINE_SINGLE_DELETE" "$BASELINE_BULK_DELETE" "$BASELINE_SINGLE_UPDATE" "$BASELINE_BATCH_UPDATE" "$BASELINE_SELECT")

    # Debug output
    if [[ -n "$DEBUG_MODE" ]]; then
        echo "DEBUG Storm ORM - Single: '$STORM_SINGLE', Batch: '$STORM_BATCH', Delete Single: '$STORM_DELETE_SINGLE', Delete Bulk: '$STORM_DELETE_BULK', Update Single: '$STORM_UPDATE_SINGLE', Update Batch: '$STORM_UPDATE_BATCH', Select: '$STORM_SELECT'" >&2
    fi

    # Store Storm ORM results
    benchmark_names+=("Storm ORM (Standard)")
    performance_percentages+=("$STORM_PERCENTAGE")
    single_inserts+=("$STORM_SINGLE")
    batch_inserts+=("$STORM_BATCH")
    single_deletes+=("$STORM_DELETE_SINGLE")
    bulk_deletes+=("$STORM_DELETE_BULK")
    single_updates+=("$STORM_UPDATE_SINGLE")
    batch_updates+=("$STORM_UPDATE_BATCH")
    selects+=("$STORM_SELECT")
else
    benchmark_names+=("Storm ORM (Standard)")
    performance_percentages+=("0")
    single_inserts+=("")
    batch_inserts+=("")
    single_deletes+=("")
    bulk_deletes+=("")
    single_updates+=("")
    batch_updates+=("")
    selects+=("")
fi


# Now that baseline is set, calculate Raw SQLite overall percentage across all 7 metrics
if [[ -n "$BASELINE_SINGLE_INSERT" ]]; then
    SQLITE_PERCENTAGE=$(calculate_overall_percentage \
        "$SQLITE_SINGLE" "$SQLITE_BATCH" "$SQLITE_DELETE_SINGLE" "$SQLITE_DELETE_BULK" "$SQLITE_UPDATE_SINGLE" "$SQLITE_UPDATE_BATCH" "$SQLITE_SELECT" \
        "$BASELINE_SINGLE_INSERT" "$BASELINE_BATCH_INSERT" "$BASELINE_SINGLE_DELETE" "$BASELINE_BULK_DELETE" "$BASELINE_SINGLE_UPDATE" "$BASELINE_BATCH_UPDATE" "$BASELINE_SELECT")
    # Update the Raw SQLite entry (index 0)
    performance_percentages[0]="$SQLITE_PERCENTAGE"
fi

# Sort results by performance percentage (highest to lowest)
# Create array of indices sorted by performance percentage
declare -a sorted_indices
for i in "${!performance_percentages[@]}"; do
    sorted_indices+=("$i")
done

# Simple bubble sort by performance percentage (descending)
for (( i = 0; i < ${#sorted_indices[@]} - 1; i++ )); do
    for (( j = 0; j < ${#sorted_indices[@]} - i - 1; j++ )); do
        idx1=${sorted_indices[j]}
        idx2=${sorted_indices[j+1]}
        if [[ ${performance_percentages[idx1]} -lt ${performance_percentages[idx2]} ]]; then
            # Swap indices
            temp=${sorted_indices[j]}
            sorted_indices[j]=${sorted_indices[j+1]}
            sorted_indices[j+1]=$temp
        fi
    done
done

# Create and display the sorted table
echo "┌─────────────────────────────────────┬──────────────────┬────────────────────┬─────────────────────┬────────────────────┬─────────────────────┬────────────────────┬─────────────────────┬─────────────────────┐"
echo "│ Benchmark                           │ Overall Perf %   │ Single INSERT      │ Best Batch INSERT   │ Single DELETE      │ Bulk DELETE         │ Single UPDATE      │ Best Batch UPDATE   │ SELECT              │"
echo "├─────────────────────────────────────┼──────────────────┼────────────────────┼─────────────────────┼────────────────────┼─────────────────────┼────────────────────┼─────────────────────┼─────────────────────┤"

# Display sorted results
for idx in "${sorted_indices[@]}"; do
    name="${benchmark_names[idx]}"
    percentage="${performance_percentages[idx]}"
    single_insert="${single_inserts[idx]}"
    batch_insert="${batch_inserts[idx]}"
    single_delete="${single_deletes[idx]}"
    bulk_delete="${bulk_deletes[idx]}"
    single_update="${single_updates[idx]}"
    batch_update="${batch_updates[idx]}"
    select="${selects[idx]}"

    # Format values for display
    if [[ -n "$percentage" && "$percentage" != "0" ]]; then
        if [[ "$percentage" == "100" && "$name" == "sqlite_orm (v1.9.1)" ]]; then
            percentage_display="${GREEN}100% (baseline)${NC}"
        else
            percentage_display="${BLUE}${percentage}%${NC}"
        fi
    else
        percentage_display="N/A"
    fi

    if [[ -n "$single_insert" && "$single_insert" =~ ^[0-9]+$ ]]; then
        single_insert_display="$(format_number_with_color "$single_insert" "inserts")"
    else
        single_insert_display="Not available"
    fi

    if [[ -n "$batch_insert" && "$batch_insert" =~ ^[0-9]+$ ]]; then
        batch_insert_display="$(format_number_with_color "$batch_insert" "inserts")"
    else
        batch_insert_display="Not available"
    fi

    if [[ -n "$single_delete" && "$single_delete" =~ ^[0-9]+$ ]]; then
        single_delete_display="$(format_number_with_color "$single_delete" "deletes")"
    else
        single_delete_display="Not available"
    fi

    if [[ -n "$bulk_delete" && "$bulk_delete" =~ ^[0-9]+$ ]]; then
        bulk_delete_display="$(format_number_with_color "$bulk_delete" "deletes")"
    else
        bulk_delete_display="Not available"
    fi

    if [[ -n "$single_update" && "$single_update" =~ ^[0-9]+$ ]]; then
        single_update_display="$(format_number_with_color "$single_update" "updates")"
    else
        single_update_display="Not available"
    fi

    if [[ -n "$batch_update" && "$batch_update" =~ ^[0-9]+$ ]]; then
        batch_update_display="$(format_number_with_color "$batch_update" "updates")"
    else
        batch_update_display="Not available"
    fi

    if [[ -n "$select" && "$select" =~ ^[0-9]+$ ]]; then
        select_display="$(format_number_with_color "$select" "rows")"
    else
        select_display="Not available"
    fi

    # Format table row with proper ANSI code handling
    # Use pad_string for all columns to handle ANSI escape codes consistently
    name_padded=$(pad_string "$name" 35)
    percentage_padded=$(pad_string "$percentage_display" 16)
    single_insert_padded=$(pad_string "$single_insert_display" 18)
    batch_insert_padded=$(pad_string "$batch_insert_display" 19)
    single_delete_padded=$(pad_string "$single_delete_display" 18)
    bulk_delete_padded=$(pad_string "$bulk_delete_display" 19)
    single_update_padded=$(pad_string "$single_update_display" 18)
    batch_update_padded=$(pad_string "$batch_update_display" 19)
    select_padded=$(pad_string "$select_display" 19)

    echo -e "│ ${name_padded} │ ${percentage_padded} │ ${single_insert_padded} │ ${batch_insert_padded} │ ${single_delete_padded} │ ${bulk_delete_padded} │ ${single_update_padded} │ ${batch_update_padded} │ ${select_padded} │"
done

echo "└─────────────────────────────────────┴──────────────────┴────────────────────┴─────────────────────┴────────────────────┴─────────────────────┴────────────────────┴─────────────────────┴─────────────────────┘"
echo ""

print_success "Comprehensive performance benchmark suite completed successfully!"

# Step 8: Build time information
echo ""
echo "=== BUILD INFORMATION ==="
echo "Compiler: $(${CMAKE_CXX_COMPILER:-/home/ihor/projects/storm/clang-p2996/build/bin/clang++} --version | head -1)"
echo "C++ Standard: C++26 with reflection support"
echo "Build Type: Release (Optimized)"
echo "CMake Preset: ninja-release"
echo "Enabled Features: Tests, Benchmarks"
echo "Optimization Level: -O2 or higher"
echo ""
echo "NOTE: These are production-grade performance measurements."
echo "      For development/debugging, use performance_comparison.sh (Debug build)"
echo ""

echo "To re-run individual benchmarks (without rebuilding):"
echo "  ./build/release/benchmarks/bench_sqlite          # Raw SQLite baseline"
echo "  ./build/release/benchmarks/bench_sqlite_orm      # sqlite_orm comparison"
echo "  ./build/release/benchmarks/bench_storm           # Storm ORM standard"
echo ""
echo "For detailed optimization analysis:"
echo "  ./build/release/benchmarks/bench_storm --mode=cache-analysis      # SQL cache performance testing"
echo "  ./build/release/benchmarks/bench_storm --mode=optimization-test   # Comprehensive optimization testing"