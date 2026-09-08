#pragma once

/**
 * @file cascade_child.h
 * @brief CascadeChild — FK to Person with ON DELETE CASCADE (#431).
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "person.h"

#include <string>

// Per-FK ON DELETE policy model (#431). The FK annotation fk<RefAction::...> carries
// the ON DELETE policy; the schema generator emits the matching "ON DELETE <action>" clause.
struct CascadeChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::Cascade>]] Person owner;
    std::string label;
};
