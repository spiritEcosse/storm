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
import <string_view>;
import <concepts>;
import <format>;
import <meta>;
import <array>;
import <vector>;
import <type_traits>;
import <optional>;
import <memory>;
import <cstdint>;
import <memory>;
import <variant>;
import <chrono>;
import <iostream>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::ConstexprString;

    // Statement class for ORM select operations
    template <typename T, storm::db::DatabaseConnection ConnType> class SelectStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

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

      public:
        // Public access to SELECT SQL
        static const std::string& get_select_sql_static() {
            return select_sql_string;
        }

      private:
        // Generate SELECT SQL string (compile-time computed, runtime accessible)
        static const std::string& get_select_sql() {
            return select_sql_string;
        }

      public:
        explicit SelectStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Optimized SELECT execution without JOIN
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized(
                const std::optional<int>&            limit            = std::nullopt,
                const std::optional<int>&            offset           = std::nullopt,
                const std::optional<OrderByWrapper>& order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            return execute_simple_select(limit, offset, order_by_wrapper);
        }

        // Optimized SELECT execution with JOIN (type-erased wrapper with compile-time SQL)
        // NOTE: join_wrapper is passed by value (lightweight - just 3 function pointers)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized(
                JoinStatementWrapper                 join_wrapper,
                const std::optional<int>&            limit            = std::nullopt,
                const std::optional<int>&            offset           = std::nullopt,
                const std::optional<OrderByWrapper>& order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            return execute_with_join_impl(join_wrapper, limit, offset, order_by_wrapper);
        }

        // SELECT with WHERE clause (without JOIN)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_with_where(
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            return execute_where_impl(where_expr, limit, offset, order_by_wrapper);
        }

        // SELECT with WHERE clause and JOIN
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_with_where_and_join(
                JoinStatementWrapper                    join_wrapper,
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            return execute_where_join_impl(join_wrapper, where_expr, limit, offset, order_by_wrapper);
        }

      private:
        // Simple SELECT execution (uses unified query loop)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_simple_select(
                const std::optional<int>&            limit            = std::nullopt,
                const std::optional<int>&            offset           = std::nullopt,
                const std::optional<OrderByWrapper>& order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            Statement* stmt_ptr = nullptr;

            // Check if we have ORDER BY/LIMIT/OFFSET - if so, build custom SQL
            if (order_by_wrapper.has_value() || limit.has_value() || offset.has_value()) {
                std::string sql = get_select_sql();
                append_order_by(sql, order_by_wrapper);
                append_limit_offset(sql, limit, offset);

                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                stmt_ptr = *prepare_result;
            } else {
                // No ORDER BY/LIMIT/OFFSET - use cached statement
                if (!cached_select_stmt_) {
                    auto prepare_result = conn_->prepare_cached(get_select_sql());
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_select_stmt_ = *prepare_result;
                }
                stmt_ptr = cached_select_stmt_;
            }

            // Use unified query loop with fast extraction
            return execute_query_loop(stmt_ptr, [](Statement* stmt, T& obj) {
                extract_all_columns_inline_fast(stmt, obj);
            });
        }

        // JOIN execution with compile-time SQL (uses unified query loop)
        // OPTIMIZATION: Uses pre-computed complete SQL from JoinStatement (zero runtime concatenation)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_with_join_impl(
                JoinStatementWrapper                 join_wrapper,
                const std::optional<int>&            limit            = std::nullopt,
                const std::optional<int>&            offset           = std::nullopt,
                const std::optional<OrderByWrapper>& order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            Statement* stmt_ptr = nullptr;

            // Check if we have ORDER BY/LIMIT/OFFSET - if so, build custom SQL
            if (order_by_wrapper.has_value() || limit.has_value() || offset.has_value()) {
                std::string sql = join_wrapper.get_complete_sql();
                append_order_by(sql, order_by_wrapper);
                append_limit_offset(sql, limit, offset);

                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                stmt_ptr = *prepare_result;
            } else {
                // No ORDER BY/LIMIT/OFFSET - use cached JOIN statement
                if (!cached_join_stmt_) {
                    auto prepare_result = conn_->prepare_cached(join_wrapper.get_complete_sql());
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_join_stmt_ = *prepare_result;
                }
                stmt_ptr = cached_join_stmt_;
            }

            // Use unified query loop with JOIN extraction
            return execute_query_loop(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) {
                join_wrapper.extract_row(stmt, &obj);
            });
        }

      private:
        // OPTIMIZATION: Fast column extraction using shared BaseStatement utility
        // Compiler inlines extract_column_value across modules for zero overhead
        template <size_t Index>
        __attribute__((always_inline)) static inline void extract_column_inline_fast(Statement* stmt, T& obj) noexcept {
            if constexpr (Index < Base::field_count_) {
                constexpr auto member = Base::all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;

                // Handle FK fields - populate only the primary key
                if constexpr (Base::is_fk_field(member)) {
                    // FIX: Default-construct FK object first to ensure all fields are zero-initialized
                    obj.[:member:] = FieldType{};

                    constexpr auto fk_pk_member = Base::template find_fk_primary_key<FieldType>();
                    using PKType                = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;

                    // Extract PK value using shared utility
                    obj.[:member:].[:fk_pk_member:] = Base::template extract_column_value<PKType>(*stmt, Index);
                    // Other FK fields are now properly default-initialized (0 for int, "" for string, etc.)
                }
                // All other types: use shared extraction utility from BaseStatement
                else {
                    obj.[:member:] = Base::template extract_column_value<FieldType>(*stmt, Index);
                }
            }
        }

        // OPTIMIZATION: Fast extraction wrapper with fold expression
        template <size_t... Is>
        __attribute__((always_inline)) static inline void
        extract_all_columns_inline_fast_impl(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            // Direct extraction without error checking using comma operator fold
            ((extract_column_inline_fast<Is>(stmt, obj)), ...);
        }

        // OPTIMIZATION: Fast extraction entry point
        __attribute__((always_inline)) static inline void
        extract_all_columns_inline_fast(Statement* stmt, T& obj) noexcept {
            extract_all_columns_inline_fast_impl(stmt, obj, typename Base::field_indices_t{});
        }

        // Unified query execution loop - eliminates code duplication across all execution paths
        // Takes extraction function as template parameter for zero-overhead abstraction
        template <typename ExtractFunc>
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_query_loop(Statement* stmt, ExtractFunc&& extract_func) noexcept -> std::expected<plf::hive<T>, Error> {
            // plf::hive OPTIMIZATION: Stable pointers + fast insertion/iteration
            // - No reallocation overhead (multi-block architecture)
            // - Superior cache locality during iteration vs std::list/std::deque
            // - Optimal for scenarios with frequent insertions during result processing
            plf::hive<T> results;

            int step_result = 0;

            // Simple loop: insert directly into hive
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                T obj;
                extract_func(stmt, obj);
                results.insert(std::move(obj));
            }

            // Error handling
            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
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

        // Helper: Build WHERE clause SQL by appending to base SQL
        [[nodiscard]] __attribute__((always_inline)) static inline std::string
        build_where_sql(const std::string& base_sql, const orm::where::ExpressionVariantPtr& where_expr) {
            std::string where_sql;
            where_sql.reserve(base_sql.size() + 7 + 100); // Pre-allocate for " WHERE " + typical WHERE clause
            where_sql = base_sql;
            where_sql += " WHERE ";
            where_sql += orm::where::to_sql(*where_expr);
            return where_sql;
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

        // SELECT with WHERE clause (no JOIN) - uses unified query loop
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_where_impl(
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            // Generate WHERE clause SQL from expression using helper
            // TODO: looks strange , why do we need this part, i guess we can add cached_where_stmt_ to queryset reset
            // then main issue is : how to avoid create string everytime in storm ? i  guess i know
            // what if
            // Statement* stmt_ptr = nullptr;
            //   if (cached_where_stmt_) {
            // will be first check if not then create a string and check it as is
            std::string where_sql = build_where_sql(get_select_sql(), where_expr);
            append_order_by(where_sql, order_by_wrapper);
            append_limit_offset(where_sql, limit, offset);

            // OPTIMIZATION: Cache WHERE statement if SQL matches previous query
            Statement* stmt_ptr = nullptr;
            if (cached_where_stmt_ && cached_where_sql_ == where_sql) {
                // Reuse cached statement for same WHERE clause
                stmt_ptr = cached_where_stmt_;
            } else {
                // Different WHERE clause or first query - prepare new statement
                auto prepare_result = conn_->prepare_cached(where_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_where_stmt_ = *prepare_result;
                cached_where_sql_  = std::move(where_sql);
                stmt_ptr           = cached_where_stmt_;
            }

            // OPTIMIZATION: Direct parameter binding using helper
            auto bind_result = bind_where_params(stmt_ptr, where_expr);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            // Use unified query loop with fast extraction
            auto result = execute_query_loop(stmt_ptr, [](Statement* stmt, T& obj) {
                extract_all_columns_inline_fast(stmt, obj);
            });
            return result;
        }

        // SELECT with WHERE clause and JOIN - uses unified query loop
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_where_join_impl(
                JoinStatementWrapper                    join_wrapper,
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) noexcept -> std::expected<plf::hive<T>, Error> {
            // Generate WHERE clause SQL from expression using helper
            std::string join_where_sql = build_where_sql(join_wrapper.get_complete_sql(), where_expr);
            append_order_by(join_where_sql, order_by_wrapper);
            append_limit_offset(join_where_sql, limit, offset);

            // OPTIMIZATION: Cache WHERE+JOIN statement if SQL matches previous query
            Statement* stmt_ptr = nullptr;
            if (cached_where_join_stmt_ && cached_where_join_sql_ == join_where_sql) {
                // Reuse cached statement for same WHERE+JOIN clause
                stmt_ptr = cached_where_join_stmt_;
            } else {
                // Different WHERE+JOIN clause or first query - prepare new statement
                auto prepare_result = conn_->prepare_cached(join_where_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_where_join_stmt_ = *prepare_result;
                cached_where_join_sql_  = std::move(join_where_sql);
                stmt_ptr                = cached_where_join_stmt_;
            }

            // OPTIMIZATION: Direct parameter binding using helper
            auto bind_result = bind_where_params(stmt_ptr, where_expr);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            // Use unified query loop with JOIN extraction
            return execute_query_loop(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) {
                join_wrapper.extract_row(stmt, &obj);
            });
        }

        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_select_stmt_     = nullptr; // Statement caching for simple SELECT
        mutable Statement*        cached_join_stmt_       = nullptr; // Separate cache for JOIN queries
        mutable Statement*        cached_where_stmt_      = nullptr; // Cache for WHERE without JOIN
        mutable Statement*        cached_where_join_stmt_ = nullptr; // Cache for WHERE + JOIN
        mutable std::string       cached_where_sql_;                 // Remember last WHERE SQL for cache validation
        mutable std::string       cached_where_join_sql_; // Remember last WHERE+JOIN SQL for cache validation
    };

} // namespace storm::orm::statements