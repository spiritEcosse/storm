module;

export module storm.join_utils;

// Standard header units
import <string>;
import <string_view>;
import <format>;
import <type_traits>;

// Storm modules
import storm.reflect;     // for refl::reflect
import storm.field_desc;  // for make_field_desc_ct
import storm.utils;       // for formatFieldName, to_lower
import storm.type_traits; // for member_pointer_traits

export namespace storm {

    enum class JoinType { Inner, Left };

    template <typename Lhs, typename Rhs, auto MemberPtr>
    [[nodiscard]] inline std::string make_join_clause(JoinType type) {
        static_assert(std::is_member_pointer_v<decltype(MemberPtr)>, "MemberPtr must be a member pointer");
        using MPClass = typename member_pointer_traits<decltype(MemberPtr)>::class_type;
        static_assert(std::is_same_v<MPClass, Rhs>, "MemberPtr must be a member of Rhs");

        const std::string lhs_table{std::string{refl::reflect<Lhs>::get_struct_name()}};
        const std::string rhs_table{std::string{refl::reflect<Rhs>::get_struct_name()}};

        // Right side (joined table field) from MemberPtr
        constexpr auto    rhs_desc = make_field_desc_ct<MemberPtr>();
        const std::string rhs_field{rhs_desc.field};

        // Left side (current table) inferred by convention: <rhs_table>_<rhs_field>
        const std::string lhs_field = std::format("{}_{}", storm::utils::to_lower(std::string{rhs_table}), rhs_field);

        const std::string lhs_full = storm::utils::formatFieldName(lhs_table, lhs_field);
        const std::string rhs_full = storm::utils::formatFieldName(rhs_table, rhs_field);

        const char* join_kw = (type == JoinType::Inner) ? "INNER JOIN" : "LEFT JOIN";
        return std::format("{} {} ON {} = {}", join_kw, rhs_table, lhs_full, rhs_full);
    }

} // namespace storm
