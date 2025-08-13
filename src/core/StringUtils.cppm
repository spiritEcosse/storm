module;

// Module global fragment (no legacy includes)

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

inline auto to_lower(std::string str) -> std::string {
    std::ranges::transform(str, str.begin(),
                          [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::string formatFieldName(const std::string& tableName, const std::string& fieldName) {
    return std::format(R"("{}"."{}")", tableName, fieldName);
}

// Generic join helper for ranges of string-like elements
template <std::ranges::input_range Range>
    requires requires (const std::ranges::range_reference_t<Range>& v) {
        std::string_view{v};
    }
inline std::string join(const Range& rng, std::string_view delim) {
    std::string out;
    bool first = true;
    for (const auto& elem : rng) {
        if (!first) out += delim;
        first = false;
        out += std::string_view{elem};
    }
    return out;
}

} // namespace storm::utils
