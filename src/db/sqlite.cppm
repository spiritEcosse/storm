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
import <array>;
import <cstdint>;

export namespace storm::db::sqlite {

    // Cache size constants
    namespace cache {
        constexpr size_t STMT_CACHE_RESERVE = 32; // Initial statement cache capacity
    } // namespace cache

    // Custom deleter for sqlite3
    struct SqliteDeleter {
        auto operator()(sqlite3* db) const noexcept -> void {
            if (db != nullptr) {
                sqlite3_close_v2(db);
            }
        }
    };

    // Error type for database operations
    struct Error {
        int         code_;
        std::string message_;

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
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

        // OPTIMIZATION: Cache raw pointer to eliminate unique_ptr::get() overhead in hot loops
        explicit Statement(sqlite3_stmt* stmt) : stmt_(stmt, sqlite3_finalize), raw_(stmt) {}

        // Destructor - unique_ptr handles cleanup via sqlite3_finalize
        ~Statement() = default;

        // Move semantics
        Statement(Statement&&)                    = default;
        auto operator=(Statement&&) -> Statement& = default;

        // Delete copy operations
        Statement(const Statement&)                    = delete;
        auto operator=(const Statement&) -> Statement& = delete;

        // DatabaseStatement concept implementation
        // All methods use template pattern for cross-module inlining
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_int(int index, int value) noexcept
                -> std::expected<void, Error> {
            const int rc = sqlite3_bind_int(raw_, index, value);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind integer parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_text(int index, std::string_view value) noexcept
                -> std::expected<void, Error> {
            const int rc =
                    sqlite3_bind_text(raw_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind text parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_int64(int index, int64_t value) noexcept
                -> std::expected<void, Error> {
            const int rc = sqlite3_bind_int64(raw_, index, value);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind int64 parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_double(int index, double value) noexcept
                -> std::expected<void, Error> {
            const int rc = sqlite3_bind_double(raw_, index, value);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind double parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_null(int index) noexcept -> std::expected<void, Error> {
            const int rc = sqlite3_bind_null(raw_, index);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind null parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_blob(int index, const void* data, size_t size) noexcept // NOSONAR(cpp:S5008) - SQLite BLOB API
                -> std::expected<void, Error> {
            const int rc = sqlite3_bind_blob(raw_, index, data, static_cast<int>(size), SQLITE_TRANSIENT);
            if (rc != SQLITE_OK) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to bind blob parameter"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto execute() noexcept -> std::expected<void, Error> {
            const int rc = sqlite3_step(raw_);
            if (rc != SQLITE_DONE) [[unlikely]] {
                return std::unexpected(Error{rc, "Failed to execute statement"});
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto step() noexcept -> std::expected<bool, Error> {
            const int rc = sqlite3_step(raw_);
            if (rc == SQLITE_ROW) [[likely]] {
                return true; // Row available
            }
            if (rc == SQLITE_DONE) {
                return false; // No more rows
            }
            return std::unexpected(Error{rc, "Failed to step statement"});
        }

        template <typename = void> __attribute__((always_inline)) auto reset() noexcept -> void {
            sqlite3_reset(raw_);
        }

        template <typename = void> __attribute__((always_inline)) auto finalize() noexcept -> void {
            stmt_.reset(); // Calls sqlite3_finalize via deleter
        }

        // Access raw handle for advanced operations
        // Returns cached raw pointer for zero overhead
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto handle() const noexcept -> sqlite3_stmt* {
            return raw_;
        }

        // === High-Performance Abstraction Layer ===
        // These methods provide database-agnostic interface while maintaining
        // zero-cost abstraction through aggressive inlining

        // Step with raw return value (no std::expected overhead in hot loop)
        // OPTIMIZATION: Template forces body visibility across modules, enabling inlining
        // Uses cached raw pointer to eliminate unique_ptr::get() overhead
        template <typename = void> [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
            return sqlite3_step(raw_);
        }

        // Column extraction methods - TEMPLATES for cross-module inlining
        // Use cached raw pointer for zero overhead
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_int(int col_index) const noexcept -> int {
            return sqlite3_column_int(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_int64(int col_index) const noexcept -> int64_t {
            return sqlite3_column_int64(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_double(int col_index) const noexcept -> double {
            return sqlite3_column_double(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_text_ptr(int col_index) const noexcept -> const
                unsigned char* {
            return sqlite3_column_text(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_bytes(int col_index) const noexcept -> int {
            return sqlite3_column_bytes(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_text_view(int col_index) const noexcept
                -> std::string_view {
            const unsigned char* text = sqlite3_column_text(raw_, col_index);
            if (text != nullptr) {
                const auto len = static_cast<size_t>(sqlite3_column_bytes(raw_, col_index));
                return {reinterpret_cast<const char*>(text), len};
            }
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_bool(int col_index) const noexcept -> bool {
            return sqlite3_column_int(raw_, col_index) != 0;
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_float(int col_index) const noexcept -> float {
            return static_cast<float>(sqlite3_column_double(raw_, col_index));
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_blob_ptr(int col_index) const noexcept -> const
                void* { // NOSONAR(cpp:S5008) - SQLite BLOB API
            return sqlite3_column_blob(raw_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto is_null(int col_index) const noexcept -> bool {
            return sqlite3_column_type(raw_, col_index) == SQLITE_NULL;
        }

        // Error message extraction
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto get_error_message() const noexcept -> const char* {
            return sqlite3_errmsg(sqlite3_db_handle(raw_));
        }

        // Returns SQL string with all bound parameters inlined (for debugging / SQL inspection)
        // Uses sqlite3_expanded_sql() which substitutes ? placeholders with actual bound values
        template <typename = void> [[nodiscard]] auto expanded_sql() const -> std::string {
            char* expanded = sqlite3_expanded_sql(raw_);
            if (expanded == nullptr) {
                return {};
            }
            std::string result(expanded);
            sqlite3_free(expanded);
            return result;
        }

        // Constants for return codes (make them constexpr for compile-time checks)
        static constexpr int  ROW_AVAILABLE      = SQLITE_ROW;
        static constexpr int  NO_MORE_ROWS       = SQLITE_DONE;
        static constexpr bool preserves_bindings = true; // sqlite3_reset() preserves bindings

      private:
        StmtPtr       stmt_;
        sqlite3_stmt* raw_; // Cached raw pointer for hot-path performance
    };

    class Connection {
        using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
        // Issue #215: storing `unique_ptr<Statement>` (not `Statement`) keeps the
        // Statement object pinned in place across map rehashes. Upstream Level 2
        // caches hold raw `Statement*` pointers obtained from prepare_cached();
        // with value storage those pointers would dangle after any insert that
        // triggers a rehash.
        using StatementValue = std::unique_ptr<Statement>;
        using StatementCache =
                std::unordered_map<std::string, StatementValue, storm::db::string_hash, storm::db::string_equal>;

      public:
        using Error     = sqlite::Error;
        using Statement = sqlite::Statement;

        // Dialect traits (version-gated via CMake — see cmake/db.cmake)
        static constexpr bool supports_limit_all = false;
        static constexpr bool supports_returning = true; // SQLite 3.35+ required
#ifdef STORM_SQLITE_STRICT_TABLES
        static constexpr bool supports_strict_tables = true; // SQLite 3.37+
#else
        static constexpr bool supports_strict_tables = false;
#endif
#ifdef STORM_SQLITE_RIGHT_JOIN
        static constexpr bool supports_right_join = true; // SQLite 3.39+
#else
        static constexpr bool supports_right_join = false;
#endif

        // Factory method with error handling and thread-safe flags
        [[nodiscard]] static auto open(std::string_view db_path) -> std::expected<Connection, Error> {
            sqlite3*          raw_db = nullptr;
            const std::string db_path_str(db_path); // Ensure null-termination
            // Add SQLITE_OPEN_FULLMUTEX for serialized mode (thread-safe)
            const int rc = sqlite3_open_v2(
                    db_path_str.c_str(),
                    &raw_db,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI | SQLITE_OPEN_FULLMUTEX,
                    nullptr
            );

            if (rc != SQLITE_OK) {
                const auto error = Error{rc, sqlite3_errmsg(raw_db)};
                if (raw_db != nullptr) {
                    sqlite3_close_v2(raw_db);
                }
                return std::unexpected(error);
            }

            return Connection{SqlitePtr{raw_db}};
        }

        // Destructor - unique_ptr handles cleanup via SqliteDeleter
        ~Connection() = default;

        // Move semantics (smart pointer handles cleanup)
        Connection(Connection&&)                    = default;
        auto operator=(Connection&&) -> Connection& = default;

        // Delete copy operations
        Connection(const Connection&)                    = delete;
        auto operator=(const Connection&) -> Connection& = delete;

        // DatabaseConnection concept implementation
        [[nodiscard]] constexpr auto is_open() const noexcept -> bool {
            return static_cast<bool>(db);
        }

        [[nodiscard]] auto prepare(std::string_view sql) -> std::expected<Statement, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }
            return prepare_raw(sql);
        }

        // Prepare statement with caching - reuses statements for identical SQL
        // OPTIMIZATION: Uses heterogeneous lookup to avoid string allocation on cache hit
        [[nodiscard]] auto prepare_cached(std::string_view sql) -> std::expected<Statement*, Error> {
            if (!is_open()) {
                return std::unexpected(Error{SQLITE_MISUSE, "Connection not open"});
            }

            // OPTIMIZATION: Lookup using string_view directly (no allocation!)
            // The transparent hash/equal functors enable this
            auto it = statement_cache_.find(sql);
            if (it != statement_cache_.end()) [[likely]] {
                // Cache hit - reset cached statement for reuse
                it->second->reset();
                return it->second.get();
            }

            // Cache miss - prepare a fresh statement and cache it
            auto prepared = prepare_raw(sql);
            if (!prepared.has_value()) {
                return std::unexpected(prepared.error());
            }

            // Only allocate string for storage in cache (on miss)
            // emplace always succeeds here: find() above confirmed key doesn't exist
            auto [inserted_it, inserted] =
                    statement_cache_.emplace(std::string(sql), std::make_unique<Statement>(std::move(*prepared)));
            (void)inserted; // Always true: find() above confirmed key absence
            return inserted_it->second.get();
        }

        // Clear the entire statement cache (useful for memory management or after
        // major schema changes). Level 2 callers (Insert/Update/Erase/Select) holding
        // `Statement*` from a prior prepare_cached() MUST invalidate their own
        // pointers before calling this — see QuerySet::invalidate_cache().
        auto clear_statement_cache() noexcept -> void {
            statement_cache_.clear();
        }

        // Issue #215: drop cached entries whose SQL references the given table.
        // Used after targeted DDL (ALTER TABLE persons …) when other tables'
        // cached statements should be preserved. Matching is word-boundary aware
        // so clearing "persons" does NOT touch "person_addresses".
        auto clear_statement_cache(std::string_view table) -> void {
            std::erase_if(statement_cache_, [table](const auto& entry) {
                return storm::db::sql_references_table(entry.first, table);
            });
        }

        // Get cache statistics
        [[nodiscard]] auto cached_statement_count() const noexcept -> size_t {
            return statement_cache_.size();
        }

        // Pre-populate statement cache with common operations
        auto prepare_common_statements() -> void {
            // Common patterns that benefit from pre-compilation
            static const std::array<std::string_view, 3> common_patterns = {"BEGIN TRANSACTION", "COMMIT", "ROLLBACK"};

            for (const auto& sql : common_patterns) {
                // Pre-populate cache (ignore errors for optional optimization)
                // NOLINTNEXTLINE(bugprone-unused-return-value)
                (void)prepare_cached(sql);
            }
        }

        // Execute SQL with error handling (optimized to use cached statements for common operations)
        [[nodiscard]] auto execute(std::string_view sql) -> std::expected<void, Error> {
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
            // sqlite3_exec requires null-terminated string
            const std::string sql_str(sql);
            char*             errmsg = nullptr;
            const int         rc     = sqlite3_exec(db.get(), sql_str.c_str(), nullptr, nullptr, &errmsg);

            if (rc != SQLITE_OK) {
                const Error error{rc, errmsg != nullptr ? errmsg : "Unknown error"};
                sqlite3_free(errmsg);
                return std::unexpected(error);
            }

            return {};
        }

        [[nodiscard]] auto get() const noexcept -> sqlite3* {
            return db.get();
        }

      private:
        explicit Connection(SqlitePtr db_ptr) : db(std::move(db_ptr)) {
            // Reserve capacity to keep early inserts on the same rehash bucket.
            // Pointer stability across rehash is now guaranteed by the
            // `unique_ptr<Statement>` value type (Issue #215).
            statement_cache_.reserve(cache::STMT_CACHE_RESERVE);
        }

        // Single source of truth for sqlite3_prepare_v2 + error wrapping.
        [[nodiscard]] auto prepare_raw(std::string_view sql) -> std::expected<Statement, Error> {
            sqlite3_stmt* stmt = nullptr;
            const int     rc   = sqlite3_prepare_v2(db.get(), sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
            if (rc != SQLITE_OK) {
                return std::unexpected(Error{rc, sqlite3_errmsg(db.get())});
            }
            return Statement{stmt};
        }

        SqlitePtr      db;
        StatementCache statement_cache_;
    };

    // Verify concepts are satisfied
    static_assert(storm::db::DatabaseConnection<Connection>);
    static_assert(storm::db::CachedDatabaseConnection<Connection>);
    static_assert(storm::db::DatabaseStatement<Statement>);
    static_assert(storm::db::DatabaseError<Error>);

} // namespace storm::db::sqlite
