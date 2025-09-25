module;

#include <sqlite3.h>

export module storm_db_sqlite_adapter;
import storm_db_concept;
import storm_db_sqlite;

import <memory>;
import <expected>;
import <string>;
import <string_view>;

export namespace storm::db::sqlite {

    // Forward declaration
    class Statement;

    // Use the Error type from storm.db.sqlite
    using Error = storm::db::sqlite::Error;

    // RAII wrapper for sqlite3_stmt
    class Statement {
        using StmtPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

      public:
        using Error = storm::db::sqlite::Error;

        explicit Statement(sqlite3_stmt* stmt) : stmt_(stmt, sqlite3_finalize) {}

        // Move semantics
        Statement(Statement&&)            = default;
        Statement& operator=(Statement&&) = default;

        // Delete copy operations
        Statement(const Statement&)            = delete;
        Statement& operator=(const Statement&) = delete;

        // DatabaseStatement concept implementation
        [[nodiscard]] auto bind_int(int index, int value) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_int(stmt_.get(), index, value);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind integer parameter"});
            }
            return {};
        }

        [[nodiscard]] auto bind_text(int index, std::string_view value) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_text(
                    stmt_.get(), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT
            );
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind text parameter"});
            }
            return {};
        }

        [[nodiscard]] auto execute() noexcept -> std::expected<void, Error> {
            int rc = sqlite3_step(stmt_.get());
            if (rc != SQLITE_DONE) {
                return std::unexpected(Error{rc, "Failed to execute statement"});
            }
            return {};
        }

        [[nodiscard]] auto step() noexcept -> std::expected<bool, Error> {
            int rc = sqlite3_step(stmt_.get());
            if (rc == SQLITE_ROW) {
                return true; // Row available
            } else if (rc == SQLITE_DONE) {
                return false; // No more rows
            } else {
                return std::unexpected(Error{rc, "Failed to step statement"});
            }
        }

        auto reset() noexcept -> void {
            sqlite3_reset(stmt_.get());
            sqlite3_clear_bindings(stmt_.get());
        }

        auto finalize() noexcept -> void {
            stmt_.reset(); // Calls sqlite3_finalize via deleter
        }

        // Access raw handle for advanced operations
        [[nodiscard]] sqlite3_stmt* handle() const noexcept {
            return stmt_.get();
        }

      private:
        StmtPtr stmt_;
    };

    // DatabaseConnection adapter for existing SQLite Connection
    class ConnectionAdapter {
      public:
        using Error     = sqlite::Error;
        using Statement = sqlite::Statement;

        explicit ConnectionAdapter(Connection& conn) : conn_(conn) {}

        // DatabaseConnection concept implementation
        [[nodiscard]] constexpr bool is_open() const noexcept {
            return conn_.is_open();
        }

        [[nodiscard]] auto prepare(std::string_view sql) noexcept -> std::expected<Statement, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            sqlite3_stmt* stmt = nullptr;
            int           rc   = sqlite3_prepare_v2(conn_.get(), sql.data(), -1, &stmt, nullptr);

            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, sqlite3_errmsg(conn_.get())});
            }

            return Statement{stmt};
        }

        [[nodiscard]] auto execute(std::string_view sql) noexcept -> std::expected<void, Error> {
            return conn_.execute(sql).transform_error([](const auto& conn_error) {
                return Error{conn_error.code(), std::string(conn_error.message())};
            });
        }

        // Access underlying connection for compatibility
        [[nodiscard]] Connection& connection() noexcept {
            return conn_;
        }
        [[nodiscard]] const Connection& connection() const noexcept {
            return conn_;
        }

      private:
        Connection& conn_;
    };

    // Verify concepts are satisfied
    static_assert(storm::db::DatabaseConnection<ConnectionAdapter>);
    static_assert(storm::db::DatabaseStatement<Statement>);
    static_assert(storm::db::DatabaseError<Error>);

} // namespace storm::db::sqlite