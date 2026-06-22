module;

// Compile-time UPSERT (ON CONFLICT) SQL grammar (#205): the consteval helpers
// that spell the conflict-target list and the `excluded.col` DO UPDATE SET
// clause, split out of InsertStatement to keep that class cohesive. Stateless
// and connection-free — every member derives purely from reflection over T.

#include <meta>

export module storm_orm_statements_upsert_grammar;

import std;

import storm_orm_statements_base;
import storm_orm_utilities;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    template <typename T> struct UpsertGrammar {
        using Base = BaseStatement<T>;

        // Append ", " before every element after the first.
        template <typename Buf> static consteval auto append_separator(Buf& buf, bool& first) -> void {
            if (!first) {
                buf.append(", ");
            }
            first = false;
        }

        // (col1, col2, ...) — conflict-target column list, FK "_id"-aware (#422).
        template <std::meta::info... Target> static consteval auto build_conflict_target() {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            result.append("(");
            bool first = true;
            (
                    [&] {
                        append_separator(result, first);
                        meta::append_column_name(result, Target);
                    }(),
                    ...
            );
            result.append(")");
            return result;
        }

        // Each SET target must be a non-static data member of T and not the PK.
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            return std::meta::is_nonstatic_data_member(Member) && Member != Base::primary_key_;
        }

        // True when `member` carries auto_update (#209) and is NOT in the explicit pack.
        template <std::meta::info... SetCols>
        static consteval auto is_unlisted_auto_update(std::meta::info member) -> bool {
            return meta::is_auto_update(member) && ((member != SetCols) && ...);
        }

        // "col=excluded.col, ..." for the explicit SetCols pack, then any
        // auto_update field of T not already listed, appended as "col=?" (bound
        // now() at execution). The column ORDER of the auto_update tail is the
        // canonical bind order used by the execute path.
        template <std::meta::info... SetCols> static consteval auto build_excluded_set_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;
            (
                    [&] {
                        append_separator(result, first);
                        meta::append_column_name(result, SetCols);
                        result.append("=excluded.");
                        meta::append_column_name(result, SetCols);
                    }(),
                    ...
            );
            for (const auto& member : Base::all_members_) {
                if (is_unlisted_auto_update<SetCols...>(member)) {
                    append_separator(result, first);
                    result.append(std::meta::identifier_of(member));
                    result.append("=?");
                }
            }
            return result;
        }
    };

} // namespace storm::orm::statements
