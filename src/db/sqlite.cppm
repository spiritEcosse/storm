module;

#include <sqlite3.h>

export module storm_db_sqlite;
import storm_db_concept;
import <expected>;
import <string_view>;
import <string>;
import <memory>;
import <unordered_map>;
import <unordered_set>;
import <vector>;

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

    // Forward declaration
    class Connection;

    // RAII wrapper for sqlite3_stmt
    class Statement {
        using StmtPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

      public:
        using Error = sqlite::Error;

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

    class Connection {
        using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
        using StatementCache = std::unordered_map<std::string, Statement>;

      public:
        using Error     = sqlite::Error;
        using Statement = sqlite::Statement;

        // Factory method with error handling and thread-safe flags
        [[nodiscard]] static auto open(std::string_view db_path) noexcept -> std::expected<Connection, Error> {
            sqlite3* raw_db = nullptr;
            // Add SQLITE_OPEN_FULLMUTEX for serialized mode (thread-safe)
            int      rc     = sqlite3_open_v2(
                    db_path.data(), &raw_db,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI | SQLITE_OPEN_FULLMUTEX,
                    nullptr
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

        // DatabaseConnection concept implementation
        [[nodiscard]] constexpr bool is_open() const noexcept {
            return static_cast<bool>(db);
        }

        [[nodiscard]] auto prepare(std::string_view sql) noexcept -> std::expected<Statement, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            sqlite3_stmt* stmt = nullptr;
            int           rc   = sqlite3_prepare_v2(db.get(), sql.data(), -1, &stmt, nullptr);

            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, sqlite3_errmsg(db.get())});
            }

            return Statement{stmt};
        }

        // Prepare statement with caching - reuses statements for identical SQL
        [[nodiscard]] auto prepare_cached(std::string_view sql) noexcept -> std::expected<Statement*, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            std::string sql_key{sql};

            // Try to find cached statement
            auto it = statement_cache_.find(sql_key);
            if (it != statement_cache_.end()) {
                // Reset cached statement for reuse
                it->second.reset();
                return &it->second;
            }

            // Create new statement and cache it
            sqlite3_stmt* stmt = nullptr;
            int           rc   = sqlite3_prepare_v2(db.get(), sql.data(), -1, &stmt, nullptr);

            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, sqlite3_errmsg(db.get())});
            }

            auto [inserted_it, success] = statement_cache_.emplace(std::move(sql_key), Statement{stmt});
            if (!success) {
                return std::unexpected(Error{SQLITE_INTERNAL, "Failed to cache statement"});
            }

            return &inserted_it->second;
        }

        // Clear statement cache (useful for memory management)
        void clear_statement_cache() noexcept {
            statement_cache_.clear();
        }

        // Get cache statistics
        [[nodiscard]] size_t cached_statement_count() const noexcept {
            return statement_cache_.size();
        }

        // Pre-populate statement cache with common operations
        void prepare_common_statements() noexcept {
            // Common patterns that benefit from pre-compilation
            static const std::vector<std::string> common_patterns = {
                "BEGIN TRANSACTION",
                "COMMIT",
                "ROLLBACK"
            };

            for (const auto& sql : common_patterns) {
                // Pre-populate cache (ignore errors for optional optimization)
                (void)prepare_cached(sql);
            }
        }

        // Execute SQL with error handling (optimized to use cached statements for common operations)
        [[nodiscard]] auto execute(std::string_view sql) noexcept -> std::expected<void, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            // For common operations, try to use cached prepared statements for better performance
            static const std::unordered_set<std::string_view> cached_operations = {
                "BEGIN TRANSACTION", "COMMIT", "ROLLBACK"
            };

            if (cached_operations.contains(sql)) {
                auto stmt_result = prepare_cached(sql);
                if (stmt_result.has_value()) {
                    return (*stmt_result)->execute();
                }
                // Fall through to regular execution if caching fails
            }

            // Regular execution for non-cached operations
            char* errmsg = nullptr;
            int   rc     = sqlite3_exec(db.get(), sql.data(), nullptr, nullptr, &errmsg);

            if (rc != SQLITE_OK) {
                Error error{rc, errmsg ? errmsg : "Unknown error"};
                sqlite3_free(errmsg);
                return std::unexpected(error);
            }

            return {};
        }

        // Access raw SQLite handle for advanced operations
        [[nodiscard]] sqlite3* get() const noexcept {
            return db.get();
        }

      private:
        explicit Connection(SqlitePtr db_ptr) : db(std::move(db_ptr)) {}

        SqlitePtr      db;
        StatementCache statement_cache_;
    };

    // Verify concepts are satisfied
    static_assert(storm::db::DatabaseConnection<Connection>);
    static_assert(storm::db::CachedDatabaseConnection<Connection>);
    static_assert(storm::db::DatabaseStatement<Statement>);
    static_assert(storm::db::DatabaseError<Error>);

} // namespace storm::db::sqlite
