// StringUtils.h
#pragma once

#include <string>
#include <algorithm> // For std::transform
#include <cctype>    // For std::tolower
#include <fmt/format.h>

namespace storm::utils {

inline auto to_lower(std::string str) -> std::string {
    std::ranges::transform(str, str.begin(),
                          [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::string formatFieldName(const std::string& tableName, const std::string& fieldName) {
    return fmt::format(R"("{}"."{}")", tableName, fieldName);
}

} // namespace storm::utils
