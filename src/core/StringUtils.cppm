module;

// Define the module
export module storm.utils;

// Import standard header units needed by implementations
import <string>;
import <algorithm>;
import <cctype>;
import <format>;
import <ranges>;
import <string_view>;

export namespace storm::utils {

    // Join type enum for compile-time join operations
    enum class JoinType { Inner, Left };

    /**
     * Compile-time string storage using NTTP (Non-Type Template Parameter)
     * This is the most reliable way to handle compile-time strings in C++23
     */
    template <std::size_t N> struct fixed_string {
        char data[N];

        // Allow default construction in consteval contexts
        consteval fixed_string() : data{} {}

        explicit consteval fixed_string(const char (&str)[N]) {
            for (std::size_t i = 0; i < N - 1; ++i) {
                data[i] = str[i];
            }
            data[N - 1] = '\0';
        }

        constexpr std::string_view view() const {
            return std::string_view{data, N - 1};
        }

        constexpr const char* c_str() const {
            return data;
        }
    };

    inline auto to_lower(std::string str) -> std::string {
        std::ranges::transform(str, str.begin(), [](unsigned char c) { return std::tolower(c); });
        return str;
    }

    std::string formatFieldName(const std::string& tableName, const std::string& fieldName) {
        return std::format(R"("{}"."{}")", tableName, fieldName);
    }

    // Compile-time string utility functions
    template <std::size_t N> consteval auto to_lower_ct(const char (&str)[N]) {
        fixed_string<N> result{};
        for (std::size_t i = 0; i < N - 1; ++i) {
            result.data[i] = (str[i] >= 'A' && str[i] <= 'Z') ? str[i] + 32 : str[i];
        }
        result.data[N - 1] = '\0';
        return result;
    }

    template <std::size_t N1, std::size_t N2>
    consteval auto formatFieldName_ct(const char (&tableName)[N1], const char (&fieldName)[N2]) {
        constexpr std::size_t    total_size = N1 + N2 + 4; // ""."" + null terminator
        fixed_string<total_size> result{};

        std::size_t pos    = 0;
        result.data[pos++] = '"';
        for (std::size_t i = 0; i < N1 - 1; ++i) {
            result.data[pos++] = tableName[i];
        }
        result.data[pos++] = '"';
        result.data[pos++] = '.';
        result.data[pos++] = '"';
        for (std::size_t i = 0; i < N2 - 1; ++i) {
            result.data[pos++] = fieldName[i];
        }
        result.data[pos++]          = '"';
        result.data[total_size - 1] = '\0';

        return result;
    }

    // Compile-time version that works with string_views (for FieldDescView)
    consteval fixed_string<512> formatFieldName_ct(std::string_view tableName, std::string_view fieldName) {
        fixed_string<512> result{};
        size_t            pos = 0;
        result.data[pos++]    = '"';
        for (char c : tableName) {
            if (pos >= 510)
                break;
            result.data[pos++] = c;
        }
        result.data[pos++] = '"';
        result.data[pos++] = '.';
        result.data[pos++] = '"';
        for (char c : fieldName) {
            if (pos >= 510)
                break;
            result.data[pos++] = c;
        }
        result.data[pos++] = '"';
        result.data[511]   = '\0'; // Ensure null termination
        return result;
    }

    // Constexpr string concatenation helper
    template <std::size_t N1, std::size_t N2> consteval auto concat_ct(const char (&str1)[N1], const char (&str2)[N2]) {
        fixed_string<N1 + N2 - 1> result{};
        std::size_t               pos = 0;
        for (std::size_t i = 0; i < N1 - 1; ++i) {
            result.data[pos++] = str1[i];
        }
        for (std::size_t i = 0; i < N2 - 1; ++i) {
            result.data[pos++] = str2[i];
        }
        result.data[N1 + N2 - 2] = '\0';
        return result;
    }

    // Constexpr join type to string
    consteval std::string_view join_type_to_string(JoinType type) {
        if (type == JoinType::Inner) {
            return "INNER JOIN";
        } else {
            return "LEFT JOIN";
        }
    }

    // Generic join helper for ranges of string-like elements
    template <std::ranges::input_range Range>
        requires requires(const std::ranges::range_reference_t<Range>& v) { std::string_view{v}; }
    inline std::string join(const Range& rng, std::string_view delim) {
        std::string out;
        bool        first = true;
        for (const auto& elem : rng) {
            if (!first)
                out += delim;
            first = false;
            out += std::string_view{elem};
        }
        return out;
    }

} // namespace storm::utils
