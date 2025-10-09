#!/bin/bash

# Universal Table Drawing Library for Storm ORM Performance Scripts
# Provides ANSI-aware table formatting functions

# Function to get visible length of string (without ANSI escape codes)
get_visible_length() {
    local str="$1"
    # Remove ANSI escape sequences using proper escape sequence patterns
    # Handle both \033[...m and \e[...m formats for actual rendered sequences
    local clean_str
    # First handle literal \033 sequences, then actual escape sequences
    clean_str=$(printf '%s' "$str" | sed -r 's/\\033\[[0-9;]*m//g' | sed -r 's/\x1b\[[0-9;]*m//g')
    echo "${#clean_str}"
}

# Function to pad string to specific width, accounting for ANSI escape codes
pad_string() {
    local string="$1"
    local width="$2"
    local align="${3:-left}"  # Default to left alignment
    local visible_length=$(get_visible_length "$string")
    local padding_needed=$((width - visible_length))

    if [[ $padding_needed -gt 0 ]]; then
        if [[ "$align" == "right" ]]; then
            printf "%*s%s" "$padding_needed" "" "$string"
        elif [[ "$align" == "center" ]]; then
            local left_pad=$((padding_needed / 2))
            local right_pad=$((padding_needed - left_pad))
            printf "%*s%s%*s" "$left_pad" "" "$string" "$right_pad" ""
        else
            printf "%s%*s" "$string" "$padding_needed" ""
        fi
    else
        printf "%s" "$string"
    fi
}

# Function to draw table border (top)
draw_table_top() {
    local -a widths=("$@")
    printf "┌"
    for i in "${!widths[@]}"; do
        printf "%0.s─" $(seq 1 ${widths[i]})
        if [[ $i -lt $((${#widths[@]} - 1)) ]]; then
            printf "┬"
        fi
    done
    printf "┐\n"
}

# Function to draw table border (middle)
draw_table_middle() {
    local -a widths=("$@")
    printf "├"
    for i in "${!widths[@]}"; do
        printf "%0.s─" $(seq 1 ${widths[i]})
        if [[ $i -lt $((${#widths[@]} - 1)) ]]; then
            printf "┼"
        fi
    done
    printf "┤\n"
}

# Function to draw table border (bottom)
draw_table_bottom() {
    local -a widths=("$@")
    printf "└"
    for i in "${!widths[@]}"; do
        printf "%0.s─" $(seq 1 ${widths[i]})
        if [[ $i -lt $((${#widths[@]} - 1)) ]]; then
            printf "┴"
        fi
    done
    printf "┘\n"
}

# Function to draw table row with proper padding
# Usage: draw_table_row widths[@] values[@]
draw_table_row() {
    local widths_ref=$1[@]
    local values_ref=$2[@]
    local widths=("${!widths_ref}")
    local values=("${!values_ref}")

    echo -en "│"
    for i in "${!widths[@]}"; do
        local padded=$(pad_string "${values[i]}" $((${widths[i]} - 2)))
        echo -en " ${padded} │"
    done
    echo ""
}

# Example usage:
# source lib/table_utils.sh
#
# # Define column widths
# widths=(33 14 14 14)
#
# # Draw table
# draw_table_top "${widths[@]}"
# draw_table_row widths values_header
# draw_table_middle "${widths[@]}"
# draw_table_row widths values_row1
# draw_table_row widths values_row2
# draw_table_bottom "${widths[@]}"
