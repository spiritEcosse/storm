module;

// Compile-time field-name SQL grammar (#434): builds the comma-separated column lists that
// INSERT and SELECT splice into their statements ("col1, col2, fk_id, ..."). Split out of
// BaseStatement so that class stays cohesive (cpp:S1448). Stateless — every member derives
// from the persisted-field reflection BaseStatement already computed (all_members_,
// primary_key_, field_count_), passed in as the Base template parameter.

#include <meta>

export module storm_orm_statements_field_names;

import std;

import storm_orm_field_attr;
import storm_orm_utilities;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    template <typename Base> struct FieldNameGrammar {
        // How many SQL columns member at all_members_[Index] maps to: N for a
        // composite-FK member (one column per target PK part, #504), 1 for
        // everything else (a plain field, or a single-column FK — byte-identical
        // to the pre-#504 shape). Index-templated (not a runtime-loop value) so the
        // FK's inner type can be named as a template argument to
        // fk_primary_key_count — a runtime loop variable cannot splice into a
        // template parameter.
        template <std::size_t Index> static consteval auto column_count_of() -> std::size_t {
            constexpr auto member = Base::all_members_[Index];
            if constexpr (storm::meta::is_fk_field(member)) {
                using FKType = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                return Base::template fk_primary_key_count<FKType>();
            } else {
                return 1;
            }
        }

        // Byte size of the column name(s) member at Index emits: the composite-aware
        // sibling of storm::meta::column_name_size, routed through fk_column_names_size
        // (Task 3) for an FK member — degenerates to column_name_size's exact output
        // for a single-column FK (the N == 1 branch there) and is untouched for a
        // non-FK member.
        template <std::size_t Index> static consteval auto column_names_size_of() -> std::size_t {
            constexpr auto member = Base::all_members_[Index];
            if constexpr (storm::meta::is_fk_field(member)) {
                using FKType           = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                constexpr auto targets = Base::template find_fk_primary_key_members<FKType>();
                return storm::meta::fk_column_names_size(member, targets, ", ");
            } else {
                return storm::meta::column_name_size(member);
            }
        }

        // Append the column name(s) member at Index emits into buf (comma-joined
        // already, for a composite-FK member — see append_fk_column_names).
        template <std::size_t Index, typename Buf> static consteval void append_column_names_of(Buf& buf) {
            constexpr auto member = Base::all_members_[Index];
            if constexpr (storm::meta::is_fk_field(member)) {
                using FKType           = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                constexpr auto targets = Base::template find_fk_primary_key_members<FKType>();
                storm::meta::append_fk_column_names(buf, member, targets, ", ");
            } else {
                storm::meta::append_column_name(buf, member);
            }
        }

        // Is all_members_[Index] the (single-column) PK skipped by INSERT? Mirrors
        // the pre-#504 SkipPrimaryKey test — a composite key has no auto-generation
        // mechanism, so nothing is skipped for it (#502).
        template <std::size_t Index> static consteval auto is_skipped_pk() -> bool {
            return !Base::has_composite_pk_ && Base::all_members_[Index] == Base::primary_key_;
        }

        // Shared iterator over data members, honouring SkipPrimaryKey, invoking
        // `body.template operator()<Index>(needs_comma)` once per emitted MEMBER (not
        // per column — the body decides internally how many columns/placeholders a
        // composite-FK member needs, via column_count_of<Index>()). Index-sequence
        // fold rather than a runtime `for` loop: naming the FK's inner type needs
        // Index as a template argument, which a runtime loop variable cannot supply.
        template <bool SkipPrimaryKey, typename Body, std::size_t... Is>
        static consteval auto for_each_field_name_impl(Body body, std::index_sequence<Is...> /*unused*/) -> void {
            bool first = true;
            (
                    [&] {
                        if constexpr (SkipPrimaryKey) {
                            if (is_skipped_pk<Is>()) {
                                return;
                            }
                        }
                        body.template operator()<Is>(!first);
                        first = false;
                    }(),
                    ...
            );
        }

        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            for_each_field_name_impl<SkipPrimaryKey>(body, std::make_index_sequence<Base::field_count_>{});
        }

        // Unified field name size calculation at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto calculate_field_names_size_impl() -> std::size_t {
            std::size_t size = 0;
            for_each_field_name<SkipPrimaryKey>([&]<std::size_t Index>(bool needs_comma) {
                if (needs_comma) {
                    size += 2; // ", "
                }
                size += column_names_size_of<Index>();
            });
            return size;
        }

        // Calculate size of all field names string at compile-time
        static consteval auto calculate_field_names_size() -> std::size_t {
            return calculate_field_names_size_impl<false>();
        }

        // Calculate size of non-PK field names string at compile-time
        static consteval auto calculate_non_pk_field_names_size() -> std::size_t {
            return calculate_field_names_size_impl<true>();
        }

        // Unified field name list builder at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto build_field_names_list_impl() {
            constexpr std::size_t size = calculate_field_names_size_impl<SkipPrimaryKey>() + 10;
            ConstexprString<size> result;
            for_each_field_name<SkipPrimaryKey>([&]<std::size_t Index>(bool needs_comma) {
                if (needs_comma) {
                    result.append(", ");
                }
                append_column_names_of<Index>(result);
            });
            return result;
        }

        // Build comma-separated list of all field names (for SELECT statements)
        // FK fields are mapped to their column names (User sender → sender_id)
        static consteval auto build_all_field_names_list() {
            return build_field_names_list_impl<false>();
        }

        // Build the INSERT column list: excludes a DB-generated single-column PK
        // (auto-increment); a composite key is caller data, so all fields (#502).
        static consteval auto build_non_pk_field_names_list() {
            return build_field_names_list_impl<true>();
        }

        // "?, ?, ..." placeholders for the SQL VALUES clause, one per bound COLUMN
        // (a DB-generated single-column PK is skipped; a composite key is not,
        // #502; a composite-FK member contributes one "?" per target PK part,
        // #504, matching bind_fk_parts's per-part bind loop). Shared by
        // InsertStatement's VALUES clause and UpsertGrammar's re-derived INSERT
        // prefix (#205).
        static consteval auto build_placeholders() {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            for_each_field_name<true>([&]<std::size_t Index>(bool needs_comma) {
                bool              comma = needs_comma;
                const std::size_t cols  = column_count_of<Index>();
                for (std::size_t c = 0; c < cols; ++c) {
                    if (comma) {
                        result.append(", ");
                    }
                    result.append("?");
                    comma = true;
                }
            });
            return result;
        }
    };

} // namespace storm::orm::statements
