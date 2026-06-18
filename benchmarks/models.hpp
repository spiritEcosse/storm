#pragma once

/**
 * Benchmark Models
 *
 * Person comes from shared/models.h (single source of truth).
 * User and FKMessage are benchmark-specific (JOIN benchmarks with 2 FKs).
 *
 * This file must be included after `import storm;` to provide complete types.
 */

import storm;

// Shared model structs — used by both tests and benchmarks
#include "../shared/models.h"

namespace storm::benchmark {

    // Bring shared Person into benchmark namespace
    using ::Person;

    // Benchmark-specific models for JOIN tests (2 FK relationships)
    struct User {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        int                                       age;
    };

    struct FKMessage {
        [[= storm::meta::FieldAttr::primary]] int id;
        [[= storm::meta::fk<>]] User              sender;
        [[= storm::meta::fk<>]] User              receiver;
        std::string                               text;
    };

    // INSERT-benchmark model — no UNIQUE, no indexes; matches the raw SQLite
    // anchor schema exactly so bulk inserts need no per-iteration DELETE.
    struct BenchPerson {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        int                                       age;
        double                                    salary;
    };

} // namespace storm::benchmark
