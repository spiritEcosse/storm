module;

#include <expected>
#include <string>

export module storm_statement_sqlite_remove;
import storm_statement_remove;
import storm_db_sqlite_adapter;
import storm_db_sqlite;

export namespace storm {

    // Convenience alias for SQLite RemoveStatement
    template <typename T> using SqliteRemoveStatement = RemoveStatement<T, storm::db::sqlite::ConnectionAdapter>;

    // Factory function for easier usage
    template <typename T>
    [[nodiscard]] auto make_sqlite_remove_statement(storm::db::sqlite::Connection& conn) -> SqliteRemoveStatement<T> {
        auto adapter = storm::db::sqlite::ConnectionAdapter{conn};
        return SqliteRemoveStatement<T>{adapter};
    }

} // namespace storm