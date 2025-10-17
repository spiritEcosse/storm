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
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized(
                std::optional<size_t> limit  = std::nullopt,
                std::optional<size_t> offset = std::nullopt
        ) noexcept -> std::expected<std::vector<T>, Error> {
            return execute_simple_select(limit, offset);
        }

        // Optimized SELECT execution with JOIN (type-erased wrapper with compile-time SQL)
        // NOTE: join_wrapper is passed by value (lightweight - just 3 function pointers)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized(
                JoinStatementWrapper  join_wrapper,
                std::optional<size_t> limit  = std::nullopt,
                std::optional<size_t> offset = std::nullopt
        ) noexcept -> std::expected<std::vector<T>, Error> {
            return execute_with_join_impl(join_wrapper, limit, offset);
        }

      private:
        // Simple SELECT execution (dynamic SQL with connection-level caching)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_simple_select(
                std::optional<size_t> limit,
                std::optional<size_t> offset
        ) noexcept
                -> std::expected<std::vector<T>, Error> {
            // OPTIMIZATION: Build SQL dynamically, only add LIMIT/OFFSET when requested
            // Connection-level prepare_cached() handles statement caching automatically
            // SQLite requires LIMIT if OFFSET is used, so use "LIMIT -1" for offset-only
            std::string sql = get_select_sql();
            if (limit.has_value()) {
                sql += " LIMIT " + std::to_string(*limit);
            } else if (offset.has_value()) {
                // OFFSET without LIMIT: use LIMIT -1 (means "no limit" in SQLite)
                sql += " LIMIT -1";
            }
            if (offset.has_value()) {
                sql += " OFFSET " + std::to_string(*offset);
            }

            // Use connection-level caching (prepare_cached maintains internal cache)
            auto prepare_result = conn_.prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt = *prepare_result;

            // OPTIMIZATION: For LIMIT queries, use reserve() + emplace_back() to avoid
            // pre-constructing objects that may not be needed
            // For large queries without LIMIT, use resize() for pre-allocation
            std::vector<T> results;

            if (limit.has_value() && *limit <= 1000) {
                // Small LIMIT: use reserve() + emplace_back() (construct on-demand)
                results.reserve(*limit);

                int step_result;
                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                    T obj{};  // Default construct
                    extract_all_columns_inline_fast(stmt, obj);
                    results.push_back(std::move(obj));
                }

                if (step_result != Statement::NO_MORE_ROWS) {
                    stmt->reset();
                    return std::unexpected(Error{step_result, stmt->get_error_message()});
                }
            } else {
                // Large LIMIT or no LIMIT: use resize() for pre-allocation
                if (limit.has_value()) {
                    results.resize(std::min(*limit, size_t(10000)));
                } else {
                    results.resize(1000);
                }

                int step_result;
                size_t row_count = 0;
                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE &&
                       row_count < results.size()) {
                    T& obj = results[row_count];
                    extract_all_columns_inline_fast(stmt, obj);
                    row_count++;
                }

                // Handle overflow with exponential growth
                while (step_result == Statement::ROW_AVAILABLE) {
                    if (row_count >= results.size()) {
                        size_t new_size = results.size() * 2;
                        results.resize(new_size);
                    }

                    T& obj = results[row_count];
                    extract_all_columns_inline_fast(stmt, obj);
                    row_count++;
                    step_result = stmt->step_raw();
                }

                results.resize(row_count);

                if (step_result != Statement::NO_MORE_ROWS) {
                    stmt->reset();
                    return std::unexpected(Error{step_result, stmt->get_error_message()});
                }
            }

            stmt->reset();
            return results;
        }

        // JOIN execution with compile-time SQL (type-erased wrapper)
        // OPTIMIZATION: Uses pre-computed complete SQL from JoinStatement (zero runtime concatenation)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_with_join_impl(
                JoinStatementWrapper  join_wrapper,
                std::optional<size_t> limit,
                std::optional<size_t> offset
        ) noexcept -> std::expected<std::vector<T>, Error> {
            // OPTIMIZATION: Build SQL dynamically, only add LIMIT/OFFSET when requested
            // Use pre-computed complete SQL (generated at compile-time)
            // SQLite requires LIMIT if OFFSET is used, so use "LIMIT -1" for offset-only
            std::string join_sql = join_wrapper.get_complete_sql();
            if (limit.has_value()) {
                join_sql += " LIMIT " + std::to_string(*limit);
            } else if (offset.has_value()) {
                // OFFSET without LIMIT: use LIMIT -1 (means "no limit" in SQLite)
                join_sql += " LIMIT -1";
            }
            if (offset.has_value()) {
                join_sql += " OFFSET " + std::to_string(*offset);
            }

            // Use connection-level caching
            auto prepare_result = conn_.prepare_cached(join_sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt = *prepare_result;

            // OPTIMIZATION: Use resize() for pre-allocation (no intermediate copies)
            // For LIMIT queries, pre-allocate exact size if LIMIT is small
            // For JOIN without LIMIT, start with smaller size to avoid over-allocation
            std::vector<T> results;
            if (limit.has_value()) {
                // LIMIT specified - use exact size or cap at 10000
                results.resize(std::min(*limit, size_t(10000)));
            } else {
                // No LIMIT - start with 1000 and let overflow handling grow as needed
                results.resize(1000);
            }

            int step_result;
            size_t row_count = 0;
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE &&
                   row_count < results.size()) {
                T& obj = results[row_count];
                join_wrapper.extract_row(stmt, &obj);
                row_count++;
            }

            // Handle overflow with exponential growth
            while (step_result == Statement::ROW_AVAILABLE) {
                if (row_count >= results.size()) {
                    size_t new_size = results.size() * 2;
                    results.resize(new_size);
                }

                T& obj = results[row_count];
                join_wrapper.extract_row(stmt, &obj);
                row_count++;
                step_result = stmt->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
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

        Connection& conn_;
        // No statement-level caching - rely on connection-level prepare_cached() instead
        // This keeps code simple and SQL queries clean (no LIMIT/OFFSET when not needed)
    };

} // namespace storm::orm::statements