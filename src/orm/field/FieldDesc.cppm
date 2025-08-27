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

    // Compile-time descriptor view and helpers
    struct FieldDescView {
        std::string_view table;
        std::string_view field;
        std::string_view alias;
    };

    template <auto MemberPtr, auto Alias = ::refl::detail::fixed_string{""}> consteval FieldDescView make_field_desc_ct() {
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

    // Compile-time free function to build the fully-qualified field name for a member pointer
    // Returns a fixed_string by value, safe to use in other consteval contexts
    template <auto MemberPtr>
    consteval auto full_field_name_ct() {
        constexpr auto v = make_field_desc_ct<MemberPtr>();
        return storm::utils::formatFieldName_ct(v.table, v.field); // returns utils::fixed_string<...>
    }

    // Fully compile-time field descriptor type (no runtime strings)
    // Usage: CtField<&T::member> or CtField<&T::member, "alias">
    template <auto MemberPtr, auto Alias = ::refl::detail::fixed_string{""}>
    struct CtField {
        static consteval FieldDescView view() {
            return make_field_desc_ct<MemberPtr, Alias>();
        }

        // Compile-time string generation for runtime construction
        static consteval auto table_str() {
            constexpr auto v = view();
            utils::fixed_string<v.table.size() + 1> result{};
            std::size_t pos = 0;
            for (char c : v.table) {
                result.data[pos++] = c;
            }
            result.data[pos] = '\0';
            return result;
        }

        static consteval auto field_str() {
            constexpr auto v = view();
            utils::fixed_string<v.field.size() + 1> result{};
            std::size_t pos = 0;
            for (char c : v.field) {
                result.data[pos++] = c;
            }
            result.data[pos] = '\0';
            return result;
        }

        static consteval auto alias_str() {
            constexpr auto v = view();
            utils::fixed_string<v.alias.size() + 1> result{};
            std::size_t pos = 0;
            for (char c : v.alias) {
                result.data[pos++] = c;
            }
            result.data[pos] = '\0';
            return result;
        }

        static consteval auto full_name() {
            constexpr auto v = view();
            return storm::utils::formatFieldName_ct(v.table, v.field);
        }

        static consteval std::string_view alias() { return std::string_view{Alias}; }
    };

} // namespace storm
