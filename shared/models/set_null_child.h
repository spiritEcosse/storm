#pragma once

/**
 * @file set_null_child.h
 * @brief SetNullChild — FK to Person with ON DELETE SET NULL (#431).
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <optional>
#include <string>

// Per-FK ON DELETE policy model (#431). SET NULL requires a nullable FK (std::optional).
struct SetNullChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::SetNull>]] std::optional<Person> owner;
    std::string label;
};
