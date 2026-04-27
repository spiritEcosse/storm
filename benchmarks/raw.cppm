// storm_benchmark_raw
//
// Reflection-based raw SQLite helpers for benchmarks. Provides generic
// extract_row<T>() / bind_*<T>() that work with any ORM model so the raw
// SQLite benchmark path doesn't need per-model column mapping.
//
// Lives in a module so consumers can `import storm_benchmark_raw;` instead of
// textually including <sqlite3.h>. Keeping the C header in this module's GMF
// (preprocessed before any `import`) avoids the std-PCM macro-redefinition
// trap documented in benchmarks/.bak/runner.cppm.ref + issue #221.

module;

#include <sqlite3.h>

export module storm_benchmark_raw;

import storm;

import <cstddef>;
import <cstdint>;
import <meta>;
import <optional>;
import <string>;
import <type_traits>;
import <utility>;
import <vector>;

export namespace storm::benchmark {

    // ========================================================================
    // FK field detection (mirrors ORM's BaseStatement::is_fk_field)
    // ========================================================================
    consteval auto is_fk_field(std::meta::info member) -> bool {
        auto field_attr = std::meta::annotation_of_type<storm::meta::FieldAttr>(member);
        return field_attr.has_value() && field_attr.value() == storm::meta::FieldAttr::fk;
    }

    consteval auto is_pk_field(std::meta::info member) -> bool {
        auto field_attr = std::meta::annotation_of_type<storm::meta::FieldAttr>(member);
        return field_attr.has_value() && field_attr.value() == storm::meta::FieldAttr::primary;
    }

    // Find primary key member of a type
    template <typename T> consteval auto find_pk_member() -> std::meta::info {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (is_pk_field(m))
                return m;
        }
        std::unreachable();
    }

    // ========================================================================
    // Consteval member accessors — avoid storing full members vector as constexpr local
    // ========================================================================
    template <typename T> consteval auto member_count() -> size_t {
        return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
    }

    template <typename T, size_t I> consteval auto get_member() -> std::meta::info {
        return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())[I];
    }

    // ========================================================================
    // Type traits
    // ========================================================================
    template <typename T> struct is_optional : std::false_type {};
    template <typename T> struct is_optional<std::optional<T>> : std::true_type {};
    template <typename T> constexpr bool is_optional_v = is_optional<T>::value;

    template <typename T> struct is_vector_uint8 : std::false_type {};
    template <> struct is_vector_uint8<std::vector<uint8_t>> : std::true_type {};
    template <typename T> constexpr bool is_vector_uint8_v = is_vector_uint8<T>::value;

    template <typename T> struct is_vector_byte : std::false_type {};
    template <> struct is_vector_byte<std::vector<std::byte>> : std::true_type {};
    template <typename T> constexpr bool is_vector_byte_v = is_vector_byte<T>::value;

    // ========================================================================
    // extract_value<T> — extract a single column from sqlite3_stmt*
    // ========================================================================
    template <typename T> __attribute__((always_inline)) inline auto extract_value(sqlite3_stmt* stmt, int col) -> T {
        if constexpr (is_optional_v<T>) {
            using Inner = typename T::value_type;
            if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
                return std::nullopt;
            }
            return T{extract_value<Inner>(stmt, col)};
        } else if constexpr (std::is_same_v<T, bool>) {
            return sqlite3_column_int(stmt, col) != 0;
        } else if constexpr (std::is_same_v<T, int>) {
            return sqlite3_column_int(stmt, col);
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, long long>) {
            return static_cast<T>(sqlite3_column_int64(stmt, col));
        } else if constexpr (std::is_same_v<T, unsigned int> || std::is_same_v<T, short> ||
                             std::is_same_v<T, unsigned short>) {
            return static_cast<T>(sqlite3_column_int(stmt, col));
        } else if constexpr (std::is_same_v<T, double>) {
            return sqlite3_column_double(stmt, col);
        } else if constexpr (std::is_same_v<T, float>) {
            return static_cast<float>(sqlite3_column_double(stmt, col));
        } else if constexpr (std::is_same_v<T, std::string>) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return text ? std::string(text) : std::string{};
        } else if constexpr (is_vector_uint8_v<T>) {
            const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, col));
            int         len  = sqlite3_column_bytes(stmt, col);
            return blob ? T(blob, blob + len) : T{};
        } else if constexpr (is_vector_byte_v<T>) {
            const auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(stmt, col));
            int         len  = sqlite3_column_bytes(stmt, col);
            return blob ? T(blob, blob + len) : T{};
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for extract_value");
        }
    }

    // ========================================================================
    // extract_row<T> — extract all columns into a model using reflection
    // ========================================================================
    template <typename T> __attribute__((always_inline)) inline auto extract_row(sqlite3_stmt* stmt) -> T {
        T obj{};

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([]<size_t I>(sqlite3_stmt* s, T& o) __attribute__((always_inline)) {
                 constexpr auto member = get_member<T, I>();
                 using FieldType       = std::remove_cvref_t<decltype(o.[:member:])>;

                 if constexpr (is_fk_field(member)) {
                     o.[:member:]           = FieldType{};
                     constexpr auto fk_pk   = find_pk_member<FieldType>();
                     using PKType           = std::remove_cvref_t<decltype(o.[:member:].[:fk_pk:])>;
                     o.[:member:].[:fk_pk:] = extract_value<PKType>(s, I);
                 } else {
                     o.[:member:] = extract_value<FieldType>(s, I);
                 }
             }.template operator()<Is>(stmt, obj)),
             ...);
        }(std::make_index_sequence<member_count<T>()>{});

        return obj;
    }

    // ========================================================================
    // bind_value — bind a single value to sqlite3_stmt*
    // ========================================================================
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, int val) -> void {
        sqlite3_bind_int(stmt, idx, val);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, int64_t val) -> void {
        sqlite3_bind_int64(stmt, idx, val);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, double val) -> void {
        sqlite3_bind_double(stmt, idx, val);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, float val) -> void {
        sqlite3_bind_double(stmt, idx, static_cast<double>(val));
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, bool val) -> void {
        sqlite3_bind_int(stmt, idx, val ? 1 : 0);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, const std::string& val) -> void {
        sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, unsigned int val) -> void {
        sqlite3_bind_int(stmt, idx, static_cast<int>(val));
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, long long val) -> void {
        sqlite3_bind_int64(stmt, idx, val);
    }
    __attribute__((always_inline)) inline auto bind_value(sqlite3_stmt* stmt, int idx, const std::vector<uint8_t>& val)
            -> void {
        sqlite3_bind_blob(stmt, idx, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }
    __attribute__((always_inline)) inline auto
    bind_value(sqlite3_stmt* stmt, int idx, const std::vector<std::byte>& val) -> void {
        sqlite3_bind_blob(stmt, idx, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    // ========================================================================
    // bind_field — bind a single struct field (handles optional + FK)
    // ========================================================================
    template <std::meta::info Member, typename T>
    __attribute__((always_inline)) inline auto bind_field(sqlite3_stmt* stmt, const T& obj, int& idx) -> void {
        using FieldType = std::remove_cvref_t<decltype(obj.[:Member:])>;

        if constexpr (is_fk_field(Member)) {
            constexpr auto fk_pk = find_pk_member<FieldType>();
            bind_value(stmt, idx++, obj.[:Member:].[:fk_pk:]);
        } else if constexpr (is_optional_v<FieldType>) {
            if (obj.[:Member:].has_value()) {
                bind_value(stmt, idx++, obj.[:Member:].value());
            } else {
                sqlite3_bind_null(stmt, idx++);
            }
        } else {
            bind_value(stmt, idx++, obj.[:Member:]);
        }
    }

    // ========================================================================
    // bind_non_pk_fields<T> — bind all non-PK fields for INSERT
    // ========================================================================
    template <typename T>
    __attribute__((always_inline)) inline auto bind_non_pk_fields(sqlite3_stmt* stmt, const T& obj, int& idx) -> void {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([]<size_t I>(sqlite3_stmt* s, const T& o, int& i) __attribute__((always_inline)) {
                 constexpr auto member = get_member<T, I>();
                 if constexpr (!is_pk_field(member)) {
                     bind_field<member>(s, o, i);
                 }
             }.template operator()<Is>(stmt, obj, idx)),
             ...);
        }(std::make_index_sequence<member_count<T>()>{});
    }

    // ========================================================================
    // bind_all_fields<T> — bind all fields including PK (for UPDATE WHERE id=?)
    // ========================================================================
    template <typename T>
    __attribute__((always_inline)) inline auto bind_all_fields(sqlite3_stmt* stmt, const T& obj, int& idx) -> void {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((bind_field<get_member<T, Is>()>(stmt, obj, idx)), ...);
        }(std::make_index_sequence<member_count<T>()>{});
    }

    // ========================================================================
    // bind_update_fields<T> — bind non-PK fields + PK at end (for UPDATE SET ... WHERE id=?)
    // ========================================================================
    template <typename T>
    __attribute__((always_inline)) inline auto bind_update_fields(sqlite3_stmt* stmt, const T& obj, int& idx) -> void {
        bind_non_pk_fields(stmt, obj, idx);
        constexpr auto pk = find_pk_member<T>();
        bind_value(stmt, idx++, obj.[:pk:]);
    }

    // ========================================================================
    // non_pk_field_count<T> — count of non-PK fields (for INSERT placeholder count)
    // ========================================================================
    template <typename T> consteval auto non_pk_field_count() -> size_t {
        size_t count   = 0;
        auto   members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
        for (auto m : members) {
            if (!is_pk_field(m))
                ++count;
        }
        return count;
    }

} // namespace storm::benchmark
