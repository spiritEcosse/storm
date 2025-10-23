module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_select;

import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_utilities;
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

      private:
        // Simple SELECT execution (with object pooling optimization)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_simple_select() noexcept
                -> std::expected<std::vector<T>, Error> {
            // Cache statement on first use (RemoveStatement pattern)
            if (!cached_select_stmt_) {
                auto prepare_result = conn_.prepare_cached(get_select_sql());
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_select_stmt_ = *prepare_result;
            }

            // OPTIMIZATION: Use resize() for pre-allocation (no intermediate copies)
            // Pre-constructing objects is significantly faster (6.08M vs 3.60M rows/sec)
            std::vector<T> results;
            results.resize(10000);

            int step_result;
            size_t row_count = 0;
            while ((step_result = cached_select_stmt_->step_raw()) == Statement::ROW_AVAILABLE &&
                   row_count < results.size()) {
                // Extract directly into pre-constructed object (no copy)
                T& obj = results[row_count];
                extract_all_columns_inline_fast(cached_select_stmt_, obj);
                row_count++;
            }

            // Handle overflow with exponential growth
            while (step_result == Statement::ROW_AVAILABLE) {
                if (row_count >= results.size()) {
                    size_t new_size = results.size() * 2;
                    results.resize(new_size);
                }

                T& obj = results[row_count];
                extract_all_columns_inline_fast(cached_select_stmt_, obj);
                row_count++;
                step_result = cached_select_stmt_->step_raw();
            }

            results.resize(row_count);

            // Check for errors
            if (step_result != Statement::NO_MORE_ROWS) {
                cached_select_stmt_->reset();
                return std::unexpected(Error{step_result, cached_select_stmt_->get_error_message()});
            }

            cached_select_stmt_->reset();
            return results;
        }

        // JOIN execution with compile-time SQL (type-erased wrapper)
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

            // OPTIMIZATION: Use resize() for pre-allocation (no intermediate copies)
            std::vector<T> results;
            results.resize(10000);

            int step_result;
            size_t row_count = 0;
            while ((step_result = cached_join_stmt_->step_raw()) == Statement::ROW_AVAILABLE &&
                   row_count < results.size()) {
                T& obj = results[row_count];
                join_wrapper.extract_row(cached_join_stmt_, &obj);
                row_count++;
            }

            // Handle overflow with exponential growth
            while (step_result == Statement::ROW_AVAILABLE) {
                if (row_count >= results.size()) {
                    size_t new_size = results.size() * 2;
                    results.resize(new_size);
                }

                T& obj = results[row_count];
                join_wrapper.extract_row(cached_join_stmt_, &obj);
                row_count++;
                step_result = cached_join_stmt_->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                cached_join_stmt_->reset();
                return std::unexpected(Error{step_result, cached_join_stmt_->get_error_message()});
            }

            cached_join_stmt_->reset();
            return results;
        }

      private:
        // OPTIMIZATION: Fast column extraction without error checking using abstracted interface
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

        Connection&        conn_;
        mutable Statement* cached_select_stmt_ = nullptr; // Statement caching for simple SELECT
        mutable Statement* cached_join_stmt_   = nullptr; // Separate cache for JOIN queries
    };

} // namespace storm::orm::statements