#pragma once

/**
 * @file message.h
 * @brief Message model (FK to Person) + its fields:: selector proxy.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <meta>
#include <string>

// Shared Message model — covers FK join tests. Sender is a Person.
struct Message {
    [[= storm::primary]] int id{};
    std::string content;
    int value{};
    [[= storm::fk<>]] Person sender;
};

namespace fields {
struct MessageT;
consteval { std::meta::define_aggregate(^^MessageT, storm::field_specs_for(^^Message)); }
inline constexpr MessageT Message{};
} // namespace fields
