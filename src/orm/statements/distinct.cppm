module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_distinct;

import storm_db_concept;
import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_utilities;
import storm_orm_where;

import <expected>;
import <string>;
import <vector>;
import <tuple>;
import <array>;
import <meta>;
import <optional>;
import <memory>;
import <functional>;

export namespace storm::orm::statements {

    // Import utilities for compile-time SQL generation
    using storm::orm::utilities::ConstexprString;

    // DistinctStatement - executes SELECT DISTINCT on specified field(s) and returns tuple data
    // Supports 1+ fields with compile-time type safety
    // Always generates DISTINCT queries (for aggregates, use separate AggregateStatement)
    //
    // API: Use ^^ operator to pass reflected field information directly
    // Example: qs.distinct<^^Person::name>().select()
    //          qs.distinct<^^Person::name, ^^Person::age>().select()
    template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... FieldInfos>
        requires(sizeof...(FieldInfos) > 0)
    class DistinctStatement : private BaseStatement<T> {
        using Base = BaseStatement<T>;

      public:
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t NumFields = sizeof...(FieldInfos);

      private:
        // Field information is already std::meta::info - no conversion needed!
        // This eliminates the need for __PRETTY_FUNCTION__ string parsing
        static constexpr auto member_infos_ = std::array{FieldInfos...};

        // Deduce field types from member_info array
        template <size_t... Is> static consteval auto get_field_types_helper(std::index_sequence<Is...>) {
            return std::tuple<std::remove_cvref_t<decltype(std::declval<T>().[:member_infos_[Is]:])>...>{};
        }

        using FieldTypesTuple = decltype(get_field_types_helper(std::make_index_sequence<NumFields>{}));

        // Calculate field size at compile-time
        template <size_t I> static consteval size_t get_field_size() {
            size_t         size       = std::meta::identifier_of(member_infos_[I]).size();
            constexpr auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member_infos_[I]);
            if constexpr (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                size += 3; // "_id"
            }
            if constexpr (I > 0) {
                size += 2; // ", "
            }
            return size;
        }

        // Calculate total size of all fields
        template <size_t... Is> static consteval size_t calculate_field_list_size(std::index_sequence<Is...>) {
            return (get_field_size<Is>() + ...);
        }

        // Compile-time field list generation (returns ConstexprString)
        template <size_t... Is> static consteval auto build_field_list_constexpr(std::index_sequence<Is...>) {
            constexpr size_t total_size = calculate_field_list_size(std::make_index_sequence<NumFields>{});
            ConstexprString<total_size + 10> result;
            auto                             append_field = [&result]<size_t I>() {
                if constexpr (I > 0) {
                    result.append(", ");
                }
                // Check if this field is a FK - if so, use column name (field_name_id)
                constexpr auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member_infos_[I]);
                if constexpr (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    result.append(std::meta::identifier_of(member_infos_[I]));
                    result.append("_id");
                } else {
                    result.append(std::meta::identifier_of(member_infos_[I]));
                }
            };
            (append_field.template operator()<Is>(), ...);
            return result;
        }

        // Calculate SQL size at compile-time
        static consteval size_t calculate_select_sql_size() {
            constexpr auto field_list = build_field_list_constexpr(std::make_index_sequence<NumFields>{});
            size_t         size       = 0;
            size += 16; // "SELECT DISTINCT " (max length, DISTINCT is optional)
            size += field_list.len;
            size += 6; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT DISTINCT at compile-time
        static consteval auto build_distinct_sql_array() {
            constexpr size_t          sql_size = calculate_select_sql_size() + 50; // Safety buffer
            ConstexprString<sql_size> result;

            result.append("SELECT DISTINCT ");
            constexpr auto field_list = build_field_list_constexpr(std::make_index_sequence<NumFields>{});
            result.append(field_list);
            result.append(" FROM ");
            result.append(Base::table_name_);

            return result;
        }

        // Pre-computed SQL generated at compile-time (always DISTINCT)
        static constexpr auto distinct_sql_array = build_distinct_sql_array();

      public:
        // Result type: vector of single field OR vector of tuple
        using ResultType = std::conditional_t<
                NumFields == 1,
                std::vector<std::tuple_element_t<0, FieldTypesTuple>>,
                std::vector<FieldTypesTuple>>;

        explicit DistinctStatement(
                std::shared_ptr<ConnType>                  conn,
                const orm::where::ExpressionVariantPtr&    where_expr = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt  = std::nullopt,
                const std::optional<int>&                  limit      = std::nullopt,
                const std::optional<int>&                  offset     = std::nullopt
        )
            : conn_(std::move(conn)), where_expr_(where_expr), join_stmt_(join_stmt), limit_(limit), offset_(offset) {}

        // Update state for reuse (called by QuerySet)
        void update_state(
                std::shared_ptr<ConnType>                  conn,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<JoinStatementWrapper>& join_stmt,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset
        ) {
            conn_       = conn;
            where_expr_ = where_expr;
            join_stmt_  = join_stmt;
            limit_      = limit;
            offset_     = offset;
            // No cache invalidation needed - connection's prepare_cached() handles caching
        }

        // Alias for execute() - provides familiar QuerySet-like API
        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            return execute();
        }

        // Execute SELECT DISTINCT query on the specified field(s)
        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            // Route to appropriate execution path based on WHERE/JOIN state
            if (join_stmt_.has_value() && where_expr_) {
                return execute_where_join_impl();
            } else if (join_stmt_.has_value()) {
                return execute_join_impl();
            } else if (where_expr_) {
                return execute_where_impl();
            } else {
                return execute_simple_distinct();
            }
        }

      private:
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

        // Helper: Inject DISTINCT keyword into JOIN SQL (after SELECT)
        [[nodiscard]] static auto inject_distinct_keyword(const std::string& sql) -> std::expected<std::string, Error> {
            size_t select_pos = sql.find("SELECT ");
            if (select_pos == std::string::npos) [[unlikely]] {
                // This should NEVER happen with correct compile-time JOIN SQL generation
                // But if it does, fail loudly rather than silently producing incorrect SQL
                return std::unexpected(
                        Error{-1,
                              "INTERNAL BUG: JOIN SQL missing SELECT clause. "
                              "This indicates a compile-time SQL generation error. SQL: " +
                                      sql}
                );
            }

            std::string result;
            result.reserve(sql.size() + 10); // "DISTINCT " = 9 chars + null terminator
            result = sql.substr(0, select_pos + 7);
            result += "DISTINCT ";
            result += sql.substr(select_pos + 7);
            return result;
        }

        // Helper: Bind WHERE expression parameters to statement
        [[nodiscard]] __attribute__((always_inline)) __attribute__((hot)) static inline auto
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

        // Simple DISTINCT execution (no WHERE, no JOIN)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_simple_distinct()
                -> std::expected<ResultType, Error> {
            // Use compile-time generated SQL (always includes DISTINCT)
            static const std::string base_sql{distinct_sql_array.data.data(), distinct_sql_array.len};

            std::string sql;
            if (limit_.has_value() || offset_.has_value()) {
                sql = base_sql;
                append_limit_offset(sql, limit_, offset_);
            } else {
                sql = base_sql;
            }

            // Connection's prepare_cached() provides efficient internal caching
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // DISTINCT with WHERE clause (no JOIN)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_where_impl()
                -> std::expected<ResultType, Error> {
            // Build WHERE SQL (compile-time base + runtime WHERE clause)
            static const std::string base_sql{distinct_sql_array.data.data(), distinct_sql_array.len};

            // Build WHERE SQL - connection's prepare_cached() handles caching efficiently
            std::string sql;
            sql.reserve(base_sql.size() + 50);
            sql = base_sql;
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            // Bind WHERE parameters (required for each execution)
            auto bind_result = bind_where_params(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // DISTINCT with JOIN (no WHERE)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_join_impl()
                -> std::expected<ResultType, Error> {
            const std::string& base_sql = join_stmt_->get_complete_sql();

            // Inject DISTINCT into SELECT clause
            auto distinct_join_sql_result = inject_distinct_keyword(base_sql);
            if (!distinct_join_sql_result) [[unlikely]] {
                return std::unexpected(distinct_join_sql_result.error());
            }

            std::string sql = std::move(distinct_join_sql_result.value());
            append_limit_offset(sql, limit_, offset_);

            // Connection's prepare_cached() provides efficient internal caching
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // DISTINCT with WHERE and JOIN
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_where_join_impl()
                -> std::expected<ResultType, Error> {
            // Build WHERE+JOIN SQL - connection's prepare_cached() handles caching efficiently
            const std::string& base_sql = join_stmt_->get_complete_sql();

            // Inject DISTINCT into SELECT clause
            auto distinct_join_sql_result = inject_distinct_keyword(base_sql);
            if (!distinct_join_sql_result) [[unlikely]] {
                return std::unexpected(distinct_join_sql_result.error());
            }

            // Add WHERE clause
            std::string sql = std::move(distinct_join_sql_result.value());
            sql.reserve(sql.size() + 50);
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            // Bind WHERE parameters (required for each execution)
            auto bind_result = bind_where_params(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // Unified query execution loop for all DISTINCT query types
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_query_loop(Statement* stmt)
                -> std::expected<ResultType, Error> {
            // HYBRID OPTIMIZATION: Adaptive strategy for small vs large result sets (matches SELECT strategy)
            // - Small results (<100 rows): emplace_back() avoids constructing unused objects
            //   (LIMIT queries: optimal for typical DISTINCT operations)
            // - Large results (≥100 rows): resize() pre-allocation avoids per-row overhead
            //   (Full table DISTINCT: 1.7x faster for single fields)
            // - Overflow (>1000): 1.5x growth provides better memory reuse than 2x
            ResultType results;
            results.reserve(100); // Initial capacity for typical DISTINCT queries

            int              step_result            = Statement::NO_MORE_ROWS;
            size_t           row_count              = 0;
            constexpr size_t SMALL_RESULT_THRESHOLD = 100;

            if constexpr (NumFields == 1) {
                // Single field: adaptive strategy
                using FieldType = std::tuple_element_t<0, FieldTypesTuple>;

                // Phase 1: Use emplace_back() for first 100 rows (optimized for LIMIT queries)
                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE &&
                       row_count < SMALL_RESULT_THRESHOLD) {
                    results.emplace_back(Base::template extract_column_value<FieldType>(*stmt, 0));
                    row_count++;
                }

                // Phase 2: If exceeded threshold, switch to pre-allocation strategy
                if (step_result == Statement::ROW_AVAILABLE) {
                    // Large result set - pre-allocate for remaining rows
                    results.resize(1000);

                    // Continue extracting into pre-allocated objects
                    while (step_result == Statement::ROW_AVAILABLE && row_count < results.size()) {
                        results[row_count] = Base::template extract_column_value<FieldType>(*stmt, 0);
                        row_count++;
                        step_result = stmt->step_raw();
                    }

                    // Handle overflow with 1.5x growth (better memory reuse than 2x)
                    while (step_result == Statement::ROW_AVAILABLE) {
                        if (row_count >= results.size()) {
                            size_t new_size = results.size() + results.size() / 2; // 1.5x growth
                            results.resize(new_size);
                        }
                        results[row_count] = Base::template extract_column_value<FieldType>(*stmt, 0);
                        row_count++;
                        step_result = stmt->step_raw();
                    }

                    // Trim to actual size
                    results.resize(row_count);
                }
            } else {
                // Multi-field: adaptive strategy
                // Phase 1: Use emplace_back() for first 100 rows (optimized for LIMIT queries)
                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE &&
                       row_count < SMALL_RESULT_THRESHOLD) {
                    emplace_tuple_from_columns(results, stmt, std::make_index_sequence<NumFields>{});
                    row_count++;
                }

                // Phase 2: If exceeded threshold, switch to pre-allocation strategy
                if (step_result == Statement::ROW_AVAILABLE) {
                    // Large result set - pre-allocate for remaining rows
                    results.resize(1000);

                    // Continue extracting into pre-allocated objects
                    while (step_result == Statement::ROW_AVAILABLE && row_count < results.size()) {
                        emplace_tuple_at_index(results, row_count, stmt, std::make_index_sequence<NumFields>{});
                        row_count++;
                        step_result = stmt->step_raw();
                    }

                    // Handle overflow with 1.5x growth
                    while (step_result == Statement::ROW_AVAILABLE) {
                        if (row_count >= results.size()) {
                            size_t new_size = results.size() + results.size() / 2; // 1.5x growth
                            results.resize(new_size);
                        }
                        emplace_tuple_at_index(results, row_count, stmt, std::make_index_sequence<NumFields>{});
                        row_count++;
                        step_result = stmt->step_raw();
                    }

                    // Trim to actual size
                    results.resize(row_count);
                }
            }

            // Check for errors
            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

      private:
        // OPTIMIZATION: Emplace tuple by extracting columns in-place (for reserve() strategy)
        // Constructs tuple directly in vector without intermediate temporaries
        // Template parameter R delays evaluation until method is called (avoids void& when NumFields == 0)
        template <size_t... Is, typename R = ResultType>
        void emplace_tuple_from_columns(R& results, Statement* stmt, std::index_sequence<Is...>)
            requires(NumFields > 0)
        {
            results.emplace_back(
                    Base::template extract_column_value<std::tuple_element_t<Is, FieldTypesTuple>>(*stmt, Is)...
            );
        }

        // OPTIMIZATION: Assign tuple to pre-allocated index (for resize() strategy)
        // Assigns to existing tuple element without intermediate temporaries
        template <size_t... Is, typename R = ResultType>
        void emplace_tuple_at_index(R& results, size_t index, Statement* stmt, std::index_sequence<Is...>)
            requires(NumFields > 0)
        {
            results[index] = std::make_tuple(
                    Base::template extract_column_value<std::tuple_element_t<Is, FieldTypesTuple>>(*stmt, Is)...
            );
        }

        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
    };

} // namespace storm::orm::statements
