// StringUtils.h
#pragma once

// Legacy header placeholder; use C++23 modules (import storm.utils).
import <string>;
import <string_view>;
import <format>;
import <ranges>;
import <algorithm>;
import <cctype>;

namespace storm::utils {

    inline auto to_lower(std::string str) -> std::string {
        std::ranges::transform(str, str.begin(), [](unsigned char c) { return std::tolower(c); });
        return str;
    }

    std::string formatFieldName(const std::string& tableName, const std::string& fieldName) {
        return std::format(R"("{}"."{}")", tableName, fieldName);
    }

    template <typename Range> inline std::string join(const Range& range, std::string_view delim) {
        std::string out;
        bool        first = true;
        for (const auto& elem : range) {
            if (!first)
                out.append(delim);
            first = false;
            if constexpr (requires { std::string_view{elem}; }) {
                out.append(std::string_view{elem});
            } else {
                out.append(static_cast<const std::string&>(elem));
            }
        }
        return out;
    }

} // namespace storm::utils
