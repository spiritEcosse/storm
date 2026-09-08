#pragma once

/**
 * @file extended_types.h
 * @brief ExtendedTypes model + its fields:: selector proxy.
 *
 * The widest model in the shared set — every supported column type. Include
 * AFTER `import storm;` — see models.h for why.
 */

#include "color.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <meta>
#include <optional>
#include <string>
#include <vector>

// Extended types model — covers all supported SQLite column types.
struct ExtendedTypes {
    [[= storm::primary]] int id{};
    int64_t big_num{};
    double precise{};
    float approx{};
    unsigned int u_int{};
    long long ll_signed{};
    [[= storm::signed_storage]] std::uint64_t big_unsigned{};     // signed int64 storage (#419/#436)
    [[= storm::full_unsigned]] std::uint64_t big_unsigned_full{}; // order-preserving full-range storage (#436)
    std::optional<double> opt_double;
    std::optional<int64_t> opt_int64;
    std::string label;
    signed char tiny_signed{};
    unsigned char tiny_unsigned{};
    char single_char{};
    Color color{Color::Red};
    std::chrono::year_month_day date_field{std::chrono::year{2000} / std::chrono::January / std::chrono::day{1}};
    std::chrono::system_clock::time_point datetime_field{};
    std::chrono::seconds duration_field{};
    std::filesystem::path file_path;
    std::vector<std::byte> raw_data;
    storm::UUID uuid_field;
    std::optional<Color> opt_color;
    std::optional<std::chrono::system_clock::time_point> opt_timestamp;
    std::optional<std::filesystem::path> opt_path;
};

namespace fields {
struct ExtendedTypesT;
consteval { std::meta::define_aggregate(^^ExtendedTypesT, storm::field_specs_for(^^ExtendedTypes)); }
inline constexpr ExtendedTypesT ExtendedTypes{};
} // namespace fields
