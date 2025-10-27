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
        // Helper to detect if a type is std::optional
        template <typename T2> struct is_optional_type : std::false_type {};

        template <typename T2> struct is_optional_type<std::optional<T2>> : std::true_type {};

        template <typename T2> static constexpr bool is_optional_type_v = is_optional_type<T2>::value;

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

                    // Extract PK value based on its type
                    if constexpr (std::is_same_v<PKType, int>) {
                        obj.[:member:].[:fk_pk_member:] = stmt->extract_int(Index);
                    } else if constexpr (std::is_same_v<PKType, int64_t>) {
                        obj.[:member:].[:fk_pk_member:] = stmt->extract_int64(Index);
                    } else if constexpr (std::is_same_v<PKType, long> || std::is_same_v<PKType, long long>) {
                        obj.[:member:].[:fk_pk_member:] = static_cast<PKType>(stmt->extract_int64(Index));
                    }
                    // Other fields remain default-initialized
                }
                // Handle std::optional types
                else if constexpr (is_optional_type_v<FieldType>) {
                    if (stmt->is_null(Index)) {
                        obj.[:member:] = std::nullopt;
                    } else {
                        using InnerType = typename FieldType::value_type;
                        InnerType temp_value;

                        // Extract based on inner type
                        if constexpr (std::is_same_v<InnerType, int>) {
                            temp_value = stmt->extract_int(Index);
                        } else if constexpr (std::is_same_v<InnerType, int64_t>) {
                            temp_value = stmt->extract_int64(Index);
                        } else if constexpr (std::is_same_v<InnerType, double>) {
                            temp_value = stmt->extract_double(Index);
                        } else if constexpr (std::is_same_v<InnerType, float>) {
                            temp_value = stmt->extract_float(Index);
                        } else if constexpr (std::is_same_v<InnerType, bool>) {
                            temp_value = stmt->extract_bool(Index);
                        } else if constexpr (std::is_same_v<InnerType, std::string>) {
                            const unsigned char* text = stmt->extract_text_ptr(Index);
                            if (text) {
                                int len = sqlite3_column_bytes(stmt->handle(), Index);
                                temp_value.assign(reinterpret_cast<const char*>(text), len);
                            } else {
                                temp_value = std::string();
                            }
                        }

                        // OPTIMIZATION: Move temp_value to avoid copy
                        obj.[:member:] = std::move(temp_value);
                    }
                }
                // Boolean type (stored as INTEGER 0/1)
                else if constexpr (std::is_same_v<FieldType, bool>) {
                    obj.[:member:] = stmt->extract_bool(Index);
                }
                // Integer types
                else if constexpr (std::is_same_v<FieldType, int>) {
                    obj.[:member:] = stmt->extract_int(Index);
                } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                     std::is_same_v<FieldType, long long>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int64(Index));
                } else if constexpr (std::is_same_v<FieldType, uint64_t> || std::is_same_v<FieldType, unsigned long> ||
                                     std::is_same_v<FieldType, unsigned long long>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int64(Index));
                } else if constexpr (std::is_same_v<FieldType, short> || std::is_same_v<FieldType, unsigned short> ||
                                     std::is_same_v<FieldType, unsigned int>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int(Index));
                }
                // Floating point types
                else if constexpr (std::is_same_v<FieldType, double>) {
                    obj.[:member:] = stmt->extract_double(Index);
                } else if constexpr (std::is_same_v<FieldType, float>) {
                    obj.[:member:] = stmt->extract_float(Index);
                }
                // BLOB types
                else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                                   std::is_same_v<FieldType, std::vector<unsigned char>>) {
                    auto [blob_ptr, blob_size] = stmt->extract_blob(Index);
                    if (blob_ptr && blob_size > 0) {
                        const uint8_t* data = static_cast<const uint8_t*>(blob_ptr);
                        obj.[:member:]      = std::vector<uint8_t>(data, data + blob_size);
                    } else {
                        obj.[:member:].clear();
                    }
                }
                // String types
                else if constexpr (std::is_same_v<FieldType, std::string>) {
                    const unsigned char* text = stmt->extract_text_ptr(Index);
                    if (text) {
                        // OPTIMIZATION: Direct assign with known length (no temporary, no strlen)
                        // Avoids temporary string construction and leverages SQLite's length info
                        int len = sqlite3_column_bytes(stmt->handle(), Index);
                        obj.[:member:].assign(reinterpret_cast<const char*>(text), len);
                    } else {
                        obj.[:member:].clear();
                    }
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

        // Helper to bind WHERE parameters
        [[nodiscard]] auto bind_where_params(Statement* stmt, const std::vector<orm::where::ParamValue>& where_params) noexcept
                -> std::expected<void, Error> {
            for (size_t i = 0; i < where_params.size(); ++i) {
                auto result = std::visit([stmt, param_idx = static_cast<int>(i + 1)](const auto& value)
                        -> std::expected<void, Error> {
                    using ValueType = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<ValueType, std::nullptr_t>) {
                        return stmt->bind_null(param_idx);
                    } else if constexpr (std::is_same_v<ValueType, bool>) {
                        return stmt->bind_int(param_idx, value ? 1 : 0);
                    } else if constexpr (std::is_same_v<ValueType, int>) {
                        return stmt->bind_int(param_idx, value);
                    } else if constexpr (std::is_same_v<ValueType, int64_t> ||
                                       std::is_same_v<ValueType, long> ||
                                       std::is_same_v<ValueType, long long>) {
                        return stmt->bind_int64(param_idx, static_cast<int64_t>(value));
                    } else if constexpr (std::is_same_v<ValueType, uint64_t> ||
                                       std::is_same_v<ValueType, unsigned long> ||
                                       std::is_same_v<ValueType, unsigned long long>) {
                        return stmt->bind_int64(param_idx, static_cast<int64_t>(value));
                    } else if constexpr (std::is_same_v<ValueType, short> ||
                                       std::is_same_v<ValueType, unsigned short> ||
                                       std::is_same_v<ValueType, unsigned int>) {
                        return stmt->bind_int(param_idx, static_cast<int>(value));
                    } else if constexpr (std::is_same_v<ValueType, double>) {
                        return stmt->bind_double(param_idx, value);
                    } else if constexpr (std::is_same_v<ValueType, float>) {
                        return stmt->bind_double(param_idx, static_cast<double>(value));
                    } else if constexpr (std::is_same_v<ValueType, std::string> ||
                                       std::is_same_v<ValueType, std::string_view>) {
                        return stmt->bind_text(param_idx, std::string_view{value});
                    }
                    return {};
                }, where_params[i]);

                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // SELECT with WHERE clause (no JOIN)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_where_impl(std::shared_ptr<orm::where::Expression> where_expr) noexcept
                -> std::expected<std::vector<T>, Error> {
            // Generate WHERE clause SQL from expression
            std::string where_sql;
            where_sql.reserve(get_select_sql().size() + 7 + 100); // Pre-allocate for typical WHERE clause
            where_sql = get_select_sql();
            where_sql.append(" WHERE ");
            where_sql.append(where_expr->to_sql());

            // Collect parameters from expression tree
            std::vector<orm::where::ParamValue> params;
            where_expr->collect_params(params);

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

            // Bind WHERE parameters
            auto bind_result = bind_where_params(stmt_ptr, params);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            // Execute and extract results (same as execute_simple_select)
            std::vector<T> results;
            results.resize(10000);

            int step_result;
            size_t row_count = 0;
            while ((step_result = stmt_ptr->step_raw()) == Statement::ROW_AVAILABLE &&
                   row_count < results.size()) {
                T& obj = results[row_count];
                extract_all_columns_inline_fast(stmt_ptr, obj);
                row_count++;
            }

            // Handle overflow with exponential growth
            while (step_result == Statement::ROW_AVAILABLE) {
                if (row_count >= results.size()) {
                    size_t new_size = results.size() * 2;
                    results.resize(new_size);
                }

                T& obj = results[row_count];
                extract_all_columns_inline_fast(stmt_ptr, obj);
                row_count++;
                step_result = stmt_ptr->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt_ptr->reset();
                return std::unexpected(Error{step_result, stmt_ptr->get_error_message()});
            }

            stmt_ptr->reset();
            return results;
        }

        // SELECT with WHERE clause and JOIN
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

            // Collect parameters from expression tree
            std::vector<orm::where::ParamValue> params;
            where_expr->collect_params(params);

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

            // Bind WHERE parameters
            auto bind_result = bind_where_params(stmt_ptr, params);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            // Execute and extract results with JOIN extraction
            std::vector<T> results;
            results.resize(10000);

            int step_result;
            size_t row_count = 0;
            while ((step_result = stmt_ptr->step_raw()) == Statement::ROW_AVAILABLE &&
                   row_count < results.size()) {
                T& obj = results[row_count];
                join_wrapper.extract_row(stmt_ptr, &obj);
                row_count++;
            }

            // Handle overflow
            while (step_result == Statement::ROW_AVAILABLE) {
                if (row_count >= results.size()) {
                    size_t new_size = results.size() * 2;
                    results.resize(new_size);
                }

                T& obj = results[row_count];
                join_wrapper.extract_row(stmt_ptr, &obj);
                row_count++;
                step_result = stmt_ptr->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt_ptr->reset();
                return std::unexpected(Error{step_result, stmt_ptr->get_error_message()});
            }

            stmt_ptr->reset();
            return results;
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