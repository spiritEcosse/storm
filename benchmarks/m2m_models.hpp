#pragma once

/**
 * Many-to-many benchmark models (#391).
 *
 * Annotated model structs for the m2m eager-load benchmark. Like the other
 * bench models, these MUST be textually visible in the GMF of any TU that
 * instantiates an ORM template on them — reflection annotations are blind
 * across BMI boundaries (clang-p2996 #262, feedback_cpp26_module_reflection_annotations),
 * and defining them in a module purview segfaults the reflection serializer.
 *
 * Include AFTER benchmarks/models.hpp (which runs `import storm;` and pulls in
 * <string>/<vector> via shared/models.h) and BEFORE the consumer's own imports.
 */

namespace storm::benchmark::m2m {

    struct BCourse {
        [[= storm::meta::primary]] int id{};
        std::string                    title;
    };

    struct BStudent {
        [[= storm::meta::primary]] int                         id{};
        std::string                                            name;
        int                                                    age{};
        [[= storm::meta::many_to_many<>]] std::vector<BCourse> courses;
    };

    // fields:: selector proxies (#518). <meta> arrives transitively via
    // benchmarks/models.hpp, which this header must be included after.
    namespace fields {

        struct BStudentT;
        consteval {
            std::meta::define_aggregate(^^BStudentT, storm::field_specs_for(^^BStudent));
        }
        inline constexpr BStudentT BStudent{};

    } // namespace fields

} // namespace storm::benchmark::m2m
