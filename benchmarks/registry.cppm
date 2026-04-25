// storm_benchmark_registry
//
// Compile-time model registry for benchmark schema-driven dispatch. Maps
// BenchmarkTest.model / .join string fields to concrete C++ types and
// member pointers, all at compile time.
//
// The annotated model types (Person / User / FKMessage) live in models.hpp
// because C++26 reflection-annotated structs are simpler to consume from
// plain headers — see models.hpp itself for the rationale. We pull that
// header in via the GMF so its declarations are textually visible inside
// this module's purview, then re-export the names via using-declarations.

module;

// shared/models.h (pulled in by models.hpp) uses std::tuple for the
// Indexes<Person>::type typedef — needs to be visible textually before that
// header expands inside the GMF.
#include <tuple>

#include "models.hpp"
#include "schema.hpp"

#include <stdexcept>

export module storm_benchmark_registry;

import storm;

export namespace storm::benchmark {
    using ::storm::benchmark::FKMessage;
    using ::storm::benchmark::Person;
    using ::storm::benchmark::User;
} // namespace storm::benchmark

export namespace storm::benchmark::registry {

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
    // concrete type matching test.model. Enables QueryBenchmark to resolve
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
