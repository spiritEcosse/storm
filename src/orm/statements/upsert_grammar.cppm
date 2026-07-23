module;

// Compile-time UPSERT (ON CONFLICT) SQL grammar (#205): the consteval helpers
// that spell the conflict-target list and the `excluded.col` DO UPDATE SET
// clause, split out of InsertStatement to keep that class cohesive. Stateless
// and connection-free — every member derives purely from reflection over T.

#include <meta>

export module storm_orm_statements_upsert_grammar;

import std;

import storm_orm_statements_base;
import storm_orm_statements_field_names;
import storm_orm_utilities;
import storm_orm_indexes;
import storm_orm_field_attr;

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

        // Each SET target must be a non-static data member of T, not ANY part of the PK,
        // and not a relation container (#486) — m2m / reverse_fk members are not
        // persisted columns, so "col=excluded.col" would reference a missing column.
        //
        // The PK test is is_pk_member, not `Member != primary_key_` (#501): the latter
        // excludes only the FIRST part, leaving every other part of a composite key a
        // legal DO UPDATE target. Unchanged for a single-PK model. (ON CONFLICT targeting
        // a composite key is #503; keeping the key parts out of the SET list is the half
        // that belongs here.)
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            return std::meta::is_nonstatic_data_member(Member) && !Base::is_pk_member(Member) &&
                   !meta::is_relation_field(Member);
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

        // INSERT INTO <table> (<non-pk cols>) VALUES (<placeholders>) — the same
        // prefix InsertStatement builds, re-derived here from reflection so this
        // module stays connection-free (it has no ConnType to reach InsertStatement
        // through). Placeholders come from FieldNameGrammar<Base>::build_placeholders(),
        // the single shared source of that text (#205).
        static consteval auto build_insert_prefix() {
            ConstexprString<utilities::buffer_size::SQL_LARGE> result;
            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(FieldNameGrammar<Base>::build_non_pk_field_names_list());
            result.append(") VALUES (");
            result.append(FieldNameGrammar<Base>::build_placeholders());
            result.append(")");
            return result;
        }

        // Full "INSERT ... ON CONFLICT (...) DO NOTHING [RETURNING <pk>]" statement.
        // A composite key is never DB-generated (#502/#503): every part is caller
        // data, so there is nothing for RETURNING to echo back — the clause is
        // omitted entirely on a composite model. Single-PK text is unchanged.
        template <std::meta::info... Target> static auto nothing_sql() -> const std::string& {
            static const std::string sql = [] {
                std::string out(build_insert_prefix());
                out += " ON CONFLICT ";
                out += std::string(build_conflict_target<Target...>());
                out += " DO NOTHING";
                if constexpr (!Base::has_composite_pk_) {
                    out += " RETURNING ";
                    out += std::string(Base::pk_name_);
                }
                return out;
            }();
            return sql;
        }

        // Full "INSERT ... ON CONFLICT (...) DO UPDATE SET <set_clause> [RETURNING <pk>]"
        // statement. The SET clause is passed in as a precomputed value (built by
        // build_excluded_set_clause<SetCols...>()) because Target... and SetCols...
        // cannot share one template parameter list. RETURNING is omitted on a
        // composite model — same reasoning as nothing_sql() above.
        template <std::meta::info... Target, std::size_t N>
        static auto update_sql(const ConstexprString<N>& set_clause) -> std::string {
            std::string out(build_insert_prefix());
            out += " ON CONFLICT ";
            out += std::string(build_conflict_target<Target...>());
            out += " DO UPDATE SET ";
            out += std::string(set_clause);
            if constexpr (!Base::has_composite_pk_) {
                out += " RETURNING ";
                out += std::string(Base::pk_name_);
            }
            return out;
        }
    };

    // True if the two field lists are identical element-wise. Conflict-target
    // columns must be supplied in the same declared order as the matching
    // UniqueIndex. This is a v1 simplification; set-comparison (order-insensitive
    // matching) can be added later if needed.
    consteval auto fields_equal_ordered(const auto& lhs, const auto& rhs) -> bool {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i] != rhs[i]) {
                return false;
            }
        }
        return true;
    }

    // True if the Target... column set exactly matches some UniqueIndex<...> in Indexes<T>.
    template <typename T, std::meta::info... Target> consteval auto target_matches_unique_index() -> bool {
        constexpr std::array targets{Target...};
        bool                 matched = false;
        [&]<typename... Idx>(std::tuple<Idx...>*) {
            (
                    [&] {
                        if constexpr (Idx::unique) {
                            if (fields_equal_ordered(Idx::fields, targets)) {
                                matched = true;
                            }
                        }
                    }(),
                    ...
            );
        }(static_cast<storm::indexes_t<T>*>(nullptr));
        return matched;
    }

    // True if T has a COMPOSITE primary key and the Target... column set exactly
    // matches it, in declaration order (#503). A composite PK is a unique
    // constraint over its N columns, so the full set is a valid conflict target —
    // but a STRICT SUBSET is not unique on its own (order-sensitive match, same as
    // target_matches_unique_index, so a partial or reordered target is correctly
    // rejected). Gated on has_composite_pk_ so a single-column PK is NOT matched
    // here: INSERT omits it from the statement entirely, so it can never conflict
    // — that exclusion predates this issue and must stay unchanged.
    template <typename T, std::meta::info... Target> consteval auto target_matches_primary_key() -> bool {
        if constexpr (!BaseStatement<T>::has_composite_pk_) {
            return false;
        } else {
            constexpr std::array targets{Target...};
            return fields_equal_ordered(BaseStatement<T>::primary_key_members_, targets);
        }
    }

    // A valid conflict target: every Target is a data member of T, AND either a
    // single storm::unique field, a matching UniqueIndex<...>, or the full
    // composite-PK column set (#503). INSERT omits a single-column PK, so it alone
    // cannot form a useful conflict target — that case is unaffected here.
    template <typename T, std::meta::info... Target>
    concept ConflictTargetUnique =
            ((std::meta::is_nonstatic_data_member(Target)) && ...) &&
            ((sizeof...(Target) == 1 && (storm::meta::is_unique(Target) && ...)) ||
             target_matches_unique_index<T, Target...>() || target_matches_primary_key<T, Target...>());

    // A valid SET target: a non-static data member of T that is not the PK.
    template <typename T, std::meta::info... SetCols>
    concept UpsertSettable = (UpsertGrammar<T>::template is_settable_member<SetCols>() && ...);

} // namespace storm::orm::statements
