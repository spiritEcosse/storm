module;

export module storm.field_desc;

// Standard header units
import <string>;
import <string_view>;
import <utility>;

// Storm modules
import storm.reflect;     // for refl::reflect and fixed_string
import storm.type_traits; // for member_pointer_traits
import storm.utils;

export namespace storm {

    // Lightweight value descriptor for a field selected via member pointer NTTP
    struct FieldDesc {
        std::string table;
        std::string field;
        std::string alias;

        [[nodiscard]] inline std::string full_name() const {
            return utils::formatFieldName(table, field);
        }
    };

    // Compile-time descriptor view and helpers
    struct FieldDescView {
        std::string_view table;
        std::string_view field;
        std::string_view alias;

        [[nodiscard]] inline std::string full_name() const {
            return utils::formatFieldName(std::string{table}, std::string{field});
        }
    };

    // Build compile-time descriptor for a member pointer and optional alias
    // Uses the reflection fixed_string already defined in storm.reflect
    // fully qualified to avoid introducing another fixed_string type
    // Example: make_field_desc_ct<&Type::member,
    // ::refl::detail::fixed_string{"alias"}>()

    template <auto MemberPtr, ::refl::detail::fixed_string Alias = ""> consteval FieldDescView make_field_desc_ct() {
        using ClassType = typename member_pointer_traits<decltype(MemberPtr)>::class_type;
        static_assert(refl::reflectable<ClassType>, "Class must be registered with REFL_DEFINE_TYPE");

        std::string_view field{};
        refl::reflect<ClassType>::for_each_member([&](auto member) {
            if (member.member_ptr == MemberPtr) {
                field = member.get_name();
            }
        });

        return {refl::reflect<ClassType>::get_struct_name(), field, std::string_view{Alias}};
    }

    // Owning wrapper when alias is known at compile time
    template <auto MemberPtr, ::refl::detail::fixed_string Alias = "">
    [[nodiscard]] inline FieldDesc make_field_desc() {
        constexpr auto v = make_field_desc_ct<MemberPtr, Alias>();
        return FieldDesc{std::string(v.table), std::string(v.field), std::string(v.alias)};
    }

    // Owning wrapper with runtime alias (table/field names at CT, alias at RT)
    template <auto MemberPtr> [[nodiscard]] inline FieldDesc make_field_desc(std::string alias) {
        constexpr auto v = make_field_desc_ct<MemberPtr>();
        return FieldDesc{std::string(v.table), std::string(v.field), std::move(alias)};
    }

} // namespace storm
