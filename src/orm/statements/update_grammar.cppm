module;

// Compile-time UPDATE SQL grammar (#434): the consteval "how UPDATE SQL is spelled" helpers,
// split out of UpdateStatement so that class stays cohesive and under the method threshold
// (cpp:S1448). Stateless and connection-free — every member derives purely from reflection
// over the model type T (via Base = BaseStatement<T>).

#include <meta>

export module storm_orm_statements_update_grammar;

import std;

import storm_orm_statements_base;
import storm_orm_utilities;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    template <typename T> struct UpdateGrammar {
        using Base = BaseStatement<T>;

        // Helper to build field assignments string for UPDATE SQL
        static consteval auto build_field_assignments() {
            // Get all members directly
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
            auto pk      = Base::primary_key_;

            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;

            for (const auto& member : members) {
                if (member != pk) {
                    if (!first) {
                        result.append(", ");
                    }
                    // Check if this is a FK field
                    if (meta::is_fk_field(member)) {
                        // FK field: use field_name_id
                        result.append(std::meta::identifier_of(member));
                        result.append("_id=?");
                    } else {
                        result.append(std::meta::identifier_of(member));
                        result.append("=?");
                    }
                    first = false;
                }
            }

            return result;
        }

        static constexpr auto field_assignments_ = build_field_assignments();

        // --- Conditional bulk UPDATE (#403): SET-clause built from explicit member NTTPs ---

        // Index of a member info within Base::all_members_ (compile-time).
        static consteval auto index_of_member(std::meta::info member) -> std::size_t {
            for (std::size_t i = 0; i < Base::all_members_.size(); ++i) {
                if (Base::all_members_[i] == member) {
                    return i;
                }
            }
            std::unreachable(); // guarded by is_settable_member() at the call site
        }

        // Each SET target must be a non-static data member of T and not the primary key.
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            return std::meta::is_nonstatic_data_member(Member) && Member != Base::primary_key_;
        }

        // Append "<name>=?" (or "<name>_id=?" for FK fields) for one member.
        template <typename Buf> static consteval auto append_one_assignment(Buf& buf, std::meta::info member) -> void {
            buf.append(std::meta::identifier_of(member));
            if (meta::is_fk_field(member)) {
                buf.append("_id=?");
            } else {
                buf.append("=?");
            }
        }

        // True when `member` carries the auto_update timestamp annotation (#209). Mirrors
        // BaseStatement::is_auto_update_field, inlined here because that member is protected
        // and UpdateGrammar does not inherit from BaseStatement.
        static consteval auto is_auto_update_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::auto_update;
        }

        // Is `member` an auto_update field NOT already present in the explicit pack?
        template <std::meta::info... Members>
        static consteval auto is_unlisted_auto_update(std::meta::info member) -> bool {
            return is_auto_update_field(member) && ((member != Members) && ...);
        }

        // Build the SET clause for the explicit Members... pack, then append any
        // auto_update field of T that the caller did not list. The column ORDER
        // here is the canonical bind order used by bind_conditional_set().
        template <std::meta::info... Members> static consteval auto build_conditional_set_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;
            // (1) explicit members, in the given order
            (
                    [&] {
                        if (!first) {
                            result.append(", ");
                        }
                        append_one_assignment(result, Members);
                        first = false;
                    }(),
                    ...
            );
            // (2) auto_update fields not already listed (stamped now() at bind time)
            for (const auto& member : Base::all_members_) {
                if (is_unlisted_auto_update<Members...>(member)) {
                    if (!first) {
                        result.append(", ");
                    }
                    result.append(std::meta::identifier_of(member));
                    result.append("=?");
                    first = false;
                }
            }
            return result;
        }

        // Compile-time UPDATE SQL size calculation
        static consteval auto calculate_update_sql_size() -> std::size_t {
            using utilities::sql_len::SET;
            using utilities::sql_len::UPDATE;
            using utilities::sql_len::WHERE;
            std::size_t size = 0;
            size += UPDATE; // "UPDATE "
            size += Base::table_name_.size();
            size += SET; // " SET "
            size += field_assignments_.len;
            size += WHERE; // " WHERE "
            size += Base::pk_name_.size();
            size += 4; // " = ?"
            size += 1; // null terminator
            return size;
        }

        // Build UPDATE SQL at compile-time using ConstexprString
        static consteval auto build_update_sql_array() {
            constexpr std::size_t     sql_size = calculate_update_sql_size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<sql_size> result;

            result.append("UPDATE ");
            result.append(Base::table_name_);
            result.append(" SET ");
            result.append(std::string_view(field_assignments_.data.data(), field_assignments_.len));
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" = ?");

            return result;
        }

        // Pre-computed UPDATE SQL generated at compile-time
        static constexpr auto           update_sql_array  = build_update_sql_array();
        static inline const std::string update_sql_string = std::string(update_sql_array);
    };

} // namespace storm::orm::statements
