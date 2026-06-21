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
        // Shared iterator over data members, honouring SkipPrimaryKey, invoking
        // `body(i, needs_comma)` per emitted field. The size-calculator and the
        // list-builder used to spell out this loop independently. The FK "_id" suffix
        // is derived inside storm::meta::append_column_name/column_name_size (#422).
        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (Base::all_members_[i] == Base::primary_key_) {
                        continue;
                    }
                }
                body(i, !first);
                first = false;
            }
        }

        // Unified field name size calculation at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto calculate_field_names_size_impl() -> std::size_t {
            std::size_t size = 0;
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool needs_comma) {
                if (needs_comma) {
                    size += 2; // ", "
                }
                size += storm::meta::column_name_size(Base::all_members_[i]);
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
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool needs_comma) {
                if (needs_comma) {
                    result.append(", ");
                }
                storm::meta::append_column_name(result, Base::all_members_[i]);
            });
            return result;
        }

        // Build comma-separated list of all field names (for SELECT statements)
        // FK fields are mapped to their column names (User sender → sender_id)
        static consteval auto build_all_field_names_list() {
            return build_field_names_list_impl<false>();
        }

        // Build comma-separated list of NON-PRIMARY KEY fields (for INSERT statements)
        // Excludes primary key to allow auto-increment
        static consteval auto build_non_pk_field_names_list() {
            return build_field_names_list_impl<true>();
        }
    };

} // namespace storm::orm::statements
