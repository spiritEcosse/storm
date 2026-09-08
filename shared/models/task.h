#pragma once

/**
 * @file task.h
 * @brief Task model (two FKs to Person) + its fields:: selector proxy.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <meta>
#include <string>

struct Task {
    [[= storm::primary]] int id{};
    [[= storm::fk<>]] Person assignee;
    [[= storm::fk<>]] Person reviewer;
    std::string description;
};

namespace fields {
struct TaskT;
consteval { std::meta::define_aggregate(^^TaskT, storm::field_specs_for(^^Task)); }
inline constexpr TaskT Task{};
} // namespace fields
