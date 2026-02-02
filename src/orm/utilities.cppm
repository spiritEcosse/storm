module;

#include <meta>

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
    // Parameter Binding Utilities
    // ============================================================================

    // Helper trait to detect std::optional types
    template <typename T> struct is_optional : std::false_type {};
    template <typename TValue> struct is_optional<std::optional<TValue>> : std::true_type {};
    template <typename TValue> constexpr bool is_optional_v = is_optional<TValue>::value;

    // Generic parameter binding - unified implementation for WHERE and CRUD statements
    // No dependency on entity type T - pure type dispatch based on value type
    template <typename StmtType, typename ErrorType>
    [[nodiscard]] auto bind_parameter_value(StmtType& stmt, int param_index, const auto& value) noexcept
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
        // Floating point types
        else if constexpr (std::is_same_v<ValueType, double>) {
            return stmt.bind_double(param_index, value);
        } else if constexpr (std::is_same_v<ValueType, float>) {
            return stmt.bind_double(param_index, static_cast<double>(value));
        }
        // BLOB types (std::vector<uint8_t>)
        else if constexpr (std::is_same_v<ValueType, std::vector<uint8_t>> ||
                           std::is_same_v<ValueType, std::vector<unsigned char>>) {
            if (value.empty()) {
                return stmt.bind_blob(param_index, nullptr, 0);
            }
            return stmt.bind_blob(param_index, value.data(), value.size());
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
                    "int, int64_t, long, short, unsigned variants, "
                    "double, float, bool, std::string, std::string_view, "
                    "std::optional<T>, std::vector<uint8_t>"
            );
            // Unreachable due to static_assert, but needed for return type
            return std::unexpected(ErrorType{});
        }
    }

    // ============================================================================
    // SQL String Building Utilities
    // ============================================================================

    // Compile-time string utility for SQL generation
    template <size_t N> struct ConstexprString {
        std::array<char, N> data{};
        size_t              len = 0;

        consteval ConstexprString() = default;

        consteval ConstexprString(const char* str) {
            size_t i = 0;
            while (str[i] != '\0' && i < N - 1) {
                data[i] = str[i];
                ++i;
            }
            len       = i;
            data[len] = '\0';
        }

        consteval auto append(const char* str) -> void {
            size_t i = 0;
            while (str[i] != '\0' && len < N - 1) {
                data[len] = str[i];
                ++len;
                ++i;
            }
            data[len] = '\0';
        }

        consteval auto append(const std::string_view& str) -> void {
            for (char c : str) {
                if (len < N - 1) {
                    data[len] = c;
                    ++len;
                }
            }
            data[len] = '\0';
        }

        // Append another ConstexprString
        template <size_t M> consteval auto append(const ConstexprString<M>& other) -> void {
            for (size_t i = 0; i < other.len && len < N - 1; ++i) {
                data[len] = other.data[i];
                ++len;
            }
            data[len] = '\0';
        }

        // Operator+= overloads (for convenient syntax)
        consteval auto operator+=(const char* str) -> ConstexprString& {
            append(str);
            return *this;
        }

        consteval auto operator+=(const std::string_view& str) -> ConstexprString& {
            append(str);
            return *this;
        }

        template <size_t M> consteval auto operator+=(const ConstexprString<M>& other) -> ConstexprString& {
            append(other);
            return *this;
        }

        // Append a single digit (0-9) for compile-time number formatting
        consteval auto append_digit(size_t digit) -> void {
            if (digit <= numeric::MAX_SINGLE_DIGIT && len < N - 1) {
                data[len] = '0' + static_cast<char>(digit);
                ++len;
                data[len] = '\0';
            }
        }

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
            // Use round-robin replacement
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