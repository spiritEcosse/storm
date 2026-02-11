module;

#include <meta>

export module storm;

// Import and re-export all Storm modules
export import storm_db_concept;
export import storm_db_sqlite;
export import storm_db_postgresql;
export import storm_orm_statements_base;
export import storm_orm_statements_select;
export import storm_orm_statements_join;
export import storm_orm_statements_orderby;
export import storm_orm_where;
export import storm_orm_queryset;
import <meta>;

export namespace storm {
    // Storm ORM functionality with reflection support
    constexpr auto version = "0.1.0";

    // Meta functionality for ORM field attributes and reflection
    namespace meta {
        // Note: FieldAttr enum is defined in storm_orm_statements_base module
        // and re-exported here through the import chain
        using FieldAttr = orm::statements::meta::FieldAttr;

        // Check if member has primary attribute
        consteval auto has_primary_attr(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == FieldAttr::primary;
        }

        // Find primary key member with compile-time error if not found
        consteval auto find_primary_key(std::meta::info type) -> std::meta::info {
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
                if (has_primary_attr(member)) {
                    return member;
                }
            }

            // Compile-time error using throw in consteval context
            throw "Model must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
        }
    } // namespace meta
} // namespace storm
