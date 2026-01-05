module;

#include <sqlite3.h>
#include <meta>
#include <cassert>

export module storm_orm_statements_join;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_db_concept;

import <expected>;
import <string>;
import <vector>;
import <utility>;
import <meta>;
import <type_traits>;
import <array>;
import <optional>;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    enum class JoinType { Inner, Left, Right };

    // Type alias for type-erased pointers used in polymorphic JOIN wrapper.
    // void* is intentional here: JoinStatementWrapper must work with any model type T
    // without knowing T at compile time. The actual T* conversion happens in the
    // function pointers stored in make_join_wrapper().
    using ErasedObjectPtr    = void*;
    using ErasedStatementPtr = void*;

    struct JoinStatementWrapper {
        auto (*get_join_sql_fn)() -> const std::string&;
        auto (*get_select_fields_fn)() -> const std::string&;
        auto (*get_complete_sql_fn)() -> const std::string&;
        auto (*extract_row_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void;
        auto (*extract_row_raw_fn)(sqlite3_stmt*, ErasedObjectPtr) -> void;

        [[nodiscard]] auto to_sql() const -> const std::string& {
            return get_join_sql_fn();
        }

        [[nodiscard]] auto build_qualified_select_fields() const -> const std::string& {
            return get_select_fields_fn();
        }

        // NEW: Get complete pre-computed SELECT...JOIN SQL
        [[nodiscard]] auto get_complete_sql() const -> const std::string& {
            return get_complete_sql_fn();
        }

        auto extract_row(ErasedStatementPtr stmt, ErasedObjectPtr obj) const -> void {
            extract_row_fn(stmt, obj);
        }

        // Extract row using raw sqlite3_stmt* pointer (faster)
        auto extract_row_raw(sqlite3_stmt* raw_stmt, ErasedObjectPtr obj) const -> void {
            extract_row_raw_fn(raw_stmt, obj);
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, auto... FKFieldPtrs>
        requires(sizeof...(FKFieldPtrs) >= 1)
    class JoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t fk_count_ = sizeof...(FKFieldPtrs);

        // C++26: Direct pack indexing
        template <size_t Idx> using FK_type = std::remove_cvref_t<decltype(std::declval<T>().*FKFieldPtrs...[Idx])>;

        template <size_t Idx> using FKBase_at = BaseStatement<FK_type<Idx>>;

        // Compile-time validation
        static_assert(
                (std::is_member_object_pointer_v<decltype(FKFieldPtrs)> && ...),
                "FK pointers must be member object pointers"
        );

        static consteval auto count_non_fk_fields() -> size_t {
            size_t count = 0;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (!Base::is_fk_field(Base::all_members_[i])) {
                    count++;
                }
            }
            return count;
        }

        static constexpr size_t non_fk_field_count_ = count_non_fk_fields();

        // Constexpr storage for column offsets
        static constexpr auto calculate_column_offsets() {
            std::array<size_t, fk_count_> offsets{};
            size_t                        current_offset = non_fk_field_count_;
            size_t                        idx            = 0;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) -> void {
                ((offsets[idx++] = current_offset, current_offset += FKBase_at<Is>::field_count_), ...);
            }(std::make_index_sequence<fk_count_>{});

            return offsets;
        }

        static constexpr auto column_offsets_ = calculate_column_offsets();

        static consteval auto build_fk_field_names() {
            std::array<std::string_view, fk_count_> names{};
            size_t                                  fk_idx = 0;

            for (size_t member_idx = 0; member_idx < Base::field_count_ && fk_idx < fk_count_; ++member_idx) {
                auto member = Base::all_members_[member_idx];
                if (Base::is_fk_field(member)) {
                    names[fk_idx++] = std::meta::identifier_of(member);
                }
            }
            return names;
        }

        static constexpr auto fk_field_names_ = build_fk_field_names();

        static constexpr auto get_join_keyword() -> std::string_view {
            if constexpr (Type == JoinType::Inner) {
                return " INNER JOIN ";
            } else if constexpr (Type == JoinType::Left) {
                return " LEFT JOIN ";
            } else {
                return " RIGHT JOIN ";
            }
        }

        // Compile-time SQL generation with ConstexprString
        static consteval auto calculate_join_sql_size() -> size_t {
            using utilities::sql_len::ON_EQUALS;
            using utilities::sql_len::SMALL_BUFFER;
            // NOLINTNEXTLINE(misc-const-correctness) - total IS modified in fold expression below
            size_t total = 0;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) -> void {
                ((total += get_join_keyword().size() + FKBase_at<Is>::table_name_.size() + 2 + 1 + 4 + 1 + 1 + 1 +
                           FKBase_at<Is>::pk_name_.size() + ON_EQUALS + fk_field_names_[Is].size() + 3),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + SMALL_BUFFER;
        }

        static consteval auto build_join_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) -> void {
                ((result.append(get_join_keyword()),
                  result.append(FKBase_at<Is>::table_name_),
                  result.append(" t"),
                  result.append_digit(Is + 2),
                  result.append(" ON t"),
                  result.append_digit(Is + 2),
                  result.append("."),
                  result.append(FKBase_at<Is>::pk_name_),
                  result.append(" = t1."),
                  result.append(fk_field_names_[Is]),
                  result.append("_id")),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        static consteval auto calculate_select_fields_size() -> size_t {
            size_t total = 0;

            // Base fields
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    total += (total > 0 ? 2 : 0) + 3 + std::meta::identifier_of(member).size();
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) -> void {
                (
                        [&]<size_t I>() -> void {
                            total += 2; // ", "
                            [&]<size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) -> void {
                                ((total += (FieldIs > 0 ? 2 : 0) + 3 + 1 +
                                           std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs]).size()),
                                 ...);
                            }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                        }.template operator()<Is>(),
                        ...
                );
            }(std::make_index_sequence<fk_count_>{});

            return total + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_select_fields_array() {
            constexpr size_t             fields_size = calculate_select_fields_size();
            ConstexprString<fields_size> result;

            // Base fields
            bool first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    if (!first) {
                        result.append(", ");
                    }
                    result.append("t1.");
                    result.append(std::meta::identifier_of(member));
                    first = false;
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) -> void {
                (
                        [&]<size_t I>() -> void {
                            result.append(", ");
                            [&]<size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) -> void {
                                // NOLINTNEXTLINE(misc-const-correctness) - first_in_table IS modified in fold expression
                                bool first_in_table = true;
                                (((first_in_table ? (void)0 : result.append(", ")),
                                  result.append("t"),
                                  result.append_digit(I + 2),
                                  result.append("."),
                                  result.append(std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs])),
                                  first_in_table = false),
                                 ...);
                            }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                        }.template operator()<Is>(),
                        ...
                );
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        static constexpr auto join_sql_array      = build_join_sql_array();
        static constexpr auto select_fields_array = build_select_fields_array();

        // NEW: Compile-time complete SQL generation
        static consteval auto calculate_complete_sql_size() -> size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT;
            using utilities::sql_len::SMALL_BUFFER;
            size_t total = 0;
            total += SELECT; // "SELECT "
            total += calculate_select_fields_size();
            total += FROM; // " FROM "
            total += Base::table_name_.size();
            total += 3; // " t1"
            total += calculate_join_sql_size();
            return total + SMALL_BUFFER;
        }

        static consteval auto build_complete_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_complete_sql_size();
            ConstexprString<sql_size> result;

            result.append("SELECT ");
            result.append(select_fields_array);
            result.append(" FROM ");
            result.append(Base::table_name_);
            result.append(" t1");
            result.append(join_sql_array);

            return result;
        }

        static constexpr auto complete_sql_array = build_complete_sql_array();

        // Lazy initialization to avoid duplicate storage
        static auto get_join_sql_cached() -> const std::string& {
            static const std::string str{join_sql_array.data.data(), join_sql_array.len};
            return str;
        }

        static auto get_select_fields_cached() -> const std::string& {
            static const std::string str{select_fields_array.data.data(), select_fields_array.len};
            return str;
        }

        // NEW: Get complete SQL with lazy initialization
        static auto get_complete_sql_cached() -> const std::string& {
            static const std::string str{complete_sql_array.data.data(), complete_sql_array.len};
            return str;
        }

      public:
        static auto get_join_sql() -> const std::string& {
            return get_join_sql_cached();
        }

        static auto get_select_fields() -> const std::string& {
            return get_select_fields_cached();
        }

        // NEW: Get complete pre-computed SELECT...JOIN SQL
        static auto get_complete_sql() -> const std::string& {
            return get_complete_sql_cached();
        }

        // =====================================================================
        // RAW POINTER EXTRACTION - Maximum performance with direct SQLite calls
        // =====================================================================

        // Extract field using raw sqlite3_stmt* pointer (no wrapper overhead)
        template <typename FieldType>
        __attribute__((always_inline)) static void
        extract_typed_field_raw(sqlite3_stmt* raw_stmt, FieldType& field, int col_idx) noexcept {
            // Handle std::optional types first
            if constexpr (storm::orm::utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (sqlite3_column_type(raw_stmt, col_idx) == SQLITE_NULL) {
                    field = std::nullopt;
                    return;
                }
                InnerType val;
                extract_typed_field_raw(raw_stmt, val, col_idx);
                field = std::move(val);
            }
            // Integer types
            else if constexpr (std::is_same_v<FieldType, int>) {
                field = sqlite3_column_int(raw_stmt, col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long>) {
                field = static_cast<FieldType>(sqlite3_column_int64(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, double>) {
                field = sqlite3_column_double(raw_stmt, col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                field = static_cast<float>(sqlite3_column_double(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                field = sqlite3_column_int(raw_stmt, col_idx) != 0;
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = sqlite3_column_text(raw_stmt, col_idx);
                if (text) {
                    int len = sqlite3_column_bytes(raw_stmt, col_idx);
                    field.assign(reinterpret_cast<const char*>(text), len);
                } else {
                    field.clear();
                }
            }
        }

        // Extract base model fields using raw pointer
        template <size_t MemberIdx>
        __attribute__((always_inline)) static void
        extract_column_at_raw(sqlite3_stmt* raw_stmt, T& obj, int& col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (!Base::is_fk_field(member)) {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                    extract_typed_field_raw(raw_stmt, obj.[:member:], col_idx);
                    col_idx++;
                }
            }
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields_raw(sqlite3_stmt* raw_stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            int col_idx = 0;
            ((extract_column_at_raw<Is>(raw_stmt, obj, col_idx)), ...);
        }

        // Extract FK fields using raw pointer
        template <typename FKBase, size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at_raw(sqlite3_stmt* raw_stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                using FieldType       = std::remove_cvref_t<decltype(fk_obj.[:member:])>;
                extract_typed_field_raw(raw_stmt, fk_obj.[:member:], col_idx);
            }
        }

        template <size_t FKIdx, size_t... FieldIs>
        __attribute__((always_inline)) static void extract_fk_fields_impl_raw(
                sqlite3_stmt*   raw_stmt,
                FK_type<FKIdx>& fk_obj,
                size_t          col_offset,
                std::index_sequence<FieldIs...> /*unused*/
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at_raw<FKBase, FieldIs>(raw_stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        template <size_t Idx>
        __attribute__((always_inline)) static void extract_fk_at_raw(sqlite3_stmt* raw_stmt, T& obj) noexcept {
            constexpr auto FKPtr  = FKFieldPtrs...[Idx];
            auto&          fk_obj = obj.*FKPtr;
            extract_fk_fields_impl_raw<Idx>(
                    raw_stmt, fk_obj, column_offsets_[Idx], std::make_index_sequence<FKBase_at<Idx>::field_count_>{}
            );
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks_raw(sqlite3_stmt* raw_stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (extract_fk_at_raw<Is>(raw_stmt, obj), ...);
        }

        // NEW: Raw pointer extraction for maximum performance
        __attribute__((hot)) __attribute__((flatten)) static auto
        extract_joined_row_raw(sqlite3_stmt* raw_stmt, T& obj) noexcept -> void {
            // Initialize ALL FK fields to defaults first
            init_all_fk_fields(obj, typename Base::field_indices_t{});
            // Extract base fields and FK fields using raw pointers
            extract_t_fields_raw(raw_stmt, obj, typename Base::field_indices_t{});
            extract_all_fks_raw(raw_stmt, obj, std::make_index_sequence<fk_count_>{});
        }

      private:
        // =====================================================================
        // WRAPPER-BASED EXTRACTION (Legacy, kept for compatibility)
        // =====================================================================

        // Enhanced type extraction with NULL handling (optimized for inlining)
        template <typename FieldType>
        __attribute__((always_inline)) static void
        extract_typed_field(Statement* stmt, FieldType& field, int col_idx) noexcept {
            if (stmt->is_null(col_idx)) {
                if constexpr (requires { field = std::nullopt; }) {
                    field = std::nullopt;
                }
                return;
            }

            if constexpr (std::is_same_v<FieldType, int>) {
                field = stmt->extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                field = stmt->extract_int64(col_idx);
            } else if constexpr (std::is_same_v<FieldType, double>) {
                field = stmt->extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                field = static_cast<bool>(stmt->extract_int(col_idx));
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    // OPTIMIZATION: Direct assign with known length (no temporary, no strlen)
                    // Avoids temporary string construction and leverages SQLite's length info
                    int len = sqlite3_column_bytes(stmt->handle(), col_idx);
                    field.assign(reinterpret_cast<const char*>(text), len);
                }
            } else if constexpr (requires { field.emplace(std::string{}); }) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    int len = sqlite3_column_bytes(stmt->handle(), col_idx);
                    field.emplace(reinterpret_cast<const char*>(text), len);
                }
            }
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            int col_idx = 0;
            ((extract_column_at<Is>(stmt, obj, col_idx)), ...);
        }

        template <size_t MemberIdx>
        __attribute__((always_inline)) static void extract_column_at(Statement* stmt, T& obj, int& col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return;
                } else {
                    extract_typed_field(stmt, obj.[:member:], col_idx);
                    col_idx++;
                }
            }
        }

        template <size_t FKIdx, size_t... FieldIs>
        __attribute__((always_inline)) static void extract_fk_fields_impl(
                Statement* stmt, FK_type<FKIdx>& fk_obj, size_t col_offset, std::index_sequence<FieldIs...> /*unused*/
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        template <typename FKBase, size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                extract_typed_field(stmt, fk_obj.[:member:], col_idx);
            }
        }

        template <size_t Idx>
        __attribute__((always_inline)) static void extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKPtr  = FKFieldPtrs...[Idx]; // C++26 pack indexing
            auto&          fk_obj = obj.*FKPtr;
            extract_fk_fields_impl<Idx>(
                    stmt, fk_obj, column_offsets_[Idx], std::make_index_sequence<FKBase_at<Idx>::field_count_>{}
            );
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

        // FIX: Initialize all FK fields to default values before extraction
        // This ensures non-JOINed FK fields have proper default values instead of garbage
        template <size_t MemberIdx> __attribute__((always_inline)) static void init_fk_field_at(T& obj) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (Base::is_fk_field(member)) {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                    obj.[:member:]  = FieldType{}; // Default-construct FK object
                }
            }
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        init_all_fk_fields(T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (init_fk_field_at<Is>(obj), ...);
        }

      public:
        // Database-agnostic API: Takes Statement* but delegates to raw extraction for performance
        // OPTIMIZATION: Cache raw pointer once and use the fast raw extraction path
        __attribute__((hot)) __attribute__((flatten)) static auto extract_joined_row(Statement* stmt, T& obj) noexcept
                -> void {
            // Get raw pointer once and delegate to the optimized raw extraction
            sqlite3_stmt* raw_stmt = stmt->handle();
            extract_joined_row_raw(raw_stmt, obj);
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, auto... FKFieldPtrs>
    [[nodiscard]] auto make_join_wrapper() -> JoinStatementWrapper {
        using JS = JoinStatement<T, ConnType, Type, FKFieldPtrs...>;

        return JoinStatementWrapper{
                +[]() -> const std::string& { return JS::get_join_sql(); },
                +[]() -> const std::string& { return JS::get_select_fields(); },
                +[]() -> const std::string& { return JS::get_complete_sql(); },
                +[](ErasedStatementPtr stmt, ErasedObjectPtr obj) -> void {
                    JS::extract_joined_row(static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj));
                },
                +[](sqlite3_stmt* raw_stmt, ErasedObjectPtr obj) -> void {
                    JS::extract_joined_row_raw(raw_stmt, *static_cast<T*>(obj));
                }
        };
    }

} // namespace storm::orm::statements
