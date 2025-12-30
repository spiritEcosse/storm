#!/bin/bash

# ============================================================================
# SONAR-CHECK: Local Code Quality Analyzer (C++ & Shell) - Parallel
# ============================================================================

JOBS=$(nproc 2>/dev/null || echo 4)

# Colors
RED='\033[0;31m' GREEN='\033[0;32m' YELLOW='\033[1;33m'
BLUE='\033[0;34m' CYAN='\033[0;36m' WHITE='\033[1;37m' NC='\033[0m'

# Counters
BLOCKERS=0 CRITICALS=0 MAJORS=0 MINORS=0 INFOS=0
TOTAL_FILES=0 TOTAL_LINES=0

# ============================================================================
# Single file analyzer (called in subshell)
# ============================================================================

analyze_one() {
    local file="$1"
    local out_file="$2"

    local n=0 loc=0 is_header=false
    [[ "$file" =~ \.(h|hpp|hxx)$ ]] && is_header=true

    while IFS= read -r line || [[ -n "$line" ]]; do
        ((n++))
        [[ -n "${line//[[:space:]]/}" ]] && ((loc++))

        # === Common checks ===
        if [[ "$line" =~ (password|passwd|secret|api_key|apikey|token)[[:space:]]*[=:][[:space:]]*[\"\'][^\"\']+[\"\'] ]]; then
            echo "blocker|$file|$n|Potential hardcoded credential"
        fi

        # === Shell checks ===
        if [[ "$file" =~ \.(sh|bash)$ ]]; then
            [[ "$line" =~ [^#]*\beval\b ]] && echo "major|$file|$n|Use of eval"
            [[ "$line" =~ \`[^\`]+\` ]] && echo "minor|$file|$n|Use \$() not backticks"
        fi

        # === C++ checks ===
        if [[ "$file" =~ \.(c|cpp|cc|cxx|h|hpp|hxx|cppm)$ ]]; then
            $is_header && [[ "$line" =~ ^[[:space:]]*using[[:space:]]+namespace[[:space:]]+std ]] && \
                echo "critical|$file|$n|using namespace std in header"

            # Skip comments (// and /* */ block comments), then check for raw new
            if [[ ! "$line" =~ ^[[:space:]]*(//|\*|/\*) ]] && [[ "$line" =~ [^a-zA-Z_]new[[:space:]]+[a-zA-Z_] ]] && [[ ! "$line" =~ (make_unique|make_shared|placement|operator) ]]; then
                echo "major|$file|$n|Raw new - use smart pointers"
            fi

            [[ "$line" =~ [^/]*\bgoto\b ]] && echo "major|$file|$n|goto statement"

            [[ "$line" =~ (MD5|SHA1)[[:space:]]*\( ]] && echo "major|$file|$n|Weak hash MD5/SHA1"
        fi

    done < "$file"

    echo "META|$loc"
}

# ============================================================================
# Main scan with parallel processing
# ============================================================================

scan() {
    local dir="$1"
    local exclude="${2:-\.git|build|cmake-build|\.cache|third_party|scripts}"

    echo -e "${CYAN}▶ Scanning: ${dir}${NC}"

    mapfile -t files < <(find "$dir" -type f \( \
        -name "*.sh" -o -name "*.bash" -o \
        -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o \
        -name "*.h" -o -name "*.hpp" -o -name "*.hxx" -o -name "*.cppm" \
    \) 2>/dev/null | grep -vE "$exclude")

    local file_count=${#files[@]}
    ((file_count == 0)) && { echo "  No files found"; return; }
    TOTAL_FILES=$((TOTAL_FILES + file_count))

    local tmp_dir="/tmp/sonar-check.$$"
    mkdir -p "$tmp_dir"

    # Launch parallel jobs
    local running=0
    for i in "${!files[@]}"; do
        analyze_one "${files[$i]}" > "${tmp_dir}/${i}.out" &
        ((running++))
        if ((running >= JOBS)); then
            wait -n 2>/dev/null || wait
            ((running--))
        fi
    done
    wait

    # Aggregate results
    for f in "${tmp_dir}"/*.out; do
        [[ -f "$f" ]] || continue
        while IFS='|' read -r sev file line msg; do
            if [[ "$sev" == "META" ]]; then
                TOTAL_LINES=$((TOTAL_LINES + file))
            else
                local icon color
                case "$sev" in
                    blocker)  icon="🔴"; color="$RED"; ((BLOCKERS++)) ;;
                    critical) icon="🟠"; color="$RED"; ((CRITICALS++)) ;;
                    major)    icon="🟡"; color="$YELLOW"; ((MAJORS++)) ;;
                    minor)    icon="🔵"; color="$BLUE"; ((MINORS++)) ;;
                    *)        icon="⚪"; color="$WHITE"; ((INFOS++)) ;;
                esac
                echo -e "  ${icon} ${color}[${sev^^}]${NC} ${file}:${line} - ${msg}"
            fi
        done < "$f"
    done

    rm -rf "$tmp_dir"
}

# ============================================================================
# Main
# ============================================================================

main() {
    local exclude="\.git|build|cmake-build|\.cache|third_party|scripts"
    local -a targets=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help)
                echo "Usage: $0 [-j N] [-e pattern] <dir...>"
                echo "  -j N    Parallel jobs (default: nproc)"
                echo "  -e PAT  Exclude regex pattern"
                exit 0 ;;
            -e) exclude="$2"; shift 2 ;;
            -j) JOBS="$2"; shift 2 ;;
            --no-color) RED='' GREEN='' YELLOW='' BLUE='' CYAN='' WHITE='' NC=''; shift ;;
            -*) echo "Unknown: $1"; exit 1 ;;
            *) targets+=("$1"); shift ;;
        esac
    done

    ((${#targets[@]} == 0)) && targets=(".")

    for target in "${targets[@]}"; do
        [[ -d "$target" ]] || { echo "Not a directory: $target"; exit 1; }
    done

    echo -e "${CYAN}sonar-check${NC} (${JOBS} jobs)\n"

    for target in "${targets[@]}"; do
        scan "$target" "$exclude"
    done

    # Report
    local total=$((BLOCKERS + CRITICALS + MAJORS + MINORS + INFOS))
    echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    printf "  Files: %-6d  Lines: %-8d  Issues: %d\n" "$TOTAL_FILES" "$TOTAL_LINES" "$total"
    echo -e "  🔴 ${BLOCKERS}  🟠 ${CRITICALS}  🟡 ${MAJORS}  🔵 ${MINORS}  ⚪ ${INFOS}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

    if ((BLOCKERS > 0)); then
        echo -e "  ${RED}✗ FAILED${NC} ($BLOCKERS blockers)"
        exit 2
    elif ((CRITICALS > 0)); then
        echo -e "  ${RED}✗ FAILED${NC} ($CRITICALS criticals)"
        exit 1
    elif ((MAJORS > 0)); then
        echo -e "  ${RED}✗ FAILED${NC} ($MAJORS majors)"
        exit 1
    else
        echo -e "  ${GREEN}✓ PASSED${NC}"
        exit 0
    fi
}

main "$@"
