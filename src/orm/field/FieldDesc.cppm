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
import storm.core_types;

export namespace storm {

    // Compile-time descriptor view and helpers
    struct FieldDescView {
        std::string_view table;
        std::string_view field;
        std::string_view alias;
    };

    // Runtime-friendly descriptor that keeps compile-time metadata but exposes
    // convenient members and a runtime full_name() string. Templated on MemberPtr
    // so we can compute the qualified name at compile time via reflection.
    template <auto MemberPtr> struct FieldDescCT {
        static consteval FieldDescView view() { return make_field_desc_ct<MemberPtr>(); }

        // Expose views for existing code paths (e.g., alias generation)
        std::string_view table = view().table;
        std::string_view field = view().field;
        std::string_view alias = view().alias;

        // Build a runtime string from the compile-time fixed_string
        std::string full_name() const {
            constexpr auto fs = refl::full_field_name<MemberPtr>();
            return std::string{fs};
        }
    };

    // Factory function preserved for existing call sites in QuerySet
    template <auto MemberPtr> constexpr auto make_field_desc() {
        return FieldDescCT<MemberPtr>{};
    }

    template <auto MemberPtr, auto Alias = utils::fixed_string{""}>
    consteval FieldDescView make_field_desc_ct() {
        using ClassType = typename member_pointer_traits<decltype(MemberPtr)>::class_type;

        constexpr std::string_view field = []() consteval {
            std::string_view result{};
            refl::reflect<ClassType>::for_each_member([&](auto member) consteval {
                if (member.member_ptr == MemberPtr) {
                    result = member.get_name();
                }
            });
            return result;
        }();

        return {refl::reflect<ClassType>::get_struct_name(), field, std::string_view{Alias}};
    }

    // Compile-time free function to build the fully-qualified field name for a member pointer
    // Returns a fixed_string by value, safe to use in other consteval contexts
    template <auto MemberPtr> consteval auto full_field_name_ct() {
        constexpr auto v = make_field_desc_ct<MemberPtr>();
        return utils::formatFieldName(v.table, v.field); // returns utils::fixed_string<...>
    }

    // Fully compile-time field descriptor type (no runtime strings)
    // Usage: CtField<&T::member> or CtField<&T::member, "alias">
    template <auto MemberPtr, auto Alias = utils::fixed_string{""}> struct CtField {
        static consteval FieldDescView view() {
            return make_field_desc_ct<MemberPtr, Alias>();
        }

        static consteval auto full_name() {
            // Delegate to reflection utility for fully-qualified name
            return refl::full_field_name<MemberPtr>();
        }

        static consteval std::string_view alias() {
            return std::string_view{Alias};
        }
    };

    struct OrderTerm {
        std::string_view table_name;
        std::string_view field_name;
        bool             ascending;
        Collation        collation = Collation::NONE;

        // Default constructor
        OrderTerm() = default;

        // Compile-time capable constructor from FieldDescView
        constexpr OrderTerm(FieldDescView desc, bool asc, Collation coll = Collation::NONE)
            : table_name(desc.table), field_name(desc.field), ascending(asc), collation(coll) {}

        // Fully compile-time factory using CtField metadata
        template <auto MemberPtr, bool Asc, auto Alias = utils::fixed_string{""}>
        static consteval OrderTerm make(Collation coll = Collation::NONE) {
            constexpr auto v = CtField<MemberPtr, Alias>::view();
            return OrderTerm(FieldDescView{v.table, v.field, v.alias}, Asc, coll);
        }
    };

} // namespace storm
