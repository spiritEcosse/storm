module;

#include <libpq-fe.h>

export module storm_db_postgresql_connection;

import std;
import storm_db_concept;
import storm_db_postgresql_error;
import storm_db_postgresql_statement;

export namespace storm::db::postgresql {

    // Custom deleter for PGconn
    struct PGconnDeleter {
        auto operator()(PGconn* conn) const noexcept -> void {
            if (conn != nullptr) {
                PQfinish(conn);
            }
        }
    };

    class Connection {
        using PGconnPtr = std::unique_ptr<PGconn, PGconnDeleter>;
        // Issue #215: storing `unique_ptr<Statement>` keeps Statement objects
        // pinned across map rehashes, so Level 2 callers can hold
        // `Statement*` from prepare_cached() safely.
        using StatementValue = std::unique_ptr<Statement>;
        using StatementCache =
                std::unordered_map<std::string, StatementValue, storm::db::string_hash, storm::db::string_equal>;

        // Cache size constants
        static constexpr std::size_t STMT_CACHE_RESERVE = 32;

      public:
        using Error     = postgresql::Error;
        using Statement = postgresql::Statement;

        // Dialect traits
        static constexpr bool supports_limit_all  = true;
        static constexpr bool supports_returning  = true;
        static constexpr bool supports_right_join = true;
        static constexpr bool uses_pg_dialect     = true;

        // Pre-populate statement cache with common operations (stub for PostgreSQL)
        auto prepare_common_statements() -> void {
            // PostgreSQL doesn't need pre-populated common statements
            // Transaction management uses direct PQexec
        }

        // Factory method with error handling
        [[nodiscard]] static auto open(std::string_view conninfo) -> std::expected<Connection, Error> {
            const std::string conninfo_str(conninfo); // Ensure null-termination
            PGconn*           raw_conn = PQconnectdb(conninfo_str.c_str());

            if (raw_conn == nullptr) {
                return std::unexpected(Error{-1, "Failed to allocate PGconn"});
            }

            if (PQstatus(raw_conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(raw_conn);
                PQfinish(raw_conn);
                return std::unexpected(Error{static_cast<int>(PQstatus(raw_conn)), msg});
            }

            return Connection{PGconnPtr{raw_conn}};
        }

        // Destructor - unique_ptr handles cleanup via PGconnDeleter
        ~Connection() = default;

        // Move semantics. cache_mutex_ is a MovableSharedMutex (Issue #271) so
        // these can stay defaulted: a moved Connection gets a fresh mutex.
        Connection(Connection&&)                    = default;
        auto operator=(Connection&&) -> Connection& = default;

        // Delete copy operations
        Connection(const Connection&)                    = delete;
        auto operator=(const Connection&) -> Connection& = delete;

        // Connection state
        [[nodiscard]] constexpr auto is_open() const noexcept -> bool {
            return static_cast<bool>(conn_);
        }

        // Prepare a statement - translates ? placeholders to $1, $2, ...
        [[nodiscard]] auto prepare(std::string_view sql) -> std::expected<Statement, Error> {
            auto stmt_name = prepare_pg_statement(sql);
            if (!stmt_name) {
                return std::unexpected(stmt_name.error());
            }
            return Statement{conn_.get(), std::move(*stmt_name)};
        }

        // Prepare with caching - reuses prepared statements for identical SQL.
        // Lock discipline lives in storm_db_concept's cache_* helpers (Issue #271,
        // shared with the SQLite backend); only the backend-specific prepare step
        // (prepare_pg_statement + set_original_sql) differs. On a miss the lock is
        // dropped before PQprepare so the expensive syscall runs uncontended.
        [[nodiscard]] auto prepare_cached(std::string_view sql) -> std::expected<Statement*, Error> {
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }

            if (auto* hit = storm::db::cache_find_hit(statement_cache_, cache_mutex_, sql)) [[likely]] {
                return hit;
            }

            auto stmt_name = prepare_pg_statement(sql);
            if (!stmt_name) {
                return std::unexpected(stmt_name.error());
            }
            auto new_stmt = std::make_unique<Statement>(conn_.get(), std::move(*stmt_name));
            new_stmt->set_original_sql(std::string(sql)); // Store original SQL for expanded_sql()
            return storm::db::cache_try_insert(statement_cache_, cache_mutex_, sql, std::move(new_stmt));
        }

        // Clear the entire statement cache (useful for memory management or after
        // major schema changes). Level 2 callers must invalidate their own
        // pointers before calling this — see QuerySet::invalidate_cache().
        auto clear_statement_cache() noexcept -> void {
            storm::db::cache_clear_all(statement_cache_, cache_mutex_); // Issue #271
        }

        // Issue #215: drop cached entries whose SQL references the given table.
        // Word-boundary aware so clearing "persons" does NOT touch
        // "person_addresses" or "persons_archive".
        auto clear_statement_cache(std::string_view table) -> void {
            storm::db::cache_clear_table(statement_cache_, cache_mutex_, table); // Issue #271
        }

        [[nodiscard]] auto cached_statement_count() const noexcept -> std::size_t {
            return storm::db::cache_count(statement_cache_, cache_mutex_); // Issue #271
        }

        // Execute SQL directly (simple queries without parameters)
        [[nodiscard]] auto execute(std::string_view sql) -> std::expected<void, Error> {
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }

            const std::string sql_str(sql); // Ensure null-termination
            PGresult*         res = PQexec(conn_.get(), sql_str.c_str());

            if (res == nullptr) {
                return std::unexpected(Error{-1, PQerrorMessage(conn_.get())});
            }

            const ExecStatusType status = PQresultStatus(res);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn_.get());
                PQclear(res);
                return std::unexpected(Error{static_cast<int>(status), msg});
            }

            PQclear(res);
            return {};
        }

        [[nodiscard]] auto get() const noexcept -> PGconn* {
            return conn_.get();
        }

      private:
        explicit Connection(PGconnPtr conn_ptr) : conn_(std::move(conn_ptr)) {
            statement_cache_.reserve(STMT_CACHE_RESERVE);
        }

        // Translate ? placeholders to $1, $2, ... for PostgreSQL
        [[nodiscard]] static auto translate_placeholders(std::string_view sql) -> std::string {
            std::string result;
            result.reserve(sql.size() + 16); // Extra space for $N expansions
            int param_index = 0;

            bool in_single_quote = false;
            bool in_double_quote = false;

            for (std::size_t i = 0; i < sql.size(); ++i) {
                const char ch = sql[i];

                // Track quoted strings to avoid translating ? inside them
                if (ch == '\'' && !in_double_quote) {
                    in_single_quote = !in_single_quote;
                    result += ch;
                } else if (ch == '"' && !in_single_quote) {
                    in_double_quote = !in_double_quote;
                    result += ch;
                } else if (ch == '?' && !in_single_quote && !in_double_quote) {
                    ++param_index;
                    result += '$';
                    result += std::to_string(param_index);
                } else {
                    result += ch;
                }
            }

            return result;
        }

        // Generate unique prepared statement names
        [[nodiscard]] auto next_stmt_name() -> std::string {
            return "_storm_" + std::to_string(stmt_counter_++);
        }

        // Shared body of prepare() and prepare_cached(): translates placeholders,
        // calls PQprepare, returns the generated statement name on success.
        [[nodiscard]] auto prepare_pg_statement(std::string_view sql) -> std::expected<std::string, Error> {
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }
            const std::string pg_sql    = translate_placeholders(sql);
            std::string       stmt_name = next_stmt_name();
            PGresult*         res       = PQprepare(conn_.get(), stmt_name.c_str(), pg_sql.c_str(), 0, nullptr);
            if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
                const std::string msg = PQerrorMessage(conn_.get());
                if (res != nullptr) {
                    PQclear(res);
                }
                return std::unexpected(Error{-1, msg});
            }
            PQclear(res);
            return stmt_name;
        }

        PGconnPtr      conn_;
        StatementCache statement_cache_;
        // Issue #271: guards statement_cache_. shared_lock on the cache-hit hot
        // path, unique_lock for insert + clear. mutable so const accessors
        // (cached_statement_count) can take a shared_lock. MovableSharedMutex
        // keeps Connection's move operations defaulted.
        mutable storm::db::MovableSharedMutex cache_mutex_;
        int                                   stmt_counter_ = 0;
    };

    // Verify concepts are satisfied
    static_assert(storm::db::DatabaseConnection<Connection>);
    static_assert(storm::db::CachedDatabaseConnection<Connection>);
    static_assert(storm::db::DatabaseStatement<Statement>);
    static_assert(storm::db::DatabaseError<Error>);

} // namespace storm::db::postgresql
