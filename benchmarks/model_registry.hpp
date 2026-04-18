#pragma once

/**
 * Compile-Time Model Registry for Benchmark Schema-Driven Dispatch
 *
 * Maps BenchmarkTest.model / .join fields to concrete C++ types and
 * member pointers at compile time.  Only three models exist in the
 * benchmark suite: Person, User, FKMessage.
 *
 * Provides:
 *   - resolve_fk_ptr(name)   : FK field name  → FKMessage member pointer
 *   - FKRelated              : type alias for the related model (User)
 *   - with_base_model<test>  : dispatches a generic lambda with the right
 *                              base-model type deduced from test.model
 */

#include "models.hpp"
#include "schema.hpp"

namespace storm::benchmark::registry {

    // ========================================================================
    // FK field resolution for FKMessage (the only FK model in benchmarks).
    // Returns User FKMessage::* member pointer by field name.
    // ========================================================================
    consteval auto resolve_fk_ptr(std::string_view name) {
        if (name == "sender") {
            return &FKMessage::sender;
        }
        if (name == "receiver") {
            return &FKMessage::receiver;
        }
        throw std::runtime_error("Unknown FK field"); // NOSONAR(cpp:S112)
    }

    // The related model for all FKMessage FK fields is User.
    using FKRelated = User;

    // ========================================================================
    // Model dispatch — calls fn.template operator()<ModelType>() with the
    // concrete type matching test.model.  Enables QueryBenchmark to resolve
    // Person / FKMessage / User without hard-coding in TestExecutor.
    //
    // Usage:
    //   registry::with_base_model<test>([&]<typename M>() {
    //       runner.run_benchmark(..., QueryBenchmark<M, test>{size}, ...);
    //   });
    // ========================================================================
    template <auto const& test, typename F> auto with_base_model(F fn) {
        if constexpr (test.model == "FKMessage") {
            return fn.template operator()<FKMessage>();
        } else if constexpr (test.model == "User") {
            return fn.template operator()<User>();
        } else {
            return fn.template operator()<Person>();
        }
    }

} // namespace storm::benchmark::registry
