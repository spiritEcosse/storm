#pragma once

/**
 * @file uuid_pk_model.h
 * @brief UuidPkModel — UUID primary key model.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include <string>

// UUID primary key model — tests DDL generation for UUID PKs (#507).
// UUID PKs are client-generated (not DB-generated like AUTOINCREMENT).
struct UuidPkModel {
    [[= storm::primary]] storm::UUID id = storm::UUID::generate();
    std::string name;
};
