module;

export module storm_orm_utilities;

import <array>;
import <string>;
import <string_view>;
import <utility>;

export namespace storm::orm::utilities {

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