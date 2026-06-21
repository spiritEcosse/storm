module;

#include <meta>

export module storm;

import std;

// Import and re-export all Storm ORM modules
export import storm_orm_field_attr;
export import storm_orm_generator;
export import storm_orm_utilities;
export import storm_orm_transaction;
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

    // Public transaction API (#415). TransactionGuard is the RAII transaction type
    // (BEGIN on begin(), explicit commit(), auto-ROLLBACK on scope exit). storm::begin
    // is the thin user-facing factory. Nested batch ops cooperate automatically: a
    // begin() on an already-open connection returns a passive guard (no inner BEGIN),
    // resolving the nested-BEGIN bug (#9).
    template <typename ConnType> using TransactionGuard = orm::utilities::TransactionGuard<ConnType>;

    template <typename ConnType>
    [[nodiscard]] auto begin(std::shared_ptr<ConnType> conn)
            -> std::expected<TransactionGuard<ConnType>, typename ConnType::Error> {
        return orm::utilities::TransactionGuard<ConnType>::begin(std::move(conn));
    }

    // Scope helper (#415): runs `body(txn)` inside a transaction. `body` returns
    // std::expected<T, Error>; on a value the scope COMMITs and forwards it, on a
    // std::unexpected (or a throw) the guard ROLLBACKs and the error propagates.
    // A thin convenience layer over storm::begin — same cooperative nesting (#9).
    template <typename ConnType, typename Body>
    [[nodiscard]] auto transaction(std::shared_ptr<ConnType> conn, Body&& body)
            -> std::invoke_result_t<Body&, TransactionGuard<ConnType>&> {
        using Ret = std::invoke_result_t<Body&, TransactionGuard<ConnType>&>;

        auto guard = orm::utilities::TransactionGuard<ConnType>::begin(std::move(conn));
        if (!guard) {
            return Ret(std::unexpect, guard.error());
        }

        Ret result = std::forward<Body>(body)(*guard);
        if (!result) {
            return result; // guard destructor ROLLBACKs on the way out.
        }

        if (auto commit_result = guard->commit(); !commit_result) {
            return Ret(std::unexpect, commit_result.error());
        }
        return result;
    }

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

        // Reverse-FK annotation (#398): reverse_fk<^^Owner::fk> on a container member
        // names the FK of another model that points back at this model, declaring the
        // eager-load destination ("all Persons, each with the Tasks that point at them").
        using orm::statements::meta::reverse_fk; // NOLINT(misc-unused-using-decls)
        using orm::statements::meta::ReverseFk;  // NOLINT(misc-unused-using-decls)

        // Foreign-key annotation (#431): fk<> (bare, RESTRICT) or fk<RefAction::...> to
        // carry the ON DELETE policy. The junction policy for an auto-junction m2m field
        // is the RefAction template arg of many_to_many<RefAction::...> above. RefAction
        // itself is re-exported from the storm_orm_field_attr leaf module above.
        using orm::statements::meta::fk; // NOLINT(misc-unused-using-decls)
        using orm::statements::meta::Fk; // NOLINT(misc-unused-using-decls)

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

    // Convenience re-exports of the user-facing annotation names into the top-level
    // `storm` namespace (#442). Model declarations can spell `storm::FieldAttr::primary`,
    // `storm::fk<>`, `storm::many_to_many<>` etc. instead of the longer `storm::meta::...`.
    // Purely additive — the `storm::meta::` spelling keeps working. Internal reflection
    // helpers (is_fk_field, find_primary_key, …) stay in storm::meta, not re-exported here.
    using meta::FieldAttr;            // NOLINT(misc-unused-using-decls)
    using meta::fk;                   // NOLINT(misc-unused-using-decls)
    using meta::Fk;                   // NOLINT(misc-unused-using-decls)
    using meta::many_to_many;         // NOLINT(misc-unused-using-decls)
    using meta::many_to_many_through; // NOLINT(misc-unused-using-decls)
    using meta::ManyToMany;           // NOLINT(misc-unused-using-decls)
    using meta::RefAction;            // NOLINT(misc-unused-using-decls)
    using meta::reverse_fk;           // NOLINT(misc-unused-using-decls)
    using meta::ReverseFk;            // NOLINT(misc-unused-using-decls)

    // Returns the compile-time generated CREATE TABLE SQL for model T and dialect D.
    template <typename T, orm::schema::Dialect D = orm::schema::Dialect::SQLite>
    [[nodiscard]] auto create_table_sql() -> const std::string& {
        return orm::schema::SchemaStatement<T>::template create_table_sql<D>();
    }

    // Returns the pre-computed CREATE INDEX SQL statements for model T.
    template <typename T> [[nodiscard]] auto create_index_sql() -> const std::vector<std::string>& {
        return orm::schema::SchemaStatement<T>::create_index_sql();
    }
} // namespace storm
