module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_select;

import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_utilities;
import storm_orm_where_expr;
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
        explicit SelectStatement(Connection& conn) : conn_(conn) {}

        // Optimized SELECT execution without JOIN
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized() noexcept
                -> std::expected<std::vector<T>, Error> {
            return execute_simple_select();
        }

        // Optimized SELECT execution with JOIN (type-erased wrapper with compile-time SQL)
        // NOTE: join_wrapper is passed by value (lightweight - just 3 function pointers)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_optimized(JoinStatementWrapper join_wrapper) noexcept -> std::expected<std::vector<T>, Error> {
            return execute_with_join_impl(join_wrapper);
        }

        // SELECT with WHERE clause (without JOIN)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_with_where(std::shared_ptr<orm::where::Expression> where_expr) noexcept
                -> std::expected<std::vector<T>, Error> {
            return execute_where_impl(where_expr);
        }

        // SELECT with WHERE clause and JOIN
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_with_where_and_join(JoinStatementWrapper join_wrapper,
                                    std::shared_ptr<orm::where::Expression> where_expr) noexcept
                -> std::expected<std::vector<T>, Error> {
            return execute_where_join_impl(join_wrapper, where_expr);
        }

      private:
        // Simple SELECT execution (uses unified query loop)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_simple_select() noexcept
                -> std::expected<std::vector<T>, Error> {
            // Cache statement on first use
            if (!cached_select_stmt_) {
                auto prepare_result = conn_.prepare_cached(get_select_sql());
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_select_stmt_ = *prepare_result;
            }

            // Use unified query loop with fast extraction
            return execute_query_loop(cached_select_stmt_,
                [](Statement* stmt, T& obj) {
                    extract_all_columns_inline_fast(stmt, obj);
                });
        }

        // JOIN execution with compile-time SQL (uses unified query loop)
        // OPTIMIZATION: Uses pre-computed complete SQL from JoinStatement (zero runtime concatenation)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_with_join_impl(JoinStatementWrapper join_wrapper) noexcept
                -> std::expected<std::vector<T>, Error> {
            // OPTIMIZATION: Use pre-computed complete SQL (generated at compile-time)
            // Eliminates all runtime string concatenation and heap allocation
            const std::string& join_sql = join_wrapper.get_complete_sql();

            // Cache JOIN statement separately (different SQL than simple SELECT)
            if (!cached_join_stmt_) {
                auto prepare_result = conn_.prepare_cached(join_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_join_stmt_ = *prepare_result;
            }

            // Use unified query loop with JOIN extraction
            return execute_query_loop(cached_join_stmt_,
                [&join_wrapper](Statement* stmt, T& obj) {
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
                    constexpr auto fk_pk_member = Base::template find_fk_primary_key<FieldType>();
                    using PKType                = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;

                    // Extract PK value using shared utility
                    obj.[:member:].[:fk_pk_member:] = Base::template extract_column_value<PKType>(*stmt, Index);
                    // Other FK fields remain default-initialized
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
        execute_query_loop(Statement* stmt, ExtractFunc&& extract_func) noexcept
                -> std::expected<std::vector<T>, Error> {
            // OPTIMIZATION: Use reserve() + emplace_back() instead of resize()
            // For small result sets, resize() wastes time constructing/destroying unused objects
            // For large result sets, reserve with exponential growth amortizes allocations
            std::vector<T> results;
            results.reserve(100);  // Initial capacity for typical queries

#ifdef STORM_PROFILE_WHERE
            auto loop_start = std::chrono::high_resolution_clock::now();
            size_t step_count = 0;
            size_t extract_count = 0;
#endif

            int step_result;
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
#ifdef STORM_PROFILE_WHERE
                step_count++;
#endif
                // Construct object on-demand
                results.emplace_back();
                extract_func(stmt, results.back());
#ifdef STORM_PROFILE_WHERE
                extract_count++;
#endif
            }

#ifdef STORM_PROFILE_WHERE
            auto loop_end = std::chrono::high_resolution_clock::now();
            auto loop_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(loop_end - loop_start).count();

            static thread_local long long total_loop_ns = 0;
            static thread_local long long total_steps = 0;
            static thread_local long long total_extracts = 0;
            static thread_local int loop_calls = 0;

            total_loop_ns += loop_ns;
            total_steps += step_count;
            total_extracts += extract_count;
            loop_calls++;

            if (loop_calls == 100) {
                std::cerr << "=== Query Loop Details (100 calls) ===\n";
                std::cerr << "Total loop time:   " << (total_loop_ns / 100) << " ns avg\n";
                std::cerr << "Avg rows/query:    " << (total_steps / 100) << "\n";
                std::cerr << "Time per step():   " << (total_steps > 0 ? total_loop_ns / total_steps : 0) << " ns\n";
                std::cerr << "Time per extract:  " << (total_extracts > 0 ? total_loop_ns / total_extracts : 0) << " ns\n";
                std::cerr << "======================================\n\n";

                total_loop_ns = 0;
                total_steps = 0;
                total_extracts = 0;
                loop_calls = 0;
            }
#endif

            // Error handling
            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        // SELECT with WHERE clause (no JOIN) - uses unified query loop
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_where_impl(std::shared_ptr<orm::where::Expression> where_expr) noexcept
                -> std::expected<std::vector<T>, Error> {
#ifdef STORM_PROFILE_WHERE
            auto t0 = std::chrono::high_resolution_clock::now();
#endif
            // Generate WHERE clause SQL from expression
            std::string where_sql;
            where_sql.reserve(get_select_sql().size() + 7 + 100); // Pre-allocate for typical WHERE clause
            where_sql = get_select_sql();
            where_sql.append(" WHERE ");
            where_sql.append(where_expr->to_sql());
#ifdef STORM_PROFILE_WHERE
            auto t1 = std::chrono::high_resolution_clock::now();
            auto sql_gen_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
#endif

            // OPTIMIZATION: Cache WHERE statement if SQL matches previous query
            Statement* stmt_ptr = nullptr;
            if (cached_where_stmt_ && cached_where_sql_ == where_sql) {
                // Reuse cached statement for same WHERE clause
                stmt_ptr = cached_where_stmt_;
            } else {
                // Different WHERE clause or first query - prepare new statement
                auto prepare_result = conn_.prepare_cached(where_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_where_stmt_ = *prepare_result;
                cached_where_sql_ = std::move(where_sql);
                stmt_ptr = cached_where_stmt_;
            }
#ifdef STORM_PROFILE_WHERE
            auto t2 = std::chrono::high_resolution_clock::now();
            auto stmt_prep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
#endif

            // OPTIMIZATION: Direct parameter binding (eliminates std::variant overhead)
            int param_index = 1;
            auto bind_result = where_expr->bind_params_direct(stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
#ifdef STORM_PROFILE_WHERE
            auto t3 = std::chrono::high_resolution_clock::now();
            auto bind_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
#endif

            // Use unified query loop with fast extraction
            auto result = execute_query_loop(stmt_ptr,
                [](Statement* stmt, T& obj) {
                    extract_all_columns_inline_fast(stmt, obj);
                });
#ifdef STORM_PROFILE_WHERE
            auto t4 = std::chrono::high_resolution_clock::now();
            auto query_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count();
            auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t0).count();

            static thread_local long long total_sql_gen = 0;
            static thread_local long long total_stmt_prep = 0;
            static thread_local long long total_bind = 0;
            static thread_local long long total_query = 0;
            static thread_local int call_count = 0;

            total_sql_gen += sql_gen_ns;
            total_stmt_prep += stmt_prep_ns;
            total_bind += bind_ns;
            total_query += query_ns;
            call_count++;

            if (call_count == 100) {
                std::cerr << "\n=== WHERE Profiling (100 calls avg) ===\n";
                std::cerr << "SQL Generation:    " << (total_sql_gen / 100) << " ns\n";
                std::cerr << "Stmt Prep/Cache:   " << (total_stmt_prep / 100) << " ns\n";
                std::cerr << "Param Binding:     " << (total_bind / 100) << " ns\n";
                std::cerr << "Query Execution:   " << (total_query / 100) << " ns\n";
                std::cerr << "Total:             " << ((total_sql_gen + total_stmt_prep + total_bind + total_query) / 100) << " ns\n";
                std::cerr << "======================================\n\n";

                // Reset for next batch
                total_sql_gen = 0;
                total_stmt_prep = 0;
                total_bind = 0;
                total_query = 0;
                call_count = 0;
            }
#endif
            return result;
        }

        // SELECT with WHERE clause and JOIN - uses unified query loop
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_where_join_impl(JoinStatementWrapper join_wrapper,
                               std::shared_ptr<orm::where::Expression> where_expr) noexcept
                -> std::expected<std::vector<T>, Error> {
            // Generate WHERE clause SQL from expression
            std::string join_where_sql;
            join_where_sql.reserve(join_wrapper.get_complete_sql().size() + 7 + 100); // Pre-allocate
            join_where_sql = join_wrapper.get_complete_sql();
            join_where_sql.append(" WHERE ");
            join_where_sql.append(where_expr->to_sql());

            // OPTIMIZATION: Cache WHERE+JOIN statement if SQL matches previous query
            Statement* stmt_ptr = nullptr;
            if (cached_where_join_stmt_ && cached_where_join_sql_ == join_where_sql) {
                // Reuse cached statement for same WHERE+JOIN clause
                stmt_ptr = cached_where_join_stmt_;
            } else {
                // Different WHERE+JOIN clause or first query - prepare new statement
                auto prepare_result = conn_.prepare_cached(join_where_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_where_join_stmt_ = *prepare_result;
                cached_where_join_sql_ = std::move(join_where_sql);
                stmt_ptr = cached_where_join_stmt_;
            }

            // OPTIMIZATION: Direct parameter binding (eliminates std::variant overhead)
            int param_index = 1;
            auto bind_result = where_expr->bind_params_direct(stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }

            // Use unified query loop with JOIN extraction
            return execute_query_loop(stmt_ptr,
                [&join_wrapper](Statement* stmt, T& obj) {
                    join_wrapper.extract_row(stmt, &obj);
                });
        }

        Connection&        conn_;
        mutable Statement* cached_select_stmt_ = nullptr;      // Statement caching for simple SELECT
        mutable Statement* cached_join_stmt_   = nullptr;      // Separate cache for JOIN queries
        mutable Statement* cached_where_stmt_ = nullptr;       // Cache for WHERE without JOIN
        mutable Statement* cached_where_join_stmt_ = nullptr;  // Cache for WHERE + JOIN
        mutable std::string cached_where_sql_;                 // Remember last WHERE SQL for cache validation
        mutable std::string cached_where_join_sql_;            // Remember last WHERE+JOIN SQL for cache validation
    };

} // namespace storm::orm::statements