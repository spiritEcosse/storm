module;

#include <sqlite3.h>
#include <meta>
#include <utility>
#include <plf_hive/plf_hive.h>

export module storm_orm_statements_distinct;

import storm_db_concept;
import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
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
        template <size_t I> static consteval auto get_field_size() -> size_t {
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
        template <size_t... Is> static consteval auto calculate_field_list_size(std::index_sequence<Is...>) -> size_t {
            return (get_field_size<Is>() + ...);
        }

        // Compile-time field list generation (returns ConstexprString)
        template <size_t... Is> static consteval auto build_field_list_constexpr(std::index_sequence<Is...>) {
            constexpr size_t total_size = calculate_field_list_size(std::make_index_sequence<NumFields>{});
            ConstexprString<total_size + 10> result;
            auto                             append_field = [&result]<size_t I>() -> void {
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
        static consteval auto calculate_select_sql_size() -> size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT_DISTINCT;
            constexpr auto field_list = build_field_list_constexpr(std::make_index_sequence<NumFields>{});
            size_t         size       = 0;
            size += SELECT_DISTINCT; // "SELECT DISTINCT " (max length, DISTINCT is optional)
            size += field_list.len;
            size += FROM; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT DISTINCT at compile-time
        static consteval auto build_distinct_sql_array() {
            constexpr size_t          sql_size = calculate_select_sql_size() + utilities::sql_len::LARGE_BUFFER;
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
        // Result type: hive of single field OR hive of tuple
        using ResultType = std::conditional_t<
                NumFields == 1,
                plf::hive<std::tuple_element_t<0, FieldTypesTuple>>,
                plf::hive<FieldTypesTuple>>;

        explicit DistinctStatement(
                std::shared_ptr<ConnType>                  conn,
                orm::where::ExpressionVariantPtr           where_expr       = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt        = std::nullopt,
                const std::optional<int>&                  limit            = std::nullopt,
                const std::optional<int>&                  offset           = std::nullopt,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt
        )
            : conn_(std::move(conn))
            , where_expr_(std::move(where_expr))
            , join_stmt_(join_stmt)
            , limit_(limit)
            , offset_(offset)
            , order_by_wrapper_(order_by_wrapper) {}

        // Update state for reuse (called by QuerySet)
        void update_state(
                std::shared_ptr<ConnType>                  conn,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<JoinStatementWrapper>& join_stmt,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt
        ) {
            conn_             = conn;
            where_expr_       = where_expr;
            join_stmt_        = join_stmt;
            limit_            = limit;
            offset_           = offset;
            order_by_wrapper_ = order_by_wrapper;
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
        // Helper: Append ORDER BY clause to SQL from wrapper
        // NOTE: ORDER BY must come before LIMIT/OFFSET in SQLite
        __attribute__((always_inline)) inline auto append_order_by(std::string& sql) const -> void {
            if (order_by_wrapper_.has_value() && !order_by_wrapper_->empty()) {
                sql += order_by_wrapper_->get_order_by_sql();
            }
        }

        // Helper: Append LIMIT/OFFSET clauses to SQL
        // NOTE: SQLite requires LIMIT when using OFFSET, so we use LIMIT -1 (meaning unlimited) when OFFSET is used
        // alone
        __attribute__((always_inline)) static inline auto
        append_limit_offset(std::string& sql, const std::optional<int>& limit, const std::optional<int>& offset)
                -> void {
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
            const size_t select_pos = sql.find("SELECT ");
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
            result.reserve(sql.size() + utilities::sql_len::SMALL_BUFFER); // "DISTINCT " + buffer
            result = sql.substr(0, select_pos + utilities::sql_len::SELECT);
            result += "DISTINCT ";
            result += sql.substr(select_pos + utilities::sql_len::SELECT);
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
            if (order_by_wrapper_.has_value() || limit_.has_value() || offset_.has_value()) {
                sql = base_sql;
                append_order_by(sql);
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
            sql.reserve(base_sql.size() + utilities::sql_len::LARGE_BUFFER);
            sql = base_sql;
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            append_order_by(sql);
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
            append_order_by(sql);
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
            sql.reserve(sql.size() + utilities::sql_len::LARGE_BUFFER);
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            append_order_by(sql);
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
            // plf::hive OPTIMIZATION: Stable pointers + fast insertion/iteration
            // - No reallocation overhead (multi-block architecture)
            // - Superior cache locality during iteration vs std::list/std::deque
            // - Optimal for scenarios with frequent insertions during result processing
            ResultType results;

            int step_result = Statement::NO_MORE_ROWS;

            if constexpr (NumFields == 1) {
                // Single field: direct insertion
                using FieldType = std::tuple_element_t<0, FieldTypesTuple>;

                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                    results.insert(Base::template extract_column_value<FieldType>(*stmt, 0));
                }
            } else {
                // Multi-field: insert tuples
                while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                    insert_tuple_from_columns(results, stmt, std::make_index_sequence<NumFields>{});
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
        // OPTIMIZATION: Insert tuple by extracting columns in-place
        // Constructs tuple directly in hive without intermediate temporaries
        // Template parameter R delays evaluation until method is called (avoids void& when NumFields == 0)
        template <size_t... Is, typename R = ResultType>
        void insert_tuple_from_columns(R& results, Statement* stmt, std::index_sequence<Is...>)
            requires(NumFields > 0)
        {
            results.insert(
                    std::make_tuple(
                            Base::template extract_column_value<std::tuple_element_t<Is, FieldTypesTuple>>(*stmt, Is)...
                    )
            );
        }

        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
        std::optional<OrderByWrapper>       order_by_wrapper_;
    };

} // namespace storm::orm::statements
