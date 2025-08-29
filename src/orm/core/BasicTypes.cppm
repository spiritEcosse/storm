module;

export module storm.basic_types;

// Storm modules
import storm.field_desc;
import storm.reflect;
import storm.type_traits;
import storm.core_types;

// Standard header units
import <string>;
import <optional>;
import <variant>;
import <string_view>;
import <cstdint>;

export namespace storm {

    // Compile-time enhanced types with reflection dependencies
    // Note: This module contains types that use reflection for compile-time features
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
        template <auto MemberPtr, bool Asc, auto Alias = ::refl::detail::fixed_string{""}>
        static consteval OrderTerm make(Collation coll = Collation::NONE) {
            constexpr auto v = CtField<MemberPtr, Alias>::view();
            return OrderTerm(FieldDescView{v.table, v.field, v.alias}, Asc, coll);
        }
    };

} // namespace storm
