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
import <cstdint>;

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

        [[nodiscard]] auto bind_int64(int index, int64_t value) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_int64(stmt_.get(), index, value);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind int64 parameter"});
            }
            return {};
        }

        [[nodiscard]] auto bind_double(int index, double value) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_double(stmt_.get(), index, value);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind double parameter"});
            }
            return {};
        }

        [[nodiscard]] auto bind_null(int index) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_null(stmt_.get(), index);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind null parameter"});
            }
            return {};
        }

        [[nodiscard]] auto bind_blob(int index, const void* data, size_t size) noexcept -> std::expected<void, Error> {
            int rc = sqlite3_bind_blob(stmt_.get(), index, data, static_cast<int>(size), SQLITE_TRANSIENT);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, "Failed to bind blob parameter"});
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

        // === High-Performance Abstraction Layer ===
        // These methods provide database-agnostic interface while maintaining
        // zero-cost abstraction through aggressive inlining

        // Step with raw return value (no std::expected overhead in hot loop)
        [[nodiscard]] __attribute__((always_inline)) inline auto step_raw() noexcept -> int {
            return sqlite3_step(stmt_.get());
        }

        // Column extraction methods - fully inlined for zero overhead
        [[nodiscard]] __attribute__((always_inline)) inline auto extract_int(int col_index) const noexcept -> int {
            return sqlite3_column_int(stmt_.get(), col_index);
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_int64(int col_index) const noexcept
                -> int64_t {
            return sqlite3_column_int64(stmt_.get(), col_index);
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_double(int col_index) const noexcept
                -> double {
            return sqlite3_column_double(stmt_.get(), col_index);
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_text_ptr(int col_index) const noexcept -> const
                unsigned char* {
            return sqlite3_column_text(stmt_.get(), col_index);
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_text_view(int col_index) const noexcept
                -> std::string_view {
            const unsigned char* text = sqlite3_column_text(stmt_.get(), col_index);
            if (text) {
                int len = sqlite3_column_bytes(stmt_.get(), col_index);
                return std::string_view(reinterpret_cast<const char*>(text), len);
            }
            return {};
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_bool(int col_index) const noexcept -> bool {
            return sqlite3_column_int(stmt_.get(), col_index) != 0;
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_float(int col_index) const noexcept -> float {
            return static_cast<float>(sqlite3_column_double(stmt_.get(), col_index));
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_blob(int col_index) const noexcept
                -> std::pair<const void*, int> {
            const void* blob = sqlite3_column_blob(stmt_.get(), col_index);
            int         size = sqlite3_column_bytes(stmt_.get(), col_index);
            return {blob, size};
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto extract_column_type(int col_index) const noexcept
                -> int {
            return sqlite3_column_type(stmt_.get(), col_index);
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto is_null(int col_index) const noexcept -> bool {
            return sqlite3_column_type(stmt_.get(), col_index) == SQLITE_NULL;
        }

        // Error message extraction
        [[nodiscard]] inline auto get_error_message() const noexcept -> const char* {
            return sqlite3_errmsg(sqlite3_db_handle(stmt_.get()));
        }

        // Constants for return codes (make them constexpr for compile-time checks)
        static constexpr int ROW_AVAILABLE = SQLITE_ROW;
        static constexpr int NO_MORE_ROWS  = SQLITE_DONE;

      private:
        StmtPtr stmt_;
    };

    // Transparent hash for string_view lookups without allocation
    struct string_hash {
        using is_transparent = void;
        using hash_type = std::hash<std::string_view>;

        [[nodiscard]] size_t operator()(std::string_view str) const noexcept {
            return hash_type{}(str);
        }

        [[nodiscard]] size_t operator()(const std::string& str) const noexcept {
            return hash_type{}(str);
        }
    };

    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }
    };

    class Connection {
        using SqlitePtr      = std::unique_ptr<sqlite3, SqliteDeleter>;
        using StatementCache = std::unordered_map<std::string, Statement, string_hash, string_equal>;

      public:
        using Error     = sqlite::Error;
        using Statement = sqlite::Statement;

        // Factory method with error handling and thread-safe flags
        [[nodiscard]] static auto open(std::string_view db_path) noexcept -> std::expected<Connection, Error> {
            sqlite3* raw_db = nullptr;
            // Add SQLITE_OPEN_FULLMUTEX for serialized mode (thread-safe)
            int rc = sqlite3_open_v2(
                    db_path.data(),
                    &raw_db,
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
        // OPTIMIZATION: Uses heterogeneous lookup to avoid string allocation on cache hit
        [[nodiscard]] auto prepare_cached(std::string_view sql) noexcept -> std::expected<Statement*, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            // OPTIMIZATION: Lookup using string_view directly (no allocation!)
            // The transparent hash/equal functors enable this
            auto it = statement_cache_.find(sql);
            if (it != statement_cache_.end()) [[likely]] {
                // Cache hit - reset cached statement for reuse
                it->second.reset();
                return &it->second;
            }

            // Cache miss - create new statement and cache it
            sqlite3_stmt* stmt = nullptr;
            int           rc   = sqlite3_prepare_v2(db.get(), sql.data(), -1, &stmt, nullptr);

            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, sqlite3_errmsg(db.get())});
            }

            // Only allocate string for storage in cache (on miss)
            auto [inserted_it, success] = statement_cache_.emplace(std::string(sql), Statement{stmt});
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
            static const std::vector<std::string> common_patterns = {"BEGIN TRANSACTION", "COMMIT", "ROLLBACK"};

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
            static const std::unordered_set<std::string_view> cached_operations =
                    {"BEGIN TRANSACTION", "COMMIT", "ROLLBACK"};

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

        // Get the row ID of the most recent successful INSERT
        [[nodiscard]] int64_t last_insert_rowid() const noexcept {
            return sqlite3_last_insert_rowid(db.get());
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
