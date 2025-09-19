module;

export module storm.aggregate;

import <string>;
import <optional>;
import <format>;
import <type_traits>;
import <any>;

import storm.reflect;
import storm.utils;
import storm.field;

export namespace storm {

    enum class AggregateKind { Max, Min, Avg, Count, Sum };

    // Compile-time helper functions for AggregateSpec
    consteval utils::fixed_string<8> get_function_name(AggregateKind k) {
        switch (k) {
        case AggregateKind::Max:
            return utils::fixed_string<8>{"MAX"};
        case AggregateKind::Min:
            return utils::fixed_string<8>{"MIN"};
        case AggregateKind::Avg:
            return utils::fixed_string<8>{"AVG"};
        case AggregateKind::Count:
            return utils::fixed_string<8>{"COUNT"};
        case AggregateKind::Sum:
            return utils::fixed_string<8>{"SUM"};
        }
    }

    consteval utils::fixed_string<8> get_alias_prefix(AggregateKind k) {
        switch (k) {
        case AggregateKind::Max:
            return utils::fixed_string<8>{"max_"};
        case AggregateKind::Min:
            return utils::fixed_string<8>{"min_"};
        case AggregateKind::Avg:
            return utils::fixed_string<8>{"avg_"};
        case AggregateKind::Count:
            return utils::fixed_string<8>{"count_"};
        case AggregateKind::Sum:
            return utils::fixed_string<8>{"sum_"};
        }
    }

    consteval std::string_view extract_simple_field_name(std::string_view full_name) {
        auto dot_pos = full_name.find_last_of('.');
        if (dot_pos != std::string_view::npos) {
            return full_name.substr(dot_pos + 1);
        } else {
            return full_name;
        }
    }

    struct AggregateSpec {
        AggregateKind                     kind;
        std::optional<refl::FieldWrapper> field; // Optional to support custom SQL
        utils::fixed_string<32>           alias{};
        std::optional<std::string>        _custom_sql; // For custom SQL functions like GROUP_CONCAT

        // Factory methods with compile-time validation
        template <auto Field> static AggregateSpec max(utils::fixed_string<32> alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MAX requires comparable field type");
            auto field_wrapper = refl::FieldWrapper::create(storm::Field<Field>{});
            return AggregateSpec{
                    .kind = AggregateKind::Max, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec min(utils::fixed_string<32> alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MIN requires comparable field type");
            auto field_wrapper = refl::FieldWrapper::create(storm::Field<Field>{});
            return AggregateSpec{
                    .kind = AggregateKind::Min, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec avg(utils::fixed_string<32> alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with AVG
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "AVG can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create(storm::Field<Field>{});
            return AggregateSpec{
                    .kind = AggregateKind::Avg, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec count(utils::fixed_string<32> alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field_wrapper = refl::FieldWrapper::create(storm::Field<Field>{});
            return AggregateSpec{
                    .kind = AggregateKind::Count, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto Field> static AggregateSpec sum(utils::fixed_string<32> alias = {}) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with SUM
            using FieldType = typename refl::meta::member_pointer_traits<decltype(Field)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "SUM can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create(storm::Field<Field>{});
            return AggregateSpec{
                    .kind = AggregateKind::Sum, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        // Field-based overloads
        template <auto MemberPtr> static AggregateSpec max(Field<MemberPtr> field, utils::fixed_string<32> alias = {}) {
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MAX requires comparable field type");
            auto field_wrapper = refl::FieldWrapper::create(field);
            return AggregateSpec{
                    .kind = AggregateKind::Max, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto MemberPtr> static AggregateSpec min(Field<MemberPtr> field, utils::fixed_string<32> alias = {}) {
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MIN requires comparable field type");
            auto field_wrapper = refl::FieldWrapper::create(field);
            return AggregateSpec{
                    .kind = AggregateKind::Min, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto MemberPtr> static AggregateSpec avg(Field<MemberPtr> field, utils::fixed_string<32> alias = {}) {
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "AVG can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create(field);
            return AggregateSpec{
                    .kind = AggregateKind::Avg, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto MemberPtr>
        static AggregateSpec count(Field<MemberPtr> field, utils::fixed_string<32> alias = {}) {
            auto field_wrapper = refl::FieldWrapper::create(field);
            return AggregateSpec{
                    .kind = AggregateKind::Count, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        template <auto MemberPtr> static AggregateSpec sum(Field<MemberPtr> field, utils::fixed_string<32> alias = {}) {
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(
                    std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                    "SUM can only be used with numeric fields"
            );
            auto field_wrapper = refl::FieldWrapper::create(field);
            return AggregateSpec{
                    .kind = AggregateKind::Sum, .field = field_wrapper, .alias = alias, ._custom_sql = std::nullopt
            };
        }

        // Factory for custom SQL functions (like GROUP_CONCAT)
        static AggregateSpec custom_sql(std::string_view sql) {
            return AggregateSpec{
                    .kind        = AggregateKind::Count, // Placeholder
                    .field       = std::nullopt,         // No field for custom SQL
                    .alias       = {},                   // Empty fixed_string
                    ._custom_sql = std::string{sql}
            };
        }

        // Generate SQL representation at compile time
        [[nodiscard]] consteval utils::fixed_string<256> to_sql() const {
            // If custom SQL is provided, use it directly
            if (_custom_sql) {
                return utils::fixed_string<256>{_custom_sql->c_str()};
            }

            const auto function_name = get_function_name(kind);

            if (!field) {
                return utils::fixed_string<256>{}; // Empty for invalid aggregates
            }

            const auto field_name        = field->view();
            const auto simple_field_name = extract_simple_field_name(field_name);

            // Generate actual alias
            const auto actual_alias = [&]() -> utils::fixed_string<64> {
                if (alias.view().size() > 0) {
                    return utils::make_string_builder<64>().append(alias.view()).build();
                } else {
                    const auto prefix = get_alias_prefix(kind);
                    return utils::make_string_builder<64>().append(prefix.view()).append(simple_field_name).build();
                }
            }();

            // Build final SQL at compile time: "FUNCTION(field) AS alias"
            return utils::make_string_builder<256>()
                    .append(function_name.view())
                    .append("(")
                    .append(field_name)
                    .append(") AS ")
                    .append(actual_alias.view())
                    .build();
        }
    };

} // namespace storm
