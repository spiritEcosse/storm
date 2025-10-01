module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_select;

import storm_orm_statements_base;
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
            size += Base::field_names_.size();
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
            result.append(Base::field_names_);
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

        // Optimized SELECT execution with statement caching and inlined row extraction
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_optimized() noexcept
                -> std::expected<std::vector<T>, Error> {
            // Cache statement on first use (RemoveStatement pattern)
            if (!cached_select_stmt_) {
                auto prepare_result = conn_.prepare_cached(get_select_sql());
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_select_stmt_ = *prepare_result;
            }

            // OPTIMIZATION: Use resize() instead of reserve() + emplace_back()
            // Pre-constructing objects is significantly faster (6.08M vs 3.60M rows/sec)
            std::vector<T> results;
            results.resize(10000); // Pre-allocate with default-constructed objects

            // OPTIMIZATION: Simplified loop with direct row check using abstracted interface
            int    step_result;
            size_t row_count = 0;
            while ((step_result = cached_select_stmt_->step_raw()) == Statement::ROW_AVAILABLE
                   && row_count < results.size()) {
                // OPTIMIZATION: Write directly into pre-constructed object
                T& obj = results[row_count];

                // OPTIMIZATION: Direct column extraction without error checking
                // This is safe because we know the statement and table structure at compile time
                extract_all_columns_inline_fast(cached_select_stmt_, obj);

                row_count++;
            }

            // Handle case where we got more rows than pre-allocated
            while (step_result == Statement::ROW_AVAILABLE) {
                results.emplace_back();
                T& obj = results.back();
                extract_all_columns_inline_fast(cached_select_stmt_, obj);
                row_count++;
                step_result = cached_select_stmt_->step_raw();
            }

            // Trim vector to actual size
            results.resize(row_count);

            // Check for errors only after loop completes
            if (step_result != Statement::NO_MORE_ROWS) {
                cached_select_stmt_->reset();
                return std::unexpected(Error{step_result, cached_select_stmt_->get_error_message()});
            }

            // Reset statement for next use
            cached_select_stmt_->reset();

            return results;
        }

      private:
        // Helper to detect if a type is std::optional
        template <typename T2>
        struct is_optional_type : std::false_type {};

        template <typename T2>
        struct is_optional_type<std::optional<T2>> : std::true_type {};

        template <typename T2>
        static constexpr bool is_optional_type_v = is_optional_type<T2>::value;

        // OPTIMIZATION: Fast column extraction without error checking using abstracted interface
        template <size_t Index>
        __attribute__((always_inline)) static inline void
        extract_column_inline_fast(Statement* stmt, T& obj) noexcept {
            if constexpr (Index < Base::field_count_) {
                constexpr auto member = Base::all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;

                // Handle std::optional types first
                if constexpr (is_optional_type_v<FieldType>) {
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
                                temp_value = std::string(reinterpret_cast<const char*>(text));
                            } else {
                                temp_value = std::string();
                            }
                        }

                        obj.[:member:] = temp_value;
                    }
                }
                // Boolean type (stored as INTEGER 0/1)
                else if constexpr (std::is_same_v<FieldType, bool>) {
                    obj.[:member:] = stmt->extract_bool(Index);
                }
                // Integer types
                else if constexpr (std::is_same_v<FieldType, int>) {
                    obj.[:member:] = stmt->extract_int(Index);
                }
                else if constexpr (std::is_same_v<FieldType, int64_t> ||
                                  std::is_same_v<FieldType, long> ||
                                  std::is_same_v<FieldType, long long>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int64(Index));
                }
                else if constexpr (std::is_same_v<FieldType, uint64_t> ||
                                  std::is_same_v<FieldType, unsigned long> ||
                                  std::is_same_v<FieldType, unsigned long long>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int64(Index));
                }
                else if constexpr (std::is_same_v<FieldType, short> ||
                                  std::is_same_v<FieldType, unsigned short> ||
                                  std::is_same_v<FieldType, unsigned int>) {
                    obj.[:member:] = static_cast<FieldType>(stmt->extract_int(Index));
                }
                // Floating point types
                else if constexpr (std::is_same_v<FieldType, double>) {
                    obj.[:member:] = stmt->extract_double(Index);
                }
                else if constexpr (std::is_same_v<FieldType, float>) {
                    obj.[:member:] = stmt->extract_float(Index);
                }
                // BLOB types
                else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                                  std::is_same_v<FieldType, std::vector<unsigned char>>) {
                    auto [blob_ptr, blob_size] = stmt->extract_blob(Index);
                    if (blob_ptr && blob_size > 0) {
                        const uint8_t* data = static_cast<const uint8_t*>(blob_ptr);
                        obj.[:member:] = std::vector<uint8_t>(data, data + blob_size);
                    } else {
                        obj.[:member:].clear();
                    }
                }
                // String types
                else if constexpr (std::is_same_v<FieldType, std::string>) {
                    const unsigned char* text = stmt->extract_text_ptr(Index);
                    if (text) {
                        // OPTIMIZATION: Direct string construction is 2.2x faster than assign()
                        // 9.05M rows/sec vs 4.10M rows/sec in benchmarks
                        obj.[:member:] = std::string(reinterpret_cast<const char*>(text));
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

        Connection&        conn_;
        mutable Statement* cached_select_stmt_ = nullptr; // RemoveStatement-style statement caching
    };

} // namespace storm::orm::statements