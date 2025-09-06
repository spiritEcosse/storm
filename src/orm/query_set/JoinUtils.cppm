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

    // Type-erased join wrapper that captures join metadata at compile time
    struct JoinWrapper {
        std::string_view sql_clause{}; // Pre-computed SQL
        std::string_view lhs_table{};  // Semantic info
        std::string_view rhs_table{};
        std::string_view lhs_field{};
        std::string_view rhs_field{};
        JoinType         type{};

        template <typename Lhs, typename Rhs, auto MemberPtr, JoinType Type> static JoinWrapper create() {
            static constexpr auto join_info = make_join_clause<Lhs, Rhs, MemberPtr, Type>();

            // Extract individual components at compile time
            static constexpr auto lhs_table_name = refl::reflect<Lhs>::get_struct_name();
            static constexpr auto rhs_table_name = refl::reflect<Rhs>::get_struct_name();
            static constexpr auto rhs_desc       = make_field_desc_ct<MemberPtr>();
            static constexpr auto rhs_field_name = rhs_desc.field;
            static constexpr auto rhs_table_lc   = storm::utils::to_lower_ct(rhs_table_name);
            static constexpr auto lhs_field_name = make_fixed_string(rhs_table_lc.view(), "_", rhs_field_name);

            // Store references to static compile-time strings
            return JoinWrapper{
                    .sql_clause = join_info.view(),
                    .lhs_table  = lhs_table_name,
                    .rhs_table  = rhs_table_name,
                    .lhs_field  = lhs_field_name.view(),
                    .rhs_field  = rhs_field_name,
                    .type       = Type
            };
        }

        // Zero-allocation access to the full SQL clause
        [[nodiscard]] std::string_view view() const noexcept {
            return sql_clause;
        }

        // Access to semantic components for future flexibility
        [[nodiscard]] std::string_view get_lhs_table() const noexcept {
            return lhs_table;
        }
        [[nodiscard]] std::string_view get_rhs_table() const noexcept {
            return rhs_table;
        }
        [[nodiscard]] std::string_view get_lhs_field() const noexcept {
            return lhs_field;
        }
        [[nodiscard]] std::string_view get_rhs_field() const noexcept {
            return rhs_field;
        }
        [[nodiscard]] JoinType get_type() const noexcept {
            return type;
        }
    };

} // namespace storm
