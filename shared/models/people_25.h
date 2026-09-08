#pragma once

/**
 * @file people_25.h
 * @brief PEOPLE_25 — the 25-row Person seed dataset.
 *
 * Split out from person.h because only ~20 of the ~97 model-using TUs seed it,
 * and the 25 designated initialisers are parsed by every TU that pulls them in.
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <array>
#include <optional>
#include <string>

namespace storm::test {

// clang-format off
inline constexpr std::array<Person, 25> PEOPLE_25 = {
    Person{.name = "Alice",   .age = 25, .salary = 55000.0, .is_active = true,  .years_experience = 5,  .department = "Engineering", .score = std::optional<int>(85),      .nickname = std::optional<std::string>("Ali")},
    Person{.name = "Bob",     .age = 30, .salary = 62000.0, .is_active = true,  .years_experience = 10, .department = "Sales",       .score = std::optional<int>(90),      .nickname = std::optional<std::string>("Bobby")},
    Person{.name = "Charlie", .age = 35, .salary = 78000.0, .is_active = false, .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Diana",   .age = 28, .salary = 48000.0, .is_active = true,  .years_experience = 5,  .department = "HR",          .score = std::optional<int>(75),      .nickname = std::optional<std::string>("Di")},
    Person{.name = "Eve",     .age = 40, .salary = 92000.0, .is_active = false, .years_experience = 10, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Frank",   .age = 45, .salary = 88000.0, .is_active = true,  .years_experience = 15, .department = "Sales",       .score = std::optional<int>(60),      .nickname = std::nullopt},
    Person{.name = "Grace",   .age = 25, .salary = 52000.0, .is_active = true,  .years_experience = 5,  .department = "Marketing",   .score = std::optional<int>(95),      .nickname = std::optional<std::string>("Gracie")},
    Person{.name = "Henry",   .age = 33, .salary = 70000.0, .is_active = false, .years_experience = 10, .department = "Support",     .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Ivy",     .age = 30, .salary = 65000.0, .is_active = true,  .years_experience = 5,  .department = "Engineering", .score = std::optional<int>(80),      .nickname = std::optional<std::string>("Iv")},
    Person{.name = "Jack",    .age = 38, .salary = 85000.0, .is_active = false, .years_experience = 15, .department = "HR",          .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Karen",   .age = 25, .salary = 50000.0, .is_active = true,  .years_experience = 5,  .department = "Sales",       .score = std::optional<int>(85),      .nickname = std::optional<std::string>("Kiki")},
    Person{.name = "Leo",     .age = 42, .salary = 95000.0, .is_active = true,  .years_experience = 10, .department = "Engineering", .score = std::optional<int>(70),      .nickname = std::nullopt},
    Person{.name = "Mia",     .age = 28, .salary = 46000.0, .is_active = true,  .years_experience = 5,  .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Nick",    .age = 35, .salary = 72000.0, .is_active = false, .years_experience = 15, .department = "Support",     .score = std::optional<int>(55),      .nickname = std::optional<std::string>("Nicky")},
    Person{.name = "Olivia",  .age = 48, .salary = 98000.0, .is_active = true,  .years_experience = 10, .department = "Sales",       .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Paul",    .age = 22, .salary = 32000.0, .is_active = false, .years_experience = 5,  .department = "HR",          .score = std::optional<int>(40),      .nickname = std::nullopt},
    Person{.name = "Quinn",   .age = 30, .salary = 67000.0, .is_active = true,  .years_experience = 10, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Rachel",  .age = 36, .salary = 76000.0, .is_active = false, .years_experience = 5,  .department = "Support",     .score = std::optional<int>(65),      .nickname = std::optional<std::string>("Rach")},
    Person{.name = "Sam",     .age = 40, .salary = 90000.0, .is_active = true,  .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Tina",    .age = 27, .salary = 44000.0, .is_active = true,  .years_experience = 5,  .department = "Sales",       .score = std::optional<int>(88),      .nickname = std::optional<std::string>("T")},
    Person{.name = "Uma",     .age = 33, .salary = 69000.0, .is_active = false, .years_experience = 10, .department = "HR",          .score = std::optional<int>(50),      .nickname = std::nullopt},
    Person{.name = "Victor",  .age = 45, .salary = 93000.0, .is_active = true,  .years_experience = 15, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Wendy",   .age = 29, .salary = 58000.0, .is_active = true,  .years_experience = 10, .department = "Support",     .score = std::optional<int>(78),      .nickname = std::optional<std::string>("Wen")},
    Person{.name = "Xander",  .age = 38, .salary = 82000.0, .is_active = false, .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Yara",    .age = 22, .salary = 35000.0, .is_active = true,  .years_experience = 5,  .department = "Support",     .score = std::optional<int>(92),      .nickname = std::optional<std::string>("Yari")},
};
// clang-format on

} // namespace storm::test
