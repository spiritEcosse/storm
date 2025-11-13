module;

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

export namespace storm::orm::utilities {

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
            } else {
                // Bind NULL for std::nullopt
                return stmt.bind_null(param_index);
            }
        }
        // Boolean type (stored as INTEGER 0/1)
        else if constexpr (std::is_same_v<ValueType, bool>) {
            return stmt.bind_int(param_index, value ? 1 : 0);
        }
        // Integer types
        else if constexpr (std::is_same_v<ValueType, int>) {
            return stmt.bind_int(param_index, value);
        } else if constexpr (std::is_same_v<ValueType, int64_t> || std::is_same_v<ValueType, long> ||
                             std::is_same_v<ValueType, long long>) {
            return stmt.bind_int64(param_index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<ValueType, uint64_t> || std::is_same_v<ValueType, unsigned long> ||
                             std::is_same_v<ValueType, unsigned long long>) {
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

        consteval void append(const char* str) {
            size_t i = 0;
            while (str[i] != '\0' && len < N - 1) {
                data[len] = str[i];
                ++len;
                ++i;
            }
            data[len] = '\0';
        }

        consteval void append(const std::string_view& str) {
            for (char c : str) {
                if (len < N - 1) {
                    data[len] = c;
                    ++len;
                }
            }
            data[len] = '\0';
        }

        // Append another ConstexprString
        template <size_t M> consteval void append(const ConstexprString<M>& other) {
            for (size_t i = 0; i < other.len && len < N - 1; ++i) {
                data[len] = other.data[i];
                ++len;
            }
            data[len] = '\0';
        }

        // Operator+= overloads (for convenient syntax)
        consteval ConstexprString& operator+=(const char* str) {
            append(str);
            return *this;
        }

        consteval ConstexprString& operator+=(const std::string_view& str) {
            append(str);
            return *this;
        }

        template <size_t M> consteval ConstexprString& operator+=(const ConstexprString<M>& other) {
            append(other);
            return *this;
        }

        // Append a single digit (0-9) for compile-time number formatting
        consteval void append_digit(size_t digit) {
            if (digit <= 9 && len < N - 1) {
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
    template <typename KeyType = size_t, size_t CACHE_SIZE = 8> struct SQLCache {
        struct Entry {
            KeyType     key{};
            std::string sql;
        };

        std::array<Entry, CACHE_SIZE> entries{};
        size_t                        next_slot = 0; // For round-robin replacement

        const std::string* find(const KeyType& key) const {
            for (const auto& entry : entries) {
                if (entry.key == key && !entry.sql.empty()) {
                    return &entry.sql;
                }
            }
            return nullptr;
        }

        void insert(KeyType key, std::string sql) {
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

        void clear() {
            for (auto& entry : entries) {
                entry.key = KeyType{};
                entry.sql.clear();
            }
            next_slot = 0;
        }
    };

    // Specialized type aliases for common use cases
    using BulkSQLCache = SQLCache<size_t, 8>;

    // Helper function for building SQL placeholders at compile-time
    template <size_t N> consteval ConstexprString<N> build_placeholders_string(size_t count) {
        ConstexprString<N> result;

        for (size_t i = 0; i < count; ++i) {
            if (i > 0) {
                result.append(", ");
            }
            result.append("?");
        }

        return result;
    }

    // Helper function for calculating placeholder string size
    consteval size_t calculate_placeholders_size(size_t count) {
        if (count == 0)
            return 1;                               // Null terminator
        return (count * 1) + ((count - 1) * 2) + 1; // count * "?" + (count-1) * ", " + null terminator
    }

    // Helper function for building SQL IN clause placeholders
    template <size_t N> consteval ConstexprString<N> build_in_clause_string(size_t count) {
        ConstexprString<N> result;
        result.append("(");

        for (size_t i = 0; i < count; ++i) {
            if (i > 0) {
                result.append(",");
            }
            result.append("?");
        }

        result.append(")");
        return result;
    }

    // Helper function for calculating IN clause size
    consteval size_t calculate_in_clause_size(size_t count) {
        if (count == 0)
            return 3;                                   // "()" + null terminator
        return 2 + (count * 1) + ((count - 1) * 1) + 1; // "()" + count * "?" + (count-1) * "," + null terminator
    }

} // namespace storm::orm::utilities