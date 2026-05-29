module;

export module storm_db_concept;

import std;

export namespace storm::db {

    // Error type for database operations.
    // Non-allocating: stores the message inline so construction is noexcept and Error can
    // be returned from noexcept catch blocks without risking bad_alloc (issue #316).
    struct Error {
        static constexpr std::size_t kMaxMessageLen = 255;

        int                                  code_{0};
        std::array<char, kMaxMessageLen + 1> message_buf_{};
        std::size_t                          message_len_{0};

        Error() noexcept = default;

        Error(int code, std::string_view msg) noexcept : code_{code} {
            const std::size_t n = msg.size() < kMaxMessageLen ? msg.size() : kMaxMessageLen;
            if (n > 0) {
                std::memcpy(message_buf_.data(), msg.data(), n);
            }
            message_buf_[n] = '\0';
            message_len_    = n;
        }

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return std::string_view{message_buf_.data(), message_len_};
        }
    };

    // Forward declarations for concept requirements
    template <typename T> struct ConnectionTraits;

    template <typename T> struct StatementTraits;

    // Database connection concept
    template <typename T>
    concept DatabaseConnection = requires(T& conn, std::string_view sql) {
        // Type aliases required
        typename T::Error;
        typename T::Statement;

        // Connection state
        { conn.is_open() } -> std::same_as<bool>;

        // Statement preparation
        { conn.prepare(sql) } -> std::same_as<std::expected<typename T::Statement, typename T::Error>>;

        // SQL execution (simple queries)
        { conn.execute(sql) } -> std::same_as<std::expected<void, typename T::Error>>;
    };

    // Extended database connection concept with caching support
    template <typename T>
    concept CachedDatabaseConnection = DatabaseConnection<T> && requires(T& conn, std::string_view sql) {
        // Cached statement preparation
        { conn.prepare_cached(sql) } -> std::same_as<std::expected<typename T::Statement*, typename T::Error>>;

        // Cache management — clear-all and per-table (Issue #215)
        { conn.clear_statement_cache() } -> std::same_as<void>;
        { conn.clear_statement_cache(sql) } -> std::same_as<void>;
        { conn.cached_statement_count() } -> std::same_as<std::size_t>;
    };

    // Database statement concept
    template <typename T>
    concept DatabaseStatement = requires(T& stmt, int int_val, std::string_view str_val) {
        // Type alias required
        typename T::Error;

        // Parameter binding
        { stmt.bind_int(1, int_val) } -> std::same_as<std::expected<void, typename T::Error>>;
        { stmt.bind_text(1, str_val) } -> std::same_as<std::expected<void, typename T::Error>>;

        // Execution
        { stmt.execute() } -> std::same_as<std::expected<void, typename T::Error>>;
        { stmt.step() } -> std::same_as<std::expected<bool, typename T::Error>>; // true if row available

        // Statement management
        { stmt.reset() } -> std::same_as<void>;
        { stmt.finalize() } -> std::same_as<void>;
    };

    // Database error concept
    template <typename T>
    concept DatabaseError = requires(const T& err) {
        { err.code() } -> std::integral;
        { err.message() } -> std::convertible_to<std::string_view>;
    };

    // Helper traits for type deduction
    template <DatabaseConnection T> struct ConnectionTraits<T> {
        using Error     = typename T::Error;
        using Statement = typename T::Statement;
    };

    template <DatabaseStatement T> struct StatementTraits<T> {
        using Error = typename T::Error;
    };

    // Transparent hash + equal for heterogeneous string_view lookup in caches.
    // Shared by every backend's StatementCache (sqlite.cppm, postgresql.cppm).
    struct string_hash {
        using is_transparent = void;
        using hash_type      = std::hash<std::string_view>;

        [[nodiscard]] auto operator()(std::string_view str) const noexcept -> std::size_t {
            return hash_type{}(str);
        }
        [[nodiscard]] auto operator()(const std::string& str) const noexcept -> std::size_t {
            return hash_type{}(str);
        }
    };

    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] auto operator()(std::string_view lhs, std::string_view rhs) const noexcept -> bool {
            return lhs == rhs;
        }
    };

    // Identifier-character predicate (SQL word boundary): same set as `\w` in regex
    // ([A-Za-z0-9_]). Used by per-table cache invalidation to avoid clearing
    // "persons" entries when invalidating "person".
    [[nodiscard]] constexpr auto is_sql_ident_char(char c) noexcept -> bool {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    }

    [[nodiscard]] inline auto is_word_boundary_match(std::string_view sql, std::size_t pos, std::size_t len) noexcept
            -> bool {
        const bool left_ok  = pos == 0 || !is_sql_ident_char(sql[pos - 1]);
        const bool right_ok = pos + len == sql.size() || !is_sql_ident_char(sql[pos + len]);
        return left_ok && right_ok;
    }

    // Word-boundary table-name match in a SQL string. Identifier characters on
    // either side of the match disqualify it, so clearing "persons" does not
    // touch "person_addresses" or "persons_archive". Issue #215.
    [[nodiscard]] inline auto sql_references_table(std::string_view sql, std::string_view table) noexcept -> bool {
        if (table.empty() || sql.size() < table.size()) {
            return false;
        }
        for (std::size_t pos = 0; (pos = sql.find(table, pos)) != std::string_view::npos; pos += table.size()) {
            if (is_word_boundary_match(sql, pos, table.size())) {
                return true;
            }
        }
        return false;
    }

} // namespace storm::db
