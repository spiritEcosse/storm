#pragma once

/**
 * @file restrict_child.h
 * @brief RestrictChild — FK to Person with ON DELETE RESTRICT (#431).
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <string>

// Per-FK ON DELETE policy model (#431).
struct RestrictChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::Restrict>]] Person owner;
    std::string label;
};
