#pragma once

/**
 * @file person.h
 * @brief Person model + its fields:: selector proxy.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include <cstdint>
#include <meta>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// Shared Person model — covers id/name/age tests, salary/experience aggregates,
// is_active ordering, optional score/nickname, and avatar BLOB.
struct Person {
    [[= storm::primary]] int id{};
    [[= storm::unique]] std::string name;
    int age{};
    double salary{};
    bool is_active{};
    int years_experience{};
    [[= storm::indexed]] std::string department;
    std::optional<int> score;
    std::optional<std::string> nickname;
    std::vector<uint8_t> avatar;

    // Composite indexes — nested-typedef opt-in (issue #464). This header is
    // textually included in several module TUs; an explicit Indexes<Person>
    // specialization would exist once per TU and trip clang-p2996's cross-BMI
    // declaration merging (ambiguous Indexes<Person>::type).
    using storm_indexes = std::tuple<storm::Index<^^Person::department, ^^Person::age>,
                                     storm::UniqueIndex<^^Person::name, ^^Person::department>>;
};

namespace fields {
struct PersonT;
consteval { std::meta::define_aggregate(^^PersonT, storm::field_specs_for(^^Person)); }
inline constexpr PersonT Person{};
} // namespace fields
