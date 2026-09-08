#pragma once

/**
 * @file uuid_pk_ref.h
 * @brief UuidPkRef — FK to a UUID-PK model.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "uuid_pk_model.h"

#include <string>

// Foreign key to UUID-PK model — tests DDL generation for FK columns referencing UUID PKs (#507).
struct UuidPkRef {
    [[= storm::primary]] int id{};
    [[= storm::fk<>]] UuidPkModel owner;
    std::string value;
};
