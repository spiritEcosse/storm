#!/bin/bash

# Storm ORM Performance Benchmark Suite
# Unified entry point for all benchmark scripts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Print header
print_header() {
    echo -e "${BOLD}${CYAN}"
    echo "╔════════════════════════════════════════════════════════════════════════════════╗"
    echo "║                                                                                ║"
    echo "║                   Storm ORM Performance Benchmark Suite                        ║"
    echo "║                                                                                ║"
    echo "╚════════════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# Print help
print_help() {
    print_header
    echo -e "${BOLD}Usage:${NC}"
    echo -e "  ./main.sh [COMMAND] [OPTIONS]"
    echo ""
    echo -e "${BOLD}Commands:${NC}"
    echo -e "  ${CYAN}--joins${NC}              Run JOIN performance analysis (Storm vs Raw SQLite)"
    echo -e "  ${CYAN}--crud${NC}               Run comprehensive CRUD benchmark (INSERT/SELECT/UPDATE/DELETE)"
    echo -e "  ${CYAN}--sql-gen${NC}            Run SQL generation performance analysis"
    echo -e "  ${CYAN}--all${NC}                Run all microbenchmarks"
    echo -e "  ${CYAN}--help, -h${NC}           Show this help message"
    echo ""
    echo -e "${BOLD}Options:${NC}"
    echo -e "  ${CYAN}--size=N${NC}             Set dataset size (default varies by benchmark)"
    echo -e "  ${CYAN}--iterations=N${NC}       Set number of iterations (default varies by benchmark)"
    echo ""
    echo -e "${BOLD}Examples:${NC}"
    echo -e "  ${GRAY}# Run JOIN benchmarks with 10K messages${NC}"
    echo -e "  ./main.sh --joins --size=10000"
    echo ""
    echo -e "  ${GRAY}# Run comprehensive CRUD benchmarks${NC}"
    echo -e "  ./main.sh --crud"
    echo ""
    echo -e "  ${GRAY}# Run all benchmarks${NC}"
    echo -e "  ./main.sh --all"
    echo ""
    echo -e "${BOLD}Available Benchmarks:${NC}"
    echo -e "  📊 ${GREEN}JOIN Performance${NC}       - Compare Storm ORM vs Raw SQLite JOIN operations"
    echo -e "  📊 ${GREEN}CRUD Operations${NC}        - Full suite of INSERT/SELECT/UPDATE/DELETE benchmarks"
    echo -e "  📊 ${GREEN}SQL Generation${NC}         - Analyze compile-time SQL generation performance"
    echo -e "  📊 ${GREEN}All Microbenchmarks${NC}    - Run complete benchmark suite"
    echo ""
}

# Parse arguments
COMMAND=""
SIZE=""
ITERATIONS=""

for arg in "$@"; do
    case $arg in
        --joins)
            COMMAND="joins"
            ;;
        --crud)
            COMMAND="crud"
            ;;
        --sql-gen)
            COMMAND="sql-gen"
            ;;
        --all)
            COMMAND="all"
            ;;
        --help|-h)
            print_help
            exit 0
            ;;
        --size=*)
            SIZE="${arg#*=}"
            ;;
        --iterations=*)
            ITERATIONS="${arg#*=}"
            ;;
        *)
            echo -e "${RED}Error: Unknown argument '$arg'${NC}"
            echo ""
            print_help
            exit 1
            ;;
    esac
done

# Execute command
case $COMMAND in
    joins)
        print_header
        echo -e "${GREEN}Running JOIN Performance Analysis...${NC}"
        echo ""
        ARGS=""
        [[ -n "$SIZE" ]] && ARGS="$SIZE"
        [[ -n "$ITERATIONS" ]] && ARGS="$ARGS $ITERATIONS"
        exec scripts/bench/joins.sh $ARGS
        ;;

    crud)
        print_header
        echo -e "${GREEN}Running CRUD Performance Benchmark...${NC}"
        echo ""
        exec scripts/bench/crud.sh
        ;;

    sql-gen)
        print_header
        echo -e "${GREEN}Running SQL Generation Analysis...${NC}"
        echo ""
        exec scripts/bench/sql-gen.sh
        ;;

    all)
        print_header
        echo -e "${GREEN}Running All Microbenchmarks...${NC}"
        echo ""
        exec scripts/bench/all.sh
        ;;

    "")
        echo -e "${RED}Error: No command specified${NC}"
        echo ""
        print_help
        exit 1
        ;;

    *)
        echo -e "${RED}Error: Unknown command '$COMMAND'${NC}"
        echo ""
        print_help
        exit 1
        ;;
esac
