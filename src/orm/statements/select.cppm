module;

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
        static consteval auto calculate_select_sql_size() -> size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT;
            size_t size = 0;
            size += SELECT; // "SELECT "
            size += Base::calculate_field_names_size();
            size += FROM; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT SQL at compile-time using ConstexprString
        static consteval auto build_select_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_select_sql_size() + utilities::sql_len::LARGE_BUFFER;
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
        static auto get_select_sql() -> const std::string& {
            return select_sql_string;
        }

      public:
        explicit SelectStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Invalidate dynamic query cache
        // Call this when QuerySet::reset() is invoked to ensure fresh query on next execute
        auto invalidate_cache() noexcept -> void {
            cached_stmt_ = nullptr;
            cached_sql_.clear();
        }

        // Unified SELECT execution - handles all combinations of JOIN and WHERE
        // Uses monadic and_then for cleaner error propagation
        // NOTE: The if/else branch is OUTSIDE the loop intentionally - checking inside would
        // add overhead per row (10k checks vs 1 check). Keep separate execute_query_loop calls.
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute(std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt)
                -> std::expected<plf::hive<T>, Error> {
            return prepare_statement(join_wrapper, where_expr, limit, offset, order_by_wrapper)
                    .and_then([this, &join_wrapper](Statement* stmt_ptr) -> std::expected<plf::hive<T>, Error> {
                        if (join_wrapper) {
                            return execute_query_loop(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) -> void {
                                join_wrapper->extract_row(stmt, &obj);
                            });
                        }
                        return execute_query_loop(stmt_ptr, [](Statement* stmt, T& obj) -> void {
                            extract_all_columns(stmt, obj);
                        });
                    });
        }

      private:
        // =====================================================================
        // STATEMENT PREPARATION - Unified caching for all query types
        // =====================================================================

        // Prepare and cache statement based on query type
        // Unified approach: simple SELECT uses dedicated cache, all others use string-based cache
        [[nodiscard]] __attribute__((always_inline)) auto prepare_statement(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> std::expected<Statement*, Error> {
            const bool has_modifiers = order_by_wrapper.has_value() || limit.has_value() || offset.has_value();

            // Fast path: simple SELECT (static SQL, no building needed)
            if (!where_expr && !join_wrapper && !has_modifiers) {
                if (cached_simple_stmt_ == nullptr) {
                    auto prepare_result = conn_->prepare_cached(get_select_sql());
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_simple_stmt_ = *prepare_result;
                }
                return cached_simple_stmt_;
            }

            // Dynamic path: build SQL and cache by string comparison
            // NOTE: Do NOT add sql.reserve() here - benchmarks show ~2% regression due to
            // extra function call overhead outweighing reallocation savings for typical SQL sizes
            std::string sql = join_wrapper ? join_wrapper->get_complete_sql() : std::string(get_select_sql());
            if (where_expr) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr);
            }
            Base::append_order_by(sql, order_by_wrapper);
            Base::append_limit_offset(sql, limit, offset);

            // Get or prepare cached statement
            if (cached_stmt_ == nullptr || cached_sql_ != sql) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(Error{-1, "Failed to prepare statement"});
                }
                cached_stmt_ = *prepare_result;
                cached_sql_  = std::move(sql);
            }

            // Bind WHERE params if needed
            if (where_expr) {
                auto bind_result = Base::template bind_where_params<Statement, Error>(cached_stmt_, where_expr);
                if (!bind_result) [[unlikely]] {
                    return std::unexpected(bind_result.error());
                }
            }

            return cached_stmt_;
        }

        // =====================================================================
        // DATABASE-AGNOSTIC EXTRACTION - Uses Statement methods with templates
        // Templates enable cross-module inlining for zero-cost abstraction
        // =====================================================================

        // Extract single column value using Statement methods (database-agnostic)
        template <typename FieldType>
        __attribute__((always_inline)) static auto extract_column(Statement* stmt, int col_idx) -> FieldType {
            // Handle std::optional types first
            if constexpr (storm::orm::utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (stmt->is_null(col_idx)) {
                    return std::nullopt;
                }
                return FieldType{extract_column<InnerType>(stmt, col_idx)};
            }
            // Handle BLOB types (vector<uint8_t>)
            else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                               std::is_same_v<FieldType, std::vector<unsigned char>>) {
                const void* blob = stmt->extract_blob_ptr(col_idx);
                const int   size = stmt->extract_bytes(col_idx);
                if (blob && size > 0) {
                    const auto* data = static_cast<const uint8_t*>(blob);
                    return FieldType(data, data + size);
                }
                return FieldType{};
            }
            // Boolean type (stored as INTEGER 0/1)
            else if constexpr (std::is_same_v<FieldType, bool>) {
                return stmt->extract_bool(col_idx);
            }
            // Integer types
            else if constexpr (std::is_same_v<FieldType, int>) {
                return stmt->extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long> || std::is_same_v<FieldType, uint64_t> ||
                                 std::is_same_v<FieldType, unsigned long> ||
                                 std::is_same_v<FieldType, unsigned long long>) {
                // All 64-bit types (signed and unsigned) use extract_int64
                return static_cast<FieldType>(stmt->extract_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, short>) {
                return static_cast<short>(stmt->extract_int(col_idx));
            } else if constexpr (std::is_same_v<FieldType, unsigned short>) {
                return static_cast<unsigned short>(stmt->extract_int(col_idx));
            } else if constexpr (std::is_same_v<FieldType, unsigned int>) {
                return static_cast<unsigned int>(stmt->extract_int(col_idx));
            }
            // Floating point types
            else if constexpr (std::is_same_v<FieldType, double>) {
                return stmt->extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                return stmt->extract_float(col_idx);
            }
            // String type
            else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    const auto len = static_cast<size_t>(stmt->extract_bytes(col_idx));
                    return std::string(reinterpret_cast<const char*>(text), len);
                }
                return std::string{};
            } else {
                // Fallback for other types
                return FieldType{};
            }
        }

        // Extract single column using Statement (database-agnostic)
        template <size_t Index>
        __attribute__((always_inline)) static void extract_column_fast(Statement* stmt, T& obj) noexcept {
            if constexpr (Index < Base::field_count_) {
                constexpr auto member = Base::all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;

                // Handle FK fields - populate only the primary key
                if constexpr (Base::is_fk_field(member)) {
                    obj.[:member:]                  = FieldType{};
                    constexpr auto fk_pk_member     = Base::template find_fk_primary_key<FieldType>();
                    using PKType                    = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;
                    obj.[:member:].[:fk_pk_member:] = extract_column<PKType>(stmt, Index);
                } else {
                    obj.[:member:] = extract_column<FieldType>(stmt, Index);
                }
            }
        }

        // Extract all columns using Statement with fold expression
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_columns_impl(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            ((extract_column_fast<Is>(stmt, obj)), ...);
        }

        // Fast extraction entry point using Statement (database-agnostic)
        __attribute__((always_inline)) static void extract_all_columns(Statement* stmt, T& obj) noexcept {
            extract_all_columns_impl(stmt, obj, typename Base::field_indices_t{});
        }

        // =====================================================================
        // QUERY LOOPS
        // =====================================================================

        // Unified query loop template - handles both direct and JOIN extraction
        // DATABASE-AGNOSTIC: Uses Statement methods with templates for cross-module inlining
        // Extractor signature: void(Statement* stmt, T& obj)
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_query_loop(Statement* stmt, const Extractor& extractor) noexcept -> std::expected<plf::hive<T>, Error> {
            plf::hive<T> results;
            int          step_result = 0;

            // DATABASE-AGNOSTIC: Template step_raw() enables cross-module inlining
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                T obj;
                extractor(stmt, obj);
                results.insert(std::move(obj));
            }

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset_raw();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        // SQL clause helpers delegated to BaseStatement:
        // - Base::append_order_by()
        // - Base::append_limit_offset()
        // - Base::bind_where_params<Statement, Error>()

        std::shared_ptr<ConnType> conn_;

        // Cache for simple SELECT (static SQL, compile-time constant)
        mutable Statement* cached_simple_stmt_ = nullptr;

        // Cache for dynamic queries (WHERE, JOIN, modifiers) - validated by SQL comparison
        mutable Statement*  cached_stmt_ = nullptr;
        mutable std::string cached_sql_;
    };

} // namespace storm::orm::statements