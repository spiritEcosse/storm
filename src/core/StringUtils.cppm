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
  std::string data;

  explicit consteval fixed_string(const char (&str)[N]) {
    data.reserve(N - 1); // -1 for null terminator
    for (std::size_t i = 0; i < N - 1; ++i) {
      data.push_back(str[i]);
    }
  }

  explicit consteval fixed_string(std::string_view sv)
    requires(N == sv.size() + 1)
  {
    data.reserve(sv.size());
    for (std::size_t i = 0; i < sv.size(); ++i) {
      data.push_back(sv[i]);
    }
  }

  constexpr std::string_view view() const { return std::string_view{data}; }

  constexpr const char *c_str() const { return data.c_str(); }
};

inline auto to_lower(std::string str) -> std::string {
  std::ranges::transform(str, str.begin(),
                         [](unsigned char c) { return std::tolower(c); });
  return str;
}

std::string formatFieldName(const std::string &tableName,
                            const std::string &fieldName) {
  return std::format(R"("{}"."{}")", tableName, fieldName);
}

// Generic join helper for ranges of string-like elements
template <std::ranges::input_range Range>
  requires requires(const std::ranges::range_reference_t<Range> &v) {
    std::string_view{v};
  }
inline std::string join(const Range &rng, std::string_view delim) {
  std::string out;
  bool first = true;
  for (const auto &elem : rng) {
    if (!first)
      out += delim;
    first = false;
    out += std::string_view{elem};
  }
  return out;
}

} // namespace storm::utils
