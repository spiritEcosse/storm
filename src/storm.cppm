module;

#include <meta>

export module storm;

// Import and re-export all Storm ORM modules
export import storm_orm_utilities;
export import storm_db_concept;
export import storm_db_sqlite;
export import storm_db_postgresql;
export import storm_orm_statements_base;
export import storm_orm_statements_insert;
export import storm_orm_statements_select;
export import storm_orm_statements_join;
export import storm_orm_statements_setop;
export import storm_orm_statements_orderby;
export import storm_orm_where;
export import storm_orm_queryset;
export import storm_orm_indexes;
export import storm_orm_schema;
import <meta>;
import <string>;
import <vector>;
import <expected>;

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

        // Find primary key member — T must satisfy ModelWithPrimaryKey<T>
        template <typename T>
            requires orm::statements::ModelWithPrimaryKey<T>
        consteval auto find_primary_key() -> std::meta::info {
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (has_primary_attr(member)) {
                    return member;
                }
            }
            std::unreachable(); // never reached: ModelWithPrimaryKey<T> guarantees a primary key exists
        }
    } // namespace meta

    // Returns the compile-time generated CREATE TABLE SQL for model T (SQLite dialect, lazy-cached).
    template <typename T> auto create_table_sql() -> const std::string& {
        return orm::schema::SchemaStatement<T>::create_table_sql();
    }

    // Returns the pre-computed CREATE INDEX SQL statements for model T.
    template <typename T> auto create_index_sql() -> const std::vector<std::string>& {
        return orm::schema::SchemaStatement<T>::create_index_sql();
    }
} // namespace storm
