module;

#include <sqlite3.h>

export module storm_db_sqlite;
import <expected>;
import <string_view>;
import <string>;
import <memory>;

export namespace storm::db::sqlite {

    // Custom deleter for sqlite3
    struct SqliteDeleter {
        void operator()(sqlite3* db) const noexcept {
            if (db)
                sqlite3_close_v2(db);
        }
    };

    // Error type for database operations
    struct Error {
        int         code_;
        std::string message_;

        [[nodiscard]] constexpr int code() const noexcept {
            return code_;
        }
        [[nodiscard]] constexpr std::string_view message() const noexcept {
            return message_;
        }
    };

    class Connection {
        using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;

      public:
        // Factory method with error handling
        [[nodiscard]] static auto open(std::string_view db_path) noexcept -> std::expected<Connection, Error> {
            sqlite3* raw_db = nullptr;
            int      rc     = sqlite3_open_v2(
                    db_path.data(), &raw_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, nullptr
            );

            if (rc != SQLITE_OK) {
                auto error = Error{rc, sqlite3_errmsg(raw_db)};
                if (raw_db)
                    sqlite3_close_v2(raw_db);
                return std::unexpected(error);
            }

            return Connection{SqlitePtr{raw_db}};
        }

        // Move semantics (smart pointer handles cleanup)
        Connection(Connection&&)            = default;
        Connection& operator=(Connection&&) = default;

        // Delete copy operations
        Connection(const Connection&)            = delete;
        Connection& operator=(const Connection&) = delete;

        // Modern accessors
        [[nodiscard]] constexpr bool is_open() const noexcept {
            return static_cast<bool>(db);
        }

        [[nodiscard]] sqlite3* get() const noexcept {
            return db.get();
        }

        // Execute SQL with error handling
        [[nodiscard]] auto execute(std::string_view sql) noexcept -> std::expected<void, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            char* errmsg = nullptr;
            int   rc     = sqlite3_exec(db.get(), sql.data(), nullptr, nullptr, &errmsg);

            if (rc != SQLITE_OK) {
                Error error{rc, errmsg ? errmsg : "Unknown error"};
                sqlite3_free(errmsg);
                return std::unexpected(error);
            }

            return {};
        }

      private:
        explicit Connection(SqlitePtr db_ptr) : db(std::move(db_ptr)) {}

        SqlitePtr db;
    };

} // namespace storm::db::sqlite
