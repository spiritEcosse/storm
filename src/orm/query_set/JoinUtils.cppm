module;

export module storm.join_utils;

// Standard header units
import <string>;
import <string_view>;
import <type_traits>;

// Storm modules
import storm.reflect;     // for refl::reflect
import storm.field_desc;  // for make_field_desc_ct
import storm.utils;       // for formatFieldName_ct, to_lower_ct, fixed_string
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
            // Table names
            constexpr auto lhs_table = refl::reflect<Lhs>::get_struct_name();
            constexpr auto rhs_table = refl::reflect<Rhs>::get_struct_name();

            // Right side field (from MemberPtr)
            constexpr auto rhs_desc  = make_field_desc_ct<MemberPtr>();
            constexpr auto rhs_field = rhs_desc.field;

            // Left side field by convention: <lower(rhs_table)>_<rhs_field>
            constexpr auto rhs_table_lc = storm::utils::to_lower_ct(rhs_table);

            // We'll compose field names manually to avoid dynamic formatting for lhs_field
            // Compose lhs_field into buffer, then quote
            storm::utils::fixed_string<512> lhs_field_buf{};
            std::size_t                     pos = 0;
            for (char c : rhs_table_lc.view()) {
                if (pos + 1 >= sizeof(lhs_field_buf.data)) break;
                lhs_field_buf.data[pos++] = c;
            }
            if (pos + 1 < sizeof(lhs_field_buf.data)) lhs_field_buf.data[pos++] = '_';
            for (char c : rhs_field) {
                if (pos + 1 >= sizeof(lhs_field_buf.data)) break;
                lhs_field_buf.data[pos++] = c;
            }
            lhs_field_buf.data[pos] = '\0';

            // Quoted sides
            const auto lhs_full_q = storm::utils::formatFieldName_ct(lhs_table, lhs_field_buf.view());
            const auto rhs_full_q = storm::utils::formatFieldName_ct(rhs_table, rhs_field);

            // JOIN keyword
            constexpr std::string_view join_kw = (Type == JoinType::Inner) ? std::string_view{"INNER JOIN"}
                                                                            : std::string_view{"LEFT JOIN"};

            // Assemble final clause into fixed buffer
            storm::utils::fixed_string<512> clause{};
            std::size_t                     cpos = 0;
            auto append_sv = [&](std::string_view sv) {
                for (char ch : sv) {
                    if (cpos + 1 >= sizeof(clause.data)) break;
                    clause.data[cpos++] = ch;
                }
            };

            auto append_char = [&](char ch) {
                if (cpos + 1 < sizeof(clause.data)) clause.data[cpos++] = ch;
            };

            append_sv(join_kw);
            append_char(' ');
            append_sv(rhs_table);
            append_sv(" ON ");
            append_sv(lhs_full_q.view());
            append_sv(" = ");
            append_sv(rhs_full_q.view());

            clause.data[cpos] = '\0';
            return clause;
        }

        inline static constexpr auto clause_ = build_clause();

        // View to the compile-time buffer (static storage duration)
        [[nodiscard]] static consteval std::string_view view() {
            return clause_.view();
        }
    };

    // Compile-time version that returns join information structure
    template <typename Lhs, typename Rhs, auto MemberPtr, JoinType Type>
    [[nodiscard]] consteval auto make_join_clause_ct() {
        static_assert(std::is_member_pointer_v<decltype(MemberPtr)>, "MemberPtr must be a member pointer");
        using MPClass = typename member_pointer_traits<decltype(MemberPtr)>::class_type;
        static_assert(std::is_same_v<MPClass, Rhs>, "MemberPtr must be a member of Rhs");

        return CtJoinInfo<Lhs, Rhs, MemberPtr, Type>{};
    }

} // namespace storm
