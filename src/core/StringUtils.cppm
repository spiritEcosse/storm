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

    /**
     * Compile-time string storage using NTTP (Non-Type Template Parameter)
     * This is the most reliable way to handle compile-time strings in C++23
     */
    template <std::size_t N> struct fixed_string {
        static constexpr std::size_t capacity = N;
        char                         data[N]  = {};

        // Default constructor
        consteval fixed_string() = default;

        // C-string constructor
        consteval fixed_string(const char (&str)[N]) {
            for (std::size_t i = 0; i < N - 1; ++i) {
                data[i] = str[i];
            }
            data[N - 1] = '\0';
        }

        // Constructor from string_view (with size check)
        consteval explicit fixed_string(std::string_view sv)
            requires(N > 0)
        {
            if (sv.size() >= N) {
                throw "String too long for fixed_string";
            }
            for (std::size_t i = 0; i < sv.size(); ++i) {
                data[i] = sv.data()[i];
            }
            data[sv.size()] = '\0';
        }

        // Size without null terminator
        consteval std::size_t size() const noexcept {
            for (std::size_t i = 0; i < N; ++i) {
                if (data[i] == '\0') {
                    return i;
                }
            }
            return N > 0 ? N - 1 : 0;
        }

        constexpr std::string_view view() const noexcept {
            return std::string_view(data, size());
        }

        constexpr const char* c_str() const noexcept {
            return data;
        }

        // Conversion to string_view
        consteval operator std::string_view() const noexcept {
            return view();
        }

        // Indexing
        constexpr char& operator[](std::size_t i) noexcept {
            return data[i];
        }

        constexpr char operator[](std::size_t i) const noexcept {
            return data[i];
        }

        // Comparison
        consteval bool operator==(const fixed_string& other) const noexcept {
            return view() == other.view();
        }

        consteval bool operator==(std::string_view sv_other) const noexcept {
            return view() == sv_other;
        }
    };

    // Deduction guide for string literals
    template <std::size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;

    // Helper to convert various string types to string_view at compile time
    template <typename T> constexpr auto ct_string_view(const T& str) {
        if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
            return std::string_view{str, sizeof(str) - 1};
        } else if constexpr (requires { str.view(); }) {
            return str.view();
        } else {
            return std::string_view{str};
        }
    }

    // Get compile-time string length
    template <typename T> constexpr std::size_t ct_string_length(const T& str) {
        return ct_string_view(str).size();
    }

    template <typename... Args> consteval auto make_fixed_string(Args&&... args) {
        constexpr auto total_length = (ct_string_length(args) + ...);

        fixed_string<total_length + 1> result{};
        std::size_t                    pos = 0;

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            auto process_arg = [&](auto&& arg) {
                auto sv = ct_string_view(arg);
                std::copy(sv.begin(), sv.end(), result.data + pos);
                pos += sv.size();
            };
            (process_arg(std::get<I>(std::forward_as_tuple(args...))), ...);
        }(std::index_sequence_for<Args...>{});

        result.data[pos] = '\0';
        return result;
    }

// Helper macro for creating fixed_string with exact size from string literal
#define MAKE_FIXED_STRING(str)                                                                                         \
    ::storm::utils::fixed_string {                                                                                     \
        str                                                                                                            \
    }

    // Specialization for fixed_string in ct_string_view
    template <std::size_t N> consteval std::string_view ct_string_view(const fixed_string<N>& str) {
        return str.view();
    }

    // Helper to get string_view from various string types
    template <typename T> consteval std::string_view ct_string_view(const T& str) {
        if constexpr (requires { str.view(); }) {
            return str.view();
        } else if constexpr (std::is_convertible_v<T, std::string_view>) {
            return std::string_view{str};
        } else {
            static_assert(false, "Cannot convert to string_view at compile time");
        }
    }

    // Variadic compile-time string concatenation
    template <typename... Strings> consteval auto concat_ct(const Strings&... strings) {
        // Calculate total length
        constexpr std::size_t total_len = (ct_string_length(strings) + ...);

        // Create result buffer
        fixed_string<total_len + 1> result{};
        std::size_t                 pos = 0;

        // Copy each string
        auto copy_string = [&](const auto& str) {
            auto sv = ct_string_view(str);
            for (char c : sv) {
                result.data[pos++] = c;
            }
        };

        (copy_string(strings), ...);
        result.data[pos] = '\0';

        return result;
    }

    // Runtime to_lower function
    inline std::string to_lower(std::string_view sv) {
        std::string result = std::string(sv);
        std::transform(result.begin(), result.end(), result.begin(), [](char c) { return std::tolower(c); });
        return result;
    }

    // Enhanced version using std::transform and ranges (C++23)
    template <std::size_t N> consteval auto to_lower_ct(const char (&str)[N]) {
        fixed_string<N> result{};
        std::transform(str, str + N - 1, result.data, [](char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; });
        result.data[N - 1] = '\0';
        return result;
    }

    // Overload for string_view/fixed_string
    template <typename T>
        requires requires(T t) { ct_string_view(t); }
    consteval auto to_lower_ct(const T& input) {
        constexpr auto sv  = ct_string_view(input);
        constexpr auto len = sv.size();

        fixed_string<len + 1> result{};
        std::transform(sv.begin(), sv.end(), result.data, [](char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; });
        result.data[len] = '\0';
        return result;
    }

    template <typename TableName, typename FieldName>
    consteval auto formatFieldName(const TableName& tableName, const FieldName& fieldName) {
        return make_fixed_string("\"", tableName, "\".\"", fieldName, "\"");
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