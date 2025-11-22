#pragma once

#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <sstream>

namespace storm::benchmark {

/**
 * CLI Utilities for interactive benchmark management
 */
class CLI {
public:
    // ANSI color codes
    static constexpr const char* RESET = "\033[0m";
    static constexpr const char* BOLD = "\033[1m";
    static constexpr const char* CYAN = "\033[36m";
    static constexpr const char* GREEN = "\033[32m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* RED = "\033[31m";
    static constexpr const char* BLUE = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";

    /**
     * Print a header box
     */
    static void print_header(const std::string& title) {
        std::cout << BOLD << CYAN;
        std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  " << title;
        // Pad to 60 chars
        for (size_t i = title.length(); i < 60; ++i) std::cout << " ";
        std::cout << "║\n";
        std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
        std::cout << RESET << "\n";
    }

    /**
     * Print a separator line
     */
    static void print_separator() {
        std::cout << "────────────────────────────────────────────────────────────────\n";
    }

    /**
     * Prompt user for string input
     */
    static std::string prompt(const std::string& label, const std::string& default_value = "") {
        std::cout << BLUE << label;
        if (!default_value.empty()) {
            std::cout << " [" << default_value << "]";
        }
        std::cout << ": " << RESET;

        std::string input;
        std::getline(std::cin, input);

        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);

        return input.empty() ? default_value : input;
    }

    /**
     * Prompt user for integer input
     */
    static int prompt_int(const std::string& label, int default_value = 0) {
        std::string input = prompt(label, std::to_string(default_value));
        try {
            return std::stoi(input);
        } catch (...) {
            return default_value;
        }
    }

    /**
     * Prompt user for boolean input (Y/n)
     */
    static bool prompt_bool(const std::string& label, bool default_value = true) {
        std::string default_str = default_value ? "Y/n" : "y/N";
        std::string input = prompt(label + " [" + default_str + "]", "");

        if (input.empty()) return default_value;

        // Convert to lowercase
        std::transform(input.begin(), input.end(), input.begin(), ::tolower);

        if (input == "y" || input == "yes") return true;
        if (input == "n" || input == "no") return false;

        return default_value;
    }

    /**
     * Prompt user to select from a list of options
     */
    static std::string prompt_choice(const std::string& label,
                                     const std::vector<std::string>& options,
                                     const std::string& default_value = "") {
        std::cout << BLUE << label << " [";
        for (size_t i = 0; i < options.size(); ++i) {
            if (i > 0) std::cout << "/";
            std::cout << options[i];
        }
        std::cout << "]";
        if (!default_value.empty()) {
            std::cout << " [" << default_value << "]";
        }
        std::cout << ": " << RESET;

        std::string input;
        std::getline(std::cin, input);

        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);

        // Convert to uppercase for comparison
        std::string input_upper = input;
        std::transform(input_upper.begin(), input_upper.end(), input_upper.begin(), ::toupper);

        // Check if input matches any option (case-insensitive)
        for (const auto& option : options) {
            std::string option_upper = option;
            std::transform(option_upper.begin(), option_upper.end(), option_upper.begin(), ::toupper);
            if (input_upper == option_upper) {
                return option;
            }
        }

        return input.empty() ? default_value : input;
    }

    /**
     * Print success message
     */
    static void success(const std::string& message) {
        std::cout << GREEN << "✓ " << message << RESET << "\n";
    }

    /**
     * Print error message
     */
    static void error(const std::string& message) {
        std::cerr << RED << "✗ " << message << RESET << "\n";
    }

    /**
     * Print warning message
     */
    static void warning(const std::string& message) {
        std::cout << YELLOW << "⚠ " << message << RESET << "\n";
    }

    /**
     * Print info message
     */
    static void info(const std::string& message) {
        std::cout << CYAN << "ℹ " << message << RESET << "\n";
    }

    /**
     * Print a formatted table row
     */
    static void print_table_row(const std::string& col1, const std::string& col2,
                                int width1 = 30, int width2 = 40) {
        std::cout << "  " << col1;
        for (size_t i = col1.length(); i < width1; ++i) std::cout << " ";
        std::cout << ": " << col2 << "\n";
    }

    /**
     * Clear the screen
     */
    static void clear_screen() {
        std::cout << "\033[2J\033[1;1H";
    }

    /**
     * Print a progress bar
     */
    static void print_progress(int current, int total, const std::string& label = "") {
        const int bar_width = 50;
        float progress = static_cast<float>(current) / total;
        int pos = static_cast<int>(bar_width * progress);

        std::cout << "\r" << label << " [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) std::cout << "█";
            else if (i == pos) std::cout << "▒";
            else std::cout << "░";
        }
        std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total << ")";
        std::cout.flush();

        if (current == total) {
            std::cout << "\n";
        }
    }

    /**
     * Get current timestamp in ISO 8601 format
     */
    static std::string get_timestamp() {
        auto now = std::time(nullptr);
        std::tm tm = *std::localtime(&now);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buffer);
    }
};

}  // namespace storm::benchmark
