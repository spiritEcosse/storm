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
import <functional>;
import <optional>;

export namespace storm::utils {

    /**
     * Enhanced compile-time string with C++26 features
     */
    template <std::size_t N> 
    struct fixed_string {
        static constexpr std::size_t capacity = N;
        char data[N] = {};

        consteval fixed_string() = default;

        // C-string constructor with explicit template parameter
        template<std::size_t M>
        consteval fixed_string(const char (&str)[M]) 
            requires(M <= N)
        {
            std::copy_n(str, M - 1, data);
            data[M - 1] = '\0';
        }

        // String view constructor
        consteval explicit fixed_string(std::string_view sv)
            requires(N > 0)
        {
            if (sv.size() >= N) {
                throw "String too long for fixed_string";
            }
            std::copy(sv.begin(), sv.end(), data);
            data[sv.size()] = '\0';
        }

        consteval std::size_t size() const noexcept {
            return std::find(data, data + N, '\0') - data;
        }

        constexpr std::string_view view() const noexcept {
            return std::string_view(data, size());
        }

        constexpr const char* c_str() const noexcept { return data; }
        consteval operator std::string_view() const noexcept { return view(); }

        constexpr char& operator[](std::size_t i) noexcept { return data[i]; }
        constexpr char operator[](std::size_t i) const noexcept { return data[i]; }

        consteval auto operator<=>(const fixed_string& other) const noexcept = default;
        consteval bool operator==(std::string_view sv_other) const noexcept {
            return view() == sv_other;
        }

        // Monadic operations - transform the string
        template<typename F>
        consteval auto transform(F&& func) const {
            fixed_string<N> result{};
            std::transform(data, data + size(), result.data, std::forward<F>(func));
            result.data[size()] = '\0';
            return result;
        }

        // Monadic operations - filter characters
        template<typename Pred>
        consteval auto filter(Pred&& pred) const {
            fixed_string<N> result{};
            auto it = std::copy_if(data, data + size(), result.data, std::forward<Pred>(pred));
            *it = '\0';
            return result;
        }

        // Chain operations (monadic bind)
        template<typename F>
        consteval auto and_then(F&& func) const {
            return func(*this);
        }
    };

    // Deduction guides
    template <std::size_t N> 
    fixed_string(const char (&)[N]) -> fixed_string<N>;
    
    fixed_string(std::string_view) -> fixed_string<256>; // Default size

    // C++26 Parameter pack indexing for better variadic handling
    template<std::size_t I, typename... Args>
    consteval auto get_nth_arg(Args&&... args) {
        return std::get<I>(std::forward_as_tuple(args...));
    }

    // Simple concat function with fixed maximum size
    template<typename... Args>
    consteval auto concat(Args&&... args) {
        constexpr std::size_t max_size = 1024; // Fixed maximum size
        fixed_string<max_size> result{};
        std::size_t pos = 0;
        
        auto append = [&](const auto& arg) {
            auto sv = [&] {
                if constexpr (requires { arg.view(); }) return arg.view();
                else return std::string_view{arg};
            }();
            
            if (pos + sv.size() < max_size) {
                std::copy(sv.begin(), sv.end(), result.data + pos);
                pos += sv.size();
            }
        };
        
        (append(args), ...);
        result.data[pos] = '\0';
        return result;
    }

    // Simplified string builder with runtime concatenation
    template<std::size_t MaxSize = 1024>
    struct string_builder {
        fixed_string<MaxSize> buffer{};
        std::size_t current_pos = 0;
        
        consteval string_builder() = default;
        
        template<typename T>
        consteval auto append(T&& value) const {
            string_builder result = *this;
            
            auto sv = [&] {
                if constexpr (requires { value.view(); }) return value.view();
                else return std::string_view{value};
            }();
            
            if (result.current_pos + sv.size() < MaxSize) {
                std::copy(sv.begin(), sv.end(), result.buffer.data + result.current_pos);
                result.current_pos += sv.size();
                result.buffer.data[result.current_pos] = '\0';
            }
            
            return result;
        }
        
        consteval auto build() const { return buffer; }
    };

    // Factory function for string builder
    template<std::size_t Size = 1024>
    consteval auto make_string_builder() {
        return string_builder<Size>{};
    }

    // Enhanced compile-time string transformations
    namespace transforms {
        constexpr auto to_lower = [](char c) consteval { 
            return (c >= 'A' && c <= 'Z') ? c + 32 : c; 
        };
        
        constexpr auto to_upper = [](char c) consteval { 
            return (c >= 'a' && c <= 'z') ? c - 32 : c; 
        };
        
        constexpr auto is_alpha = [](char c) consteval { 
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); 
        };
        
        constexpr auto is_digit = [](char c) consteval { 
            return c >= '0' && c <= '9'; 
        };
    }

    // Variadic template with parameter pack indexing (C++26)
    template<typename... Strings>
    consteval auto join_with(std::string_view separator, Strings&&... strings) {
        if constexpr (sizeof...(strings) == 0) {
            return fixed_string<1>{""};
        } else if constexpr (sizeof...(strings) == 1) {
            return concat(strings...);
        } else {
            constexpr auto total_content_size = ([]<typename T>(const T& s) {
                if constexpr (requires { s.size(); }) return s.size();
                else return std::string_view{s}.size();
            }(strings) + ...);
            
            constexpr auto total_sep_size = separator.size() * (sizeof...(strings) - 1);
            
            fixed_string<total_content_size + total_sep_size + 1> result{};
            std::size_t pos = 0;
            std::size_t count = 0;
            
            auto append_with_sep = [&](const auto& str) {
                if (count++ > 0) {
                    std::copy(separator.begin(), separator.end(), result.data + pos);
                    pos += separator.size();
                }
                auto sv = [&] {
                    if constexpr (requires { str.view(); }) return str.view();
                    else return std::string_view{str};
                }();
                std::copy(sv.begin(), sv.end(), result.data + pos);
                pos += sv.size();
            };
            
            (append_with_sep(strings), ...);
            result.data[pos] = '\0';
            return result;
        }
    }

    // Advanced field formatting with monadic operations
    template<typename TableName, typename FieldName>
    consteval auto formatFieldName(const TableName& table, const FieldName& field) {
        return make_string_builder<256>()
            .append("\"")
            .append(table)
            .append("\".\"")
            .append(field)
            .append("\"")
            .build();
    }

    // Compile-time regex-like pattern matching (simple version)
    template<fixed_string Pattern>
    struct ct_pattern {
        static constexpr auto pattern = Pattern;
        
        consteval bool matches(std::string_view input) const {
            // Simple wildcard matching (* and ?)
            return matches_impl(input.begin(), input.end(), 
                               pattern.view().begin(), pattern.view().end());
        }
        
    private:
        consteval bool matches_impl(auto input_it, auto input_end,
                                   auto pattern_it, auto pattern_end) const {
            while (pattern_it != pattern_end && input_it != input_end) {
                if (*pattern_it == '*') {
                    ++pattern_it;
                    if (pattern_it == pattern_end) return true;
                    while (input_it != input_end) {
                        if (matches_impl(input_it, input_end, pattern_it, pattern_end))
                            return true;
                        ++input_it;
                    }
                    return false;
                } else if (*pattern_it == '?' || *pattern_it == *input_it) {
                    ++pattern_it;
                    ++input_it;
                } else {
                    return false;
                }
            }
            while (pattern_it != pattern_end && *pattern_it == '*') ++pattern_it;
            return pattern_it == pattern_end && input_it == input_end;
        }
    };

    // Factory for pattern matching
    template<fixed_string Pattern>
    consteval auto make_pattern() {
        return ct_pattern<Pattern>{};
    }

    // Enhanced runtime utilities with ranges
    template<std::ranges::input_range Range>
    requires requires(const std::ranges::range_reference_t<Range>& v) { 
        std::string_view{v}; 
    }
    inline std::string join(const Range& rng, std::string_view delim) {
        if (std::ranges::empty(rng)) return {};
        
        auto it = std::ranges::begin(rng);
        std::string result{std::string_view{*it}};
        
        for (++it; it != std::ranges::end(rng); ++it) {
            result += delim;
            result += std::string_view{*it};
        }
        return result;
    }

    // Compile-time string literals with user-defined literal
    namespace literals {
        template<fixed_string Str>
        consteval auto operator""_fs() {
            return Str;
        }
    }

} // namespace storm::utils

// Usage examples in comments:
/*
using namespace storm::utils;
using namespace storm::utils::literals;

constexpr auto greeting = "Hello"_fs
    .transform(transforms::to_upper)
    .and_then([](auto s) { return concat(s, " WORLD!"); });

constexpr auto joined = join_with(", ", "apple", "banana", "cherry");

constexpr auto pattern = make_pattern<"test_*">();
static_assert(pattern.matches("test_123"));

constexpr auto field = formatFieldName("users", "name");
*/