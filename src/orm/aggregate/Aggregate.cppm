module;

export module storm.aggregate;

import <string>;
import <optional>;
import <format>;
import <type_traits>;
import <any>;

import storm.reflect;

export namespace storm {

    enum class AggregateKind { Max, Min, Avg, Count, Sum };

    struct AggregateSpec {
        AggregateKind                     kind;
        std::optional<refl::FieldWrapper> field; // Optional to support custom SQL
        std::optional<std::string>        alias;
        std::optional<std::string>        _custom_sql; // For custom SQL functions like GROUP_CONCAT

        // Factory methods with compile-time validation
        template <auto Field> static AggregateSpec max(std::string_view alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field_wrapper = refl::FieldWrapper::create<Field>();
            return AggregateSpec{
                    .kind        = AggregateKind::Max,
                    .field       = field_wrapper,
                    .alias       = alias.empty() ? std::nullopt : std::make_optional(std::string{alias}),
                    ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec min(std::string_view alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field_wrapper = refl::FieldWrapper::create<Field>();
            return AggregateSpec{
                    .kind        = AggregateKind::Min,
                    .field       = field_wrapper,
                    .alias       = alias.empty() ? std::nullopt : std::make_optional(std::string{alias}),
                    ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec avg(std::string_view alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with AVG
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "AVG can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create<Field>();
            return AggregateSpec{
                    .kind        = AggregateKind::Avg,
                    .field       = field_wrapper,
                    .alias       = alias.empty() ? std::nullopt : std::make_optional(std::string{alias}),
                    ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec count(std::string_view alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field_wrapper = refl::FieldWrapper::create<Field>();
            return AggregateSpec{
                    .kind        = AggregateKind::Count,
                    .field       = field_wrapper,
                    .alias       = alias.empty() ? std::nullopt : std::make_optional(std::string{alias}),
                    ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec sum(std::string_view alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with SUM
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "SUM can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create<Field>();
            return AggregateSpec{
                    .kind        = AggregateKind::Sum,
                    .field       = field_wrapper,
                    .alias       = alias.empty() ? std::nullopt : std::make_optional(std::string{alias}),
                    ._custom_sql = std::nullopt
            };
        }

        // Factory for custom SQL functions (like GROUP_CONCAT)
        static AggregateSpec custom_sql(std::string_view sql) {
            return AggregateSpec{
                    .kind        = AggregateKind::Count, // Placeholder
                    .field       = std::nullopt,         // No field for custom SQL
                    .alias       = std::nullopt,
                    ._custom_sql = std::string{sql}
            };
        }

        // Generate SQL representation
        [[nodiscard]] std::string to_sql() const {
            // If custom SQL is provided, use it directly
            if (_custom_sql) {
                return *_custom_sql;
            }

            std::string function_name;
            switch (kind) {
            case AggregateKind::Max:
                function_name = "MAX";
                break;
            case AggregateKind::Min:
                function_name = "MIN";
                break;
            case AggregateKind::Avg:
                function_name = "AVG";
                break;
            case AggregateKind::Count:
                function_name = "COUNT";
                break;
            case AggregateKind::Sum:
                function_name = "SUM";
                break;
            }

            if (!field) {
                return ""; // Should not happen for regular aggregates
            }

            const auto field_name = field->view();

            // Extract simple field name from full name (e.g., "table.field" -> "field")
            std::string simple_field_name{field_name};
            if (auto dot_pos = simple_field_name.find_last_of('.'); dot_pos != std::string::npos) {
                simple_field_name = simple_field_name.substr(dot_pos + 1);
            }

            const auto actual_alias = alias ? *alias
                                            : std::format(
                                                      "{}_{}",
                                                      function_name == "MAX"     ? "max"
                                                      : function_name == "MIN"   ? "min"
                                                      : function_name == "AVG"   ? "avg"
                                                      : function_name == "COUNT" ? "count"
                                                                                 : "sum",
                                                      simple_field_name
                                              );

            return std::format("{}({}) AS {}", function_name, field_name, actual_alias);
        }
    };

} // namespace storm
