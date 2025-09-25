module;

#include <sqlite3.h>

export module storm_query_set;

import storm_statement_remove;
import storm_db_concept;
import storm_db_manager;
import storm_db_sqlite_adapter;

import <expected>;
import <string>;
import <span>;
import <concepts>;

export namespace storm {
    template <class T, storm::db::DatabaseConnection ConnType = storm::db::sqlite::ConnectionAdapter> class QuerySet {
        using Error = typename ConnType::Error;

      public:
        // Constructor with explicit connection (backward compatibility)
        explicit QuerySet(ConnType& conn) : conn_(conn) {}

        // Default constructor using global connection manager
        QuerySet() requires std::same_as<ConnType, storm::db::sqlite::ConnectionAdapter>
            : conn_(storm::db::ConnectionManager::get_default_connection()) {}

        std::expected<bool, Error> remove(const T& obj) {
            return execute_remove(std::span<const T>{&obj, 1});
        }

      private:
        [[nodiscard]] std::expected<bool, Error> execute_remove(std::span<const T> objects) const noexcept {
            return RemoveStatement<T, ConnType>(conn_).execute(objects);
        }

        ConnType& conn_;
    };

    // Factory function for convenient QuerySet creation with default connection
    template <typename T>
    [[nodiscard]] auto make_queryset() -> QuerySet<T> {
        return QuerySet<T>{};
    }

    // Factory function for convenient QuerySet creation with explicit connection
    template <typename T, storm::db::DatabaseConnection ConnType>
    [[nodiscard]] auto make_queryset(ConnType& conn) -> QuerySet<T, ConnType> {
        return QuerySet<T, ConnType>{conn};
    }

} // namespace storm
