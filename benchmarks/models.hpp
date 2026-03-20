#pragma once

/**
 * Benchmark Models
 *
 * Defines all models used in benchmarks.
 * This file must be included before runner.hpp to provide complete types.
 */

import storm;
import <optional>;

namespace storm::benchmark {

    // Test model for basic operations (INSERT, UPDATE, DELETE, SELECT WHERE)
    struct Person {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        int                                       age;
        bool                                      is_active;
        double                                    salary;
        std::optional<int>                        score;
    };

    // Test models for JOIN operations
    struct User {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        int                                       age;
    };

    struct FKMessage {
        [[= storm::meta::FieldAttr::primary]] int id;
        [[= storm::meta::FieldAttr::fk]] User     sender;
        [[= storm::meta::FieldAttr::fk]] User     receiver;
        std::string                               text;
    };

} // namespace storm::benchmark
