module;

export module storm.join_utils;

// Standard header units
import <string>;
import <string_view>;
import <type_traits>;

// Storm modules
import storm.reflect;     // for refl::reflect
import storm.utils;       // for formatFieldName, to_lower_ct, fixed_string
import storm.type_traits; // for member_pointer_traits

export namespace storm {

    enum class JoinType { Inner, Left };

    // Compile-time join information structure
    template <typename Lhs, typename Rhs, auto MemberPtr, JoinType Type> struct CtJoinInfo {
        static constexpr JoinType join_type = Type;
        using LeftType                      = Lhs;
        using RightType                     = Rhs;
        static constexpr auto member_ptr    = MemberPtr;

        // Compile-time builder for the full JOIN clause
        static consteval auto build_clause() {
            constexpr auto lhs_table    = refl::reflect<Lhs>::get_struct_name();
            constexpr auto rhs_table    = refl::reflect<Rhs>::get_struct_name();
            constexpr auto rhs_desc     = make_field_desc_ct<MemberPtr>();
            constexpr auto rhs_field    = rhs_desc.field;
            constexpr auto rhs_table_lc = storm::utils::to_lower_ct(rhs_table);

            // Create left field name using factory function
            constexpr auto lhs_field = make_fixed_string(rhs_table_lc.view(), "_", rhs_field);

            // Create quoted field names
            const auto lhs_full_q = storm::utils::formatFieldName(lhs_table, lhs_field.view());
            const auto rhs_full_q = storm::utils::formatFieldName(rhs_table, rhs_field);

            // Build complete clause
            constexpr auto join_kw = (Type == JoinType::Inner) ? "INNER JOIN" : "LEFT JOIN";
            return make_fixed_string(join_kw, " ", rhs_table, " ON ", lhs_full_q.view(), " = ", rhs_full_q.view());
        }

        inline static constexpr auto clause_ = build_clause();

        // View to the compile-time buffer (static storage duration)
        [[nodiscard]] static consteval std::string_view view() {
            return clause_.view();
        }
    };

    // Compile-time version that returns join information structure
    template <typename Lhs, typename Rhs, auto MemberPtr, JoinType Type>
    [[nodiscard]] consteval auto make_join_clause() {
        static_assert(std::is_member_pointer_v<decltype(MemberPtr)>, "MemberPtr must be a member pointer");
        using MPClass = typename member_pointer_traits<decltype(MemberPtr)>::class_type;
        static_assert(std::is_same_v<MPClass, Rhs>, "MemberPtr must be a member of Rhs");

        return CtJoinInfo<Lhs, Rhs, MemberPtr, Type>{};
    }

} // namespace storm
