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

    namespace meta {
        using storm::meta::FieldAttr;
    }

    template <typename Base> struct FieldNameGrammar {
        // Shared iterator over data members, honouring SkipPrimaryKey, invoking
        // `body(i, is_fk, name)` per emitted field. The size-calculator and the
        // list-builder used to spell out this loop independently.
        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (Base::all_members_[i] == Base::primary_key_) {
                        continue;
                    }
                }
                auto       field_attr = std::meta::annotation_of_type<meta::FieldAttr>(Base::all_members_[i]);
                bool const is_fk      = field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
                body(i, is_fk, !first);
                first = false;
            }
        }

        // Unified field name size calculation at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto calculate_field_names_size_impl() -> std::size_t {
            std::size_t size = 0;
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool is_fk, bool needs_comma) {
                if (needs_comma) {
                    size += 2; // ", "
                }
                size += std::meta::identifier_of(Base::all_members_[i]).size();
                if (is_fk) {
                    size += 3; // "_id"
                }
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
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool is_fk, bool needs_comma) {
                if (needs_comma) {
                    result.append(", ");
                }
                result.append(std::meta::identifier_of(Base::all_members_[i]));
                if (is_fk) {
                    result.append("_id");
                }
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
