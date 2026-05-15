module;

export module storm_db_concept;
import <expected>;
import <string_view>;
import <string>;
import <concepts>;
import <cstddef>;
import <functional>;

export namespace storm::db {

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
        { conn.cached_statement_count() } -> std::same_as<size_t>;
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

        [[nodiscard]] auto operator()(std::string_view str) const noexcept -> size_t {
            return hash_type{}(str);
        }
        [[nodiscard]] auto operator()(const std::string& str) const noexcept -> size_t {
            return hash_type{}(str);
        }
    };

    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] auto operator()(std::string_view lhs, std::string_view rhs) const noexcept -> bool {
            return lhs == rhs;
        }
    };

} // namespace storm::db