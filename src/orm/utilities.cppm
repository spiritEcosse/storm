module;

#include <meta>
#include <uuid.h>

export module storm_orm_utilities;

import <array>;
import <string>;
import <string_view>;
import <utility>;
import <expected>;
import <optional>;
import <vector>;
import <cstdint>;
import <type_traits>;
import <chrono>;
import <filesystem>;
import <cstddef>;
import <format>;
import <random>;
import <meta>;

export namespace storm::orm::utilities {

    // ============================================================================
    // SQL Fragment Length Constants (for compile-time SQL size calculations)
    // ============================================================================

    namespace sql_len {
        // SELECT-related lengths
        constexpr size_t SELECT          = 7;  // "SELECT "
        constexpr size_t SELECT_DISTINCT = 16; // "SELECT DISTINCT "
        constexpr size_t DISTINCT        = 9;  // "DISTINCT "
        constexpr size_t FROM            = 6;  // " FROM "

        // INSERT-related lengths
        constexpr size_t INSERT_INTO = 12; // "INSERT INTO "
        constexpr size_t VALUES_OPEN = 10; // ") VALUES ("

        // UPDATE-related lengths
        constexpr size_t UPDATE = 7; // "UPDATE "
        constexpr size_t SET    = 5; // " SET "

        // DELETE-related lengths
        constexpr size_t DELETE_FROM = 12; // "DELETE FROM "

        // WHERE-related lengths
        constexpr size_t WHERE             = 7;  // " WHERE "
        constexpr size_t IN_OPEN           = 5;  // " IN ("
        constexpr size_t IN_CLAUSE         = 10; // " IN (" + buffer for placeholders base
        constexpr size_t AND               = 5;  // " AND "
        constexpr size_t EQ_PARAM_AND      = 8;  // " = ? AND "
        constexpr size_t LIKE_PATTERN      = 8;  // " LIKE ?"
        constexpr size_t BETWEEN_PATTERN   = 17; // " BETWEEN ? AND ?"
        constexpr size_t IS_NOT_NULL_AND   = 17; // " IS NOT NULL AND "
        constexpr size_t LOGICAL_OP_PARENS = 8;  // "(" + " AND " or " OR " + ")"

        // JOIN-related lengths
        constexpr size_t ON_EQUALS = 5; // " ON " + " = " partial

        // Misc lengths
        constexpr size_t SMALL_BUFFER  = 10;  // Small buffer/padding
        constexpr size_t MEDIUM_BUFFER = 30;  // Medium reserve hint
        constexpr size_t LARGE_BUFFER  = 50;  // Large reserve hint
        constexpr size_t XL_BUFFER     = 100; // Extra large reserve hint
    } // namespace sql_len

    // ============================================================================
    // Buffer Size Constants
    // ============================================================================

    namespace buffer_size {
        constexpr size_t SQL_SMALL  = 512;  // Small SQL buffer
        constexpr size_t SQL_MEDIUM = 1024; // Medium SQL buffer (default)
        constexpr size_t SQL_LARGE  = 4096; // Large SQL buffer (for complex queries)

        constexpr size_t CACHE_DEFAULT = 8;  // Default SQL cache size
        constexpr size_t STMT_CACHE    = 32; // Statement cache reserve size
    } // namespace buffer_size

    // ============================================================================
    // Batch Operation Constants
    // ============================================================================

    namespace batch {
        constexpr size_t SMALL_THRESHOLD     = 10; // Batch size considered "small"
        constexpr size_t FALLBACK_BATCH_SIZE = 50; // Safe fallback for any field count
    } // namespace batch

    // Numeric constants
    namespace numeric {
        constexpr size_t MAX_SINGLE_DIGIT = 9; // Maximum single digit value (0-9)
    } // namespace numeric

    // ============================================================================
    // Collation Support
    // ============================================================================

    enum class Collate { None, Binary, NoCase, RTrim };

    constexpr auto collate_to_sql(Collate col) noexcept -> std::string_view {
        using enum Collate;
        switch (col) {
        case None:
            return "";
        case Binary:
            return " COLLATE BINARY";
        case NoCase:
            return " COLLATE NOCASE";
        case RTrim:
            return " COLLATE RTRIM";
        }
        return ""; // LCOV_EXCL_LINE - unreachable: all enum values handled above
    }

    // ============================================================================
    // Parameter Binding Utilities
    // ============================================================================

    // Helper trait to detect std::optional types
    template <typename T> struct is_optional : std::false_type {};
    template <typename TValue> struct is_optional<std::optional<TValue>> : std::true_type {};
    template <typename TValue> constexpr bool is_optional_v = is_optional<TValue>::value;

    // Unwrap std::optional<T> → T; pass-through for non-optional types
    template <typename T> struct optional_inner_type {
        using type = T;
    };
    template <typename TValue> struct optional_inner_type<std::optional<TValue>> {
        using type = TValue;
    };
    template <typename T> using optional_inner_type_t = typename optional_inner_type<T>::type;

    // Helper trait to detect std::chrono::duration types
    template <typename T> struct is_chrono_duration : std::false_type {};
    template <typename Rep, typename Period>
    struct is_chrono_duration<std::chrono::duration<Rep, Period>> : std::true_type {};
    template <typename T> constexpr bool is_chrono_duration_v = is_chrono_duration<T>::value;

    // UUID type — thin wrapper for RFC 4122 UUID strings stored as TEXT in SQLite
    struct UUID {
        std::string value;

        UUID() = default;
        explicit UUID(std::string v) : value(std::move(v)) {}
        explicit UUID(const char* v) : value(v) {}
        explicit UUID(std::string_view v) : value(v) {}

        auto operator==(const UUID&) const -> bool = default;
             operator std::string_view() const noexcept {
            return value;
        } // NOLINT(google-explicit-constructor)

        // Validate UUID using stduuid library (checks RFC 4122 compliance)
        [[nodiscard]] static auto is_valid(std::string_view sv) -> bool {
            return uuids::uuid::from_string(sv).has_value();
        }

        // Generate a random RFC 4122 v4 UUID using stduuid
        static auto generate() -> UUID {
            static thread_local std::mt19937 gen{std::random_device{}()};
            uuids::uuid_random_generator     gen_uuid{gen};
            return UUID{uuids::to_string(gen_uuid())};
        }
    };

    // ============================================================================
    // Chrono / String Conversion Helpers (ISO-8601 format)
    // ============================================================================

    namespace chrono_conv {

        inline auto parse_int(std::string_view sv) -> int {
            int val = 0;
            for (char c : sv) {
                val = val * 10 + (c - '0');
            }
            return val;
        }

        // year_month_day → "YYYY-MM-DD"
        inline auto ymd_to_string(std::chrono::year_month_day ymd) -> std::string {
            return std::format(
                    "{:04d}-{:02d}-{:02d}",
                    static_cast<int>(ymd.year()),
                    static_cast<unsigned>(ymd.month()),
                    static_cast<unsigned>(ymd.day())
            );
        }

        // "YYYY-MM-DD" → year_month_day
        inline auto string_to_ymd(std::string_view s) -> std::chrono::year_month_day {
            return std::chrono::year_month_day{
                    std::chrono::year{parse_int(s.substr(0, 4))},
                    std::chrono::month{static_cast<unsigned>(parse_int(s.substr(5, 2)))},
                    std::chrono::day{static_cast<unsigned>(parse_int(s.substr(8, 2)))}
            };
        }

        // system_clock::time_point → "YYYY-MM-DD HH:MM:SS"
        inline auto tp_to_string(std::chrono::system_clock::time_point tp) -> std::string {
            auto dp            = std::chrono::floor<std::chrono::days>(tp);
            auto ymd           = std::chrono::year_month_day{dp};
            auto time_since_dp = std::chrono::floor<std::chrono::seconds>(tp) - dp;
            auto h             = std::chrono::duration_cast<std::chrono::hours>(time_since_dp);
            auto m             = std::chrono::duration_cast<std::chrono::minutes>(time_since_dp - h);
            auto s             = time_since_dp - h - m;
            return std::format(
                    "{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    static_cast<int>(ymd.year()),
                    static_cast<unsigned>(ymd.month()),
                    static_cast<unsigned>(ymd.day()),
                    static_cast<int>(h.count()),
                    static_cast<int>(m.count()),
                    static_cast<int>(s.count())
            );
        }

        // "YYYY-MM-DD HH:MM:SS" → system_clock::time_point
        inline auto string_to_tp(std::string_view s) -> std::chrono::system_clock::time_point {
            auto ymd = std::chrono::year_month_day{
                    std::chrono::year{parse_int(s.substr(0, 4))},
                    std::chrono::month{static_cast<unsigned>(parse_int(s.substr(5, 2)))},
                    std::chrono::day{static_cast<unsigned>(parse_int(s.substr(8, 2)))}
            };
            auto dp = std::chrono::sys_days{ymd};
            return dp + std::chrono::hours{parse_int(s.substr(11, 2))} +
                   std::chrono::minutes{parse_int(s.substr(14, 2))} + std::chrono::seconds{parse_int(s.substr(17, 2))};
        }

    } // namespace chrono_conv

    // Generic parameter binding - unified implementation for WHERE and CRUD statements
    // No dependency on entity type T - pure type dispatch based on value type
    template <typename StmtType, typename ErrorType>
    [[nodiscard]] auto bind_parameter_value(StmtType& stmt, int param_index, const auto& value) noexcept // NOSONAR
            -> std::expected<void, ErrorType> {
        using ValueType = std::decay_t<decltype(value)>;

        // Handle std::optional types first
        if constexpr (is_optional_v<ValueType>) {
            if (value.has_value()) {
                // Recursively bind the contained value
                return bind_parameter_value<StmtType, ErrorType>(stmt, param_index, *value);
            }
            // Bind NULL for std::nullopt
            return stmt.bind_null(param_index);
        }
        // Boolean type (stored as INTEGER 0/1)
        else if constexpr (std::is_same_v<ValueType, bool>) {
            return stmt.bind_int(param_index, value ? 1 : 0);
        }
        // Integer types
        else if constexpr (std::is_same_v<ValueType, int>) {
            return stmt.bind_int(param_index, value);
        } else if constexpr (std::is_same_v<ValueType, int64_t> || std::is_same_v<ValueType, long> ||
                             std::is_same_v<ValueType, long long> || std::is_same_v<ValueType, uint64_t> ||
                             std::is_same_v<ValueType, unsigned long> ||
                             std::is_same_v<ValueType, unsigned long long>) {
            // All 64-bit types (signed and unsigned) use bind_int64
            return stmt.bind_int64(param_index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<ValueType, short> || std::is_same_v<ValueType, unsigned short> ||
                             std::is_same_v<ValueType, unsigned int>) {
            return stmt.bind_int(param_index, static_cast<int>(value));
        }
        // Char types (stored as INTEGER)
        else if constexpr (std::is_same_v<ValueType, signed char> || std::is_same_v<ValueType, unsigned char> ||
                           std::is_same_v<ValueType, char>) {
            return stmt.bind_int(param_index, static_cast<int>(value));
        }
        // Enum types (stored as INTEGER via underlying type)
        else if constexpr (std::is_enum_v<ValueType>) {
            using Underlying = std::underlying_type_t<ValueType>;
            if constexpr (sizeof(Underlying) <= sizeof(int)) {
                return stmt.bind_int(param_index, static_cast<int>(static_cast<Underlying>(value)));
            } else {
                return stmt.bind_int64(param_index, static_cast<int64_t>(static_cast<Underlying>(value)));
            }
        }
        // Floating point types
        else if constexpr (std::is_same_v<ValueType, double>) {
            return stmt.bind_double(param_index, value);
        } else if constexpr (std::is_same_v<ValueType, float>) {
            return stmt.bind_double(param_index, static_cast<double>(value));
        }
        // Chrono date type (stored as TEXT "YYYY-MM-DD")
        else if constexpr (std::is_same_v<ValueType, std::chrono::year_month_day>) {
            auto str = chrono_conv::ymd_to_string(value);
            return stmt.bind_text(param_index, std::string_view{str});
        }
        // Chrono datetime type (stored as TEXT "YYYY-MM-DD HH:MM:SS")
        else if constexpr (std::is_same_v<ValueType, std::chrono::system_clock::time_point>) {
            auto str = chrono_conv::tp_to_string(value);
            return stmt.bind_text(param_index, std::string_view{str});
        }
        // Chrono duration types (stored as INTEGER — raw count)
        else if constexpr (is_chrono_duration_v<ValueType>) {
            return stmt.bind_int64(param_index, static_cast<int64_t>(value.count()));
        }
        // Filesystem path (stored as TEXT)
        else if constexpr (std::is_same_v<ValueType, std::filesystem::path>) {
            auto str = value.string();
            return stmt.bind_text(param_index, std::string_view{str});
        }
        // BLOB types (std::vector<uint8_t>)
        else if constexpr (std::is_same_v<ValueType, std::vector<uint8_t>> ||
                           std::is_same_v<ValueType, std::vector<unsigned char>>) {
            if (value.empty()) {
                return stmt.bind_blob(param_index, nullptr, 0);
            }
            return stmt.bind_blob(param_index, value.data(), value.size());
        }
        // BLOB type (std::vector<std::byte>)
        else if constexpr (std::is_same_v<ValueType, std::vector<std::byte>>) {
            if (value.empty()) {
                return stmt.bind_blob(param_index, nullptr, 0);
            }
            return stmt.bind_blob(param_index, value.data(), value.size());
        }
        // UUID type (stored as TEXT) — auto-generate if empty, validate if provided
        else if constexpr (std::is_same_v<ValueType, UUID>) {
            if (value.value.empty()) {
                auto generated = UUID::generate();
                return stmt.bind_text(param_index, std::string_view{generated.value});
            }
            if (!UUID::is_valid(value.value)) {
                return std::unexpected(ErrorType{-1, std::format("Invalid UUID format: '{}'", value.value)});
            }
            return stmt.bind_text(param_index, std::string_view{value.value});
        }
        // String types (must be last to avoid matching everything)
        else if constexpr (std::is_convertible_v<ValueType, std::string_view>) {
            return stmt.bind_text(param_index, std::string_view{value});
        } else {
            static_assert(
                    std::is_same_v<ValueType, int> || std::is_same_v<ValueType, int64_t> ||
                            std::is_same_v<ValueType, double> || std::is_same_v<ValueType, bool> ||
                            std::is_convertible_v<ValueType, std::string_view>,
                    "Unsupported field type for binding. Supported types: "
                    "int, int64_t, long, short, char, unsigned variants, enum, "
                    "double, float, bool, std::string, std::string_view, "
                    "chrono::year_month_day, chrono::time_point, chrono::duration, "
                    "filesystem::path, UUID, std::optional<T>, "
                    "std::vector<uint8_t>, std::vector<std::byte>"
            );
            // Unreachable due to static_assert, but needed for return type
            return std::unexpected(ErrorType{});
        }
    }

    // ============================================================================
    // SQL String Building Utilities
    // ============================================================================

    // Compile-time string utility for SQL generation and constexpr parsing
    template <size_t N> struct ConstexprString {
        std::array<char, N> data{};
        size_t              len = 0;

        constexpr ConstexprString() = default;

        explicit constexpr ConstexprString(const char* str) {
            size_t i = 0;
            while (str[i] != '\0' && i < N - 1) {
                data[i] = str[i];
                ++i;
            }
            len       = i;
            data[len] = '\0';
        }

        // LCOV_EXCL_START — constexpr-only methods: evaluated at compile time, no runtime code generated
        // Read access
        constexpr std::string_view view() const {
            return {data.data(), len};
        }
        constexpr const char* c_str() const {
            return data.data();
        }
        constexpr bool empty() const {
            return len == 0;
        }

        constexpr bool operator==(std::string_view other) const {
            return view() == other;
        }
        constexpr bool operator!=(std::string_view other) const {
            return view() != other;
        }

        constexpr auto append(const char* str) -> void {
            size_t i = 0;
            while (str[i] != '\0' && len < N - 1) {
                data[len] = str[i];
                ++len;
                ++i;
            }
            data[len] = '\0';
        }

        constexpr auto append(const std::string_view& str) -> void {
            for (char c : str) {
                if (len < N - 1) {
                    data[len] = c;
                    ++len;
                }
            }
            data[len] = '\0';
        }

        // Append another ConstexprString
        template <size_t M> constexpr auto append(const ConstexprString<M>& other) -> void {
            for (size_t i = 0; i < other.len && len < N - 1; ++i) {
                data[len] = other.data[i];
                ++len;
            }
            data[len] = '\0';
        }

        // Operator+= overloads (for convenient syntax)
        constexpr auto operator+=(const char* str) -> ConstexprString& {
            append(str);
            return *this;
        }

        constexpr auto operator+=(const std::string_view& str) -> ConstexprString& {
            append(str);
            return *this;
        }

        template <size_t M> constexpr auto operator+=(const ConstexprString<M>& other) -> ConstexprString& {
            append(other);
            return *this;
        }

        // Append a single digit (0-9) for compile-time number formatting
        constexpr auto append_digit(size_t digit) -> void {
            if (digit <= numeric::MAX_SINGLE_DIGIT && len < N - 1) {
                data[len] = '0' + static_cast<char>(digit);
                ++len;
                data[len] = '\0';
            }
        }
        // LCOV_EXCL_STOP

        // Runtime conversion to std::string
        operator std::string() const {
            return std::string(data.data(), len);
        }
    };

    // Generic thread-local SQL cache template
    template <typename KeyType = size_t, size_t CACHE_SIZE = buffer_size::CACHE_DEFAULT> struct SQLCache {
        struct Entry {
            KeyType     key{};
            std::string sql;
        };

        std::array<Entry, CACHE_SIZE> entries{};
        size_t                        next_slot = 0; // For round-robin replacement

        auto find(const KeyType& key) const -> const std::string* {
            for (const auto& entry : entries) {
                if (entry.key == key && !entry.sql.empty()) {
                    return &entry.sql;
                }
            }
            return nullptr;
        }

        auto insert(KeyType key, std::string sql) -> void {
            // Try to find empty slot first
            for (auto& entry : entries) {
                if (entry.key == KeyType{} && entry.sql.empty()) {
                    entry.key = std::move(key);
                    entry.sql = std::move(sql);
                    return;
                }
            }
            entries[next_slot].key = std::move(key);
            entries[next_slot].sql = std::move(sql);
            next_slot              = (next_slot + 1) % CACHE_SIZE;
        }

        auto clear() -> void {
            for (auto& entry : entries) {
                entry.key = KeyType{};
                entry.sql.clear();
            }
            next_slot = 0;
        }
    };

    // Specialized type aliases for common use cases
    using BulkSQLCache = SQLCache<size_t, buffer_size::CACHE_DEFAULT>;

} // namespace storm::orm::utilities