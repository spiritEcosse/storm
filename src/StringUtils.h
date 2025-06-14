// StringUtils.h
#pragma once

#include <string>
#include <algorithm> // For std::transform
#include <cctype>    // For std::tolower

namespace storm::utils {

// Helper to convert a string to lowercase
inline std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return str;
}

} // namespace storm::utils
