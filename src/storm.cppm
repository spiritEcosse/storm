module;

#include <meta>

export module storm;

import std;

// Import and re-export all Storm ORM modules
export import storm_orm_field_attr;
export import storm_orm_generator;
export import storm_orm_utilities;
export import storm_db_concept;
export import storm_db_sqlite;
export import storm_db_postgresql;
export import storm_db_pool;
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

export namespace storm {
    // Storm ORM functionality with reflection support
    constexpr auto version = "0.1.0";

    // Re-export UUID type for convenient access as storm::UUID
    using UUID = orm::utilities::UUID;

    // Meta functionality for ORM field attributes and reflection.
    // FieldAttr + is_primary_attr come from the storm_orm_field_attr leaf module
    // (re-exported above), which already declares them in storm::meta (#387).
    namespace meta {
        // Many-to-many annotations (#203): many_to_many (auto junction table) and
        // many_to_many_through<Through> (explicit junction model). These re-exports
        // are consumed by user model declarations, never inside this module.
        using orm::statements::meta::many_to_many;         // NOLINT(misc-unused-using-decls)
        using orm::statements::meta::many_to_many_through; // NOLINT(misc-unused-using-decls)
        using orm::statements::meta::ManyToMany;           // NOLINT(misc-unused-using-decls)

        // Check if member has primary attribute
        consteval auto has_primary_attr(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<FieldAttr>(member);
            return field_attr.has_value() && is_primary_attr(field_attr.value());
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

    // Returns the compile-time generated CREATE TABLE SQL for model T and dialect D.
    template <typename T, orm::schema::Dialect D = orm::schema::Dialect::SQLite>
    auto create_table_sql() -> const std::string& {
        return orm::schema::SchemaStatement<T>::template create_table_sql<D>();
    }

    // Returns the pre-computed CREATE INDEX SQL statements for model T.
    template <typename T> auto create_index_sql() -> const std::vector<std::string>& {
        return orm::schema::SchemaStatement<T>::create_index_sql();
    }
} // namespace storm
