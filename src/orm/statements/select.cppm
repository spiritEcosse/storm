module;

#include <sqlite3.h>
#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_statements_select;

import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
import storm_orm_utilities;
import storm_orm_where;
import storm_db_concept;
import storm_db_sqlite;

import <expected>;
import <string>;
import <vector>;
import <type_traits>;
import <optional>;
import <memory>;
import <cstdint>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::ConstexprString;

    // Statement class for ORM select operations
    template <typename T, storm::db::DatabaseConnection ConnType> class SelectStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Compile-time SQL size calculation for SELECT statement
        static consteval size_t calculate_select_sql_size() {
            size_t size = 0;
            size += 7; // "SELECT "
            size += Base::calculate_field_names_size();
            size += 6; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT SQL at compile-time using ConstexprString
        static consteval auto build_select_sql_array() {
            constexpr size_t          sql_size = calculate_select_sql_size() + 50; // Add buffer for safety
            ConstexprString<sql_size> result;

            result.append("SELECT ");
            result.append(Base::build_all_field_names_list());
            result.append(" FROM ");
            result.append(Base::table_name_);

            return result;
        }

        // Pre-computed SELECT SQL generated at compile-time
        static constexpr auto           select_sql_array  = build_select_sql_array();
        static inline const std::string select_sql_string = std::string(select_sql_array);

        // Generate SELECT SQL string (compile-time computed, runtime accessible)
        static const std::string& get_select_sql() {
            return select_sql_string;
        }

      public:
        explicit SelectStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Invalidate dynamic query cache
        // Call this when QuerySet::reset() is invoked to ensure fresh query on next execute
        void invalidate_cache() noexcept {
            cached_stmt_ = nullptr;
            cached_sql_.clear();
        }

        // Unified SELECT execution - handles all combinations of JOIN and WHERE
        // All query logic inlined for maximum performance and code simplicity
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute(std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt) noexcept
                -> std::expected<plf::hive<T>, Error> {
            Statement* stmt_ptr = nullptr;

            // Fast path: simple SELECT with no modifiers (static SQL, compile-time constant)
            const bool is_simple = !where_expr && !join_wrapper && !order_by_wrapper.has_value() &&
                                   !limit.has_value() && !offset.has_value();

            if (is_simple) {
                if (!cached_simple_stmt_) {
                    auto prepare_result = conn_->prepare_cached(get_select_sql());
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_simple_stmt_ = *prepare_result;
                }
                stmt_ptr = cached_simple_stmt_;
            } else {
                // Dynamic path: build SQL and validate cache
                std::string sql = join_wrapper ? join_wrapper->get_complete_sql() : std::string(get_select_sql());

                if (where_expr) {
                    sql += " WHERE ";
                    sql += orm::where::to_sql(*where_expr);
                }
                append_order_by(sql, order_by_wrapper);
                append_limit_offset(sql, limit, offset);

                // Check cache - reuse statement if SQL matches
                if (cached_stmt_ && cached_sql_ == sql) {
                    stmt_ptr = cached_stmt_;
                } else {
                    auto prepare_result = conn_->prepare_cached(sql);
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_stmt_ = *prepare_result;
                    cached_sql_  = std::move(sql);
                    stmt_ptr     = cached_stmt_;
                }

                // Bind WHERE parameters if present
                if (where_expr) {
                    auto bind_result = bind_where_params(stmt_ptr, where_expr);
                    if (!bind_result) [[unlikely]] {
                        return std::unexpected(bind_result.error());
                    }
                }
            }

            // Execute query with appropriate extractor
            if (join_wrapper) {
                return execute_query_loop(stmt_ptr, [&join_wrapper](sqlite3_stmt*, Statement* s, T& obj) {
                    join_wrapper->extract_row(s, &obj);
                });
            }
            return execute_query_loop(stmt_ptr, [](sqlite3_stmt* raw, Statement*, T& obj) {
                extract_all_columns_raw(raw, obj);
            });
        }

      private:
        // =====================================================================
        // RAW POINTER EXTRACTION - Eliminates unique_ptr::get() overhead
        // These functions take sqlite3_stmt* directly for maximum performance
        // =====================================================================

        // Extract single column value using raw sqlite3_stmt* pointer
        template <typename FieldType>
        __attribute__((always_inline)) static inline FieldType
        extract_column_raw(sqlite3_stmt* raw_stmt, int col_idx) noexcept {
            // Handle std::optional types first
            if constexpr (storm::orm::utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (sqlite3_column_type(raw_stmt, col_idx) == SQLITE_NULL) {
                    return std::nullopt;
                }
                return FieldType{extract_column_raw<InnerType>(raw_stmt, col_idx)};
            }
            // Handle BLOB types (vector<uint8_t>)
            else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                               std::is_same_v<FieldType, std::vector<unsigned char>>) {
                const void* blob = sqlite3_column_blob(raw_stmt, col_idx);
                const int   size = sqlite3_column_bytes(raw_stmt, col_idx);
                if (blob && size > 0) {
                    const auto* data = static_cast<const uint8_t*>(blob);
                    return FieldType(data, data + size);
                }
                return FieldType{};
            }
            // Boolean type (stored as INTEGER 0/1)
            else if constexpr (std::is_same_v<FieldType, bool>) {
                return sqlite3_column_int(raw_stmt, col_idx) != 0;
            }
            // Integer types
            else if constexpr (std::is_same_v<FieldType, int>) {
                return sqlite3_column_int(raw_stmt, col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long>) {
                return static_cast<FieldType>(sqlite3_column_int64(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, uint64_t> || std::is_same_v<FieldType, unsigned long> ||
                                 std::is_same_v<FieldType, unsigned long long>) {
                return static_cast<FieldType>(sqlite3_column_int64(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, short>) {
                return static_cast<short>(sqlite3_column_int(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, unsigned short>) {
                return static_cast<unsigned short>(sqlite3_column_int(raw_stmt, col_idx));
            } else if constexpr (std::is_same_v<FieldType, unsigned int>) {
                return static_cast<unsigned int>(sqlite3_column_int(raw_stmt, col_idx));
            }
            // Floating point types
            else if constexpr (std::is_same_v<FieldType, double>) {
                return sqlite3_column_double(raw_stmt, col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                return static_cast<float>(sqlite3_column_double(raw_stmt, col_idx));
            }
            // String type
            else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = sqlite3_column_text(raw_stmt, col_idx);
                if (text) {
                    const auto len = static_cast<size_t>(sqlite3_column_bytes(raw_stmt, col_idx));
                    return std::string(reinterpret_cast<const char*>(text), len);
                }
                return std::string{};
            } else {
                // Fallback for other types
                return FieldType{};
            }
        }

        // Extract single column using raw pointer
        template <size_t Index>
        __attribute__((always_inline)) static inline void
        extract_column_raw_fast(sqlite3_stmt* raw_stmt, T& obj) noexcept {
            if constexpr (Index < Base::field_count_) {
                constexpr auto member = Base::all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;

                // Handle FK fields - populate only the primary key
                if constexpr (Base::is_fk_field(member)) {
                    obj.[:member:]                  = FieldType{};
                    constexpr auto fk_pk_member     = Base::template find_fk_primary_key<FieldType>();
                    using PKType                    = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;
                    obj.[:member:].[:fk_pk_member:] = extract_column_raw<PKType>(raw_stmt, Index);
                } else {
                    obj.[:member:] = extract_column_raw<FieldType>(raw_stmt, Index);
                }
            }
        }

        // Extract all columns using raw pointer with fold expression
        template <size_t... Is>
        __attribute__((always_inline)) static inline void
        extract_all_columns_raw_impl(sqlite3_stmt* raw_stmt, T& obj, std::index_sequence<Is...>) noexcept {
            ((extract_column_raw_fast<Is>(raw_stmt, obj)), ...);
        }

        // Fast extraction entry point using raw pointer
        __attribute__((always_inline)) static inline void
        extract_all_columns_raw(sqlite3_stmt* raw_stmt, T& obj) noexcept {
            extract_all_columns_raw_impl(raw_stmt, obj, typename Base::field_indices_t{});
        }

        // =====================================================================
        // QUERY LOOPS
        // =====================================================================

        // Unified query loop template - handles both direct and JOIN extraction
        // OPTIMIZATION: Caches sqlite3_stmt* once and uses direct SQLite calls in hot loop
        // Extractor signature: void(sqlite3_stmt* raw, Statement* stmt, T& obj)
        //   - Direct extraction uses raw pointer for max performance
        //   - JOIN extraction uses Statement* for type-erased access
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_query_loop(Statement* stmt, Extractor&& extractor) noexcept -> std::expected<plf::hive<T>, Error> {
            plf::hive<T>  results;
            sqlite3_stmt* raw_stmt    = stmt->handle(); // Cache raw pointer ONCE
            int           step_result = 0;

            while ((step_result = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
                T obj;
                extractor(raw_stmt, stmt, obj);
                results.insert(std::move(obj));
            }

            if (step_result != SQLITE_DONE) {
                sqlite3_reset(raw_stmt);
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        // Helper: Append ORDER BY clause to SQL from wrapper
        // NOTE: ORDER BY must come before LIMIT/OFFSET in SQLite
        __attribute__((always_inline)) static inline void
        append_order_by(std::string& sql, const std::optional<OrderByWrapper>& order_by_wrapper) {
            if (order_by_wrapper.has_value() && !order_by_wrapper->empty()) {
                sql += order_by_wrapper->get_order_by_sql();
            }
        }

        // Helper: Append LIMIT/OFFSET clauses to SQL
        // NOTE: SQLite requires LIMIT when using OFFSET, so we use LIMIT -1 (meaning unlimited) when OFFSET is used
        // alone
        __attribute__((always_inline)) static inline void
        append_limit_offset(std::string& sql, const std::optional<int>& limit, const std::optional<int>& offset) {
            if (limit.has_value()) {
                sql += " LIMIT ";
                sql += std::to_string(limit.value());
            } else if (offset.has_value()) {
                // SQLite requires LIMIT when using OFFSET
                sql += " LIMIT -1";
            }

            if (offset.has_value()) {
                sql += " OFFSET ";
                sql += std::to_string(offset.value());
            }
        }

        // Helper: Bind WHERE expression parameters to statement
        [[nodiscard]] __attribute__((always_inline)) static inline auto
        bind_where_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            int  param_index = 1;
            auto bind_result = orm::where::bind_params_direct(*where_expr, stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
            return {};
        }

        std::shared_ptr<ConnType> conn_;

        // Cache for simple SELECT (static SQL, compile-time constant)
        mutable Statement* cached_simple_stmt_ = nullptr;

        // Cache for dynamic queries (WHERE, JOIN, modifiers) - validated by SQL comparison
        mutable Statement*  cached_stmt_ = nullptr;
        mutable std::string cached_sql_;
    };

} // namespace storm::orm::statements