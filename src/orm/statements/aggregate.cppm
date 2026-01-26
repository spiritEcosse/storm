module;

#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_statements_aggregate;

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
import <cstdint>;
import <optional>;
import <memory>;

export namespace storm::orm::statements {

    // Import utilities for compile-time SQL generation
    using storm::orm::utilities::ConstexprString;

    // Aggregate function types
    enum class AggregateType { SUM, COUNT, AVG, MIN, MAX, COUNT_DISTINCT };

    // LCOV_EXCL_START - compile-time only (called from consteval functions)
    // Helper to get SQL function name from AggregateType
    // Note: COUNT_DISTINCT is handled specially in build_operation_sql
    constexpr auto get_agg_function_name(AggregateType type) -> std::string_view {
        switch (type) {
        case AggregateType::SUM:
            return "SUM";
        case AggregateType::COUNT:
            return "COUNT";
        case AggregateType::AVG:
            return "AVG";
        case AggregateType::MIN:
            return "MIN";
        case AggregateType::MAX:
            return "MAX";
        case AggregateType::COUNT_DISTINCT:
            return "COUNT";
        }
        return "";
    }
    // LCOV_EXCL_STOP

    // Aggregate operation descriptor
    template <AggregateType Type, std::meta::info... FieldInfos> struct AggregateOp {
        static constexpr AggregateType agg_type    = Type;
        static constexpr size_t        field_count = sizeof...(FieldInfos);
        // LCOV_EXCL_START - compile-time only (initializes constexpr field_infos)
        static constexpr auto get_field_infos() -> std::array<std::meta::info, sizeof...(FieldInfos)> {
            if constexpr (sizeof...(FieldInfos) > 0) {
                return std::array{FieldInfos...};
            } else {
                return std::array<std::meta::info, 0>{};
            }
        }
        // LCOV_EXCL_STOP
        static constexpr auto field_infos = get_field_infos();
    };

    // FieldInfoPack - compile-time holder for std::meta::info values
    // Used to pass group-by fields to GroupByAggregateStatement
    template <std::meta::info... Infos> struct FieldInfoPack {
        static constexpr size_t                                        size = sizeof...(Infos);
        static constexpr std::array<std::meta::info, sizeof...(Infos)> values{Infos...};

        template <size_t I> static consteval auto at() -> std::meta::info {
            static_assert(I < size, "Index out of bounds");
            return values[I];
        }
    };

    // AggregateBuilder - fluent interface for building multiple aggregates in one query
    // Usage: queryset.aggregate().sum<&Person::age>().count().avg<&Person::salary>().select()
    template <typename T, storm::db::DatabaseConnection ConnType, typename... Ops> class AggregateBuilder {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Result type for a single operation
        template <size_t OpIdx> struct OpResultType {
            static constexpr auto op = std::tuple_element_t<OpIdx, std::tuple<Ops...>>{};

            // SUM/COUNT return int64_t, AVG/MIN/MAX return double
            using type = std::conditional_t<
                    op.agg_type == AggregateType::SUM || op.agg_type == AggregateType::COUNT,
                    int64_t,
                    double>;
        };

        // Deduce result type based on number of operations
        // Returns: single scalar for 1 op, tuple for multiple ops, void for 0 ops
        template <size_t... Is> static consteval auto deduce_result_type_helper(std::index_sequence<Is...> /*unused*/) {
            if constexpr (sizeof...(Ops) == 0) {
                struct EmptyTag {};
                return EmptyTag{}; // Should never be used (select() has requires clause)
            } else if constexpr (sizeof...(Ops) == 1) {
                return typename OpResultType<0>::type{}; // Single scalar value
            } else {
                return std::tuple<typename OpResultType<Is>::type...>{}; // Multiple values as tuple
            }
        }

      public:
        using ResultType = decltype(deduce_result_type_helper(std::make_index_sequence<sizeof...(Ops)>{}));

        explicit AggregateBuilder(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Add SUM aggregate (multi-field: SUM(f1 + f2 + ...))
        template <std::meta::info... FieldInfos> auto sum() {
            return AggregateBuilder<T, ConnType, Ops..., AggregateOp<AggregateType::SUM, FieldInfos...>>{conn_};
        }

        // Add COUNT aggregate (COUNT(*) if no fields specified)
        template <std::meta::info... FieldInfos> auto count() {
            return AggregateBuilder<T, ConnType, Ops..., AggregateOp<AggregateType::COUNT, FieldInfos...>>{conn_};
        }

        // Add AVG aggregate (multi-field: AVG(f1 + f2 + ...))
        template <std::meta::info... FieldInfos> auto avg() {
            return AggregateBuilder<T, ConnType, Ops..., AggregateOp<AggregateType::AVG, FieldInfos...>>{conn_};
        }

        // Add MIN aggregate (multi-field: MIN(f1 + f2 + ...))
        template <std::meta::info... FieldInfos> auto min() {
            return AggregateBuilder<T, ConnType, Ops..., AggregateOp<AggregateType::MIN, FieldInfos...>>{conn_};
        }

        // Add MAX aggregate (multi-field: MAX(f1 + f2 + ...))
        template <std::meta::info... FieldInfos> auto max() {
            return AggregateBuilder<T, ConnType, Ops..., AggregateOp<AggregateType::MAX, FieldInfos...>>{conn_};
        }

        // Execute the query and return results
        auto select() -> std::expected<ResultType, Error>
            requires(sizeof...(Ops) > 0)
        {
            return execute();
        }

      private:
        // Build SQL for single operation
        template <size_t OpIdx> static consteval auto build_operation_sql() {
            constexpr auto op       = std::tuple_element_t<OpIdx, std::tuple<Ops...>>{};
            constexpr auto agg_name = get_agg_function_name(op.agg_type);

            // Calculate size
            size_t size = agg_name.size() + 2; // "AGG("

            if constexpr (op.field_count == 0) {
                // COUNT(*) case
                size += 2; // "*)"
            } else {
                // Calculate field expression: field1 + field2 + ...
                size_t field_expr_size = 0;
                for (size_t i = 0; i < op.field_count; ++i) {
                    field_expr_size += std::meta::identifier_of(op.field_infos[i]).size();
                    if (i > 0) {
                        field_expr_size += 3; // " + "
                    }
                }
                size += field_expr_size + 1; // ")"
            }

            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            result.append(agg_name);
            result.append("(");

            if constexpr (op.field_count == 0) {
                result.append("*");
            } else {
                for (size_t i = 0; i < op.field_count; ++i) {
                    if (i > 0) {
                        result.append(" + ");
                    }
                    result.append(std::meta::identifier_of(op.field_infos[i]));
                }
            }

            result.append(")");
            return result;
        }

        // Build full SELECT statement
        template <size_t... Is> static consteval auto build_aggregate_sql(std::index_sequence<Is...> /*unused*/) {
            ConstexprString<2048> result;
            result.append("SELECT ");

            // Add each operation
            (([&result]() -> void {
                 if constexpr (Is > 0) {
                     result.append(", ");
                 }
                 constexpr auto op_sql = build_operation_sql<Is>();
                 result.append(op_sql);
             }()),
             ...);

            result.append(" FROM ");
            result.append(Base::get_table_name());

            return result;
        }

        static constexpr auto sql_array = build_aggregate_sql(std::make_index_sequence<sizeof...(Ops)>{});

        // Extract single result value based on type
        template <typename ResultT> static auto extract_result(Statement& stmt, int col_idx) -> ResultT {
            if constexpr (std::is_same_v<ResultT, int64_t>) {
                return stmt.extract_int64(col_idx);
            } else if constexpr (std::is_same_v<ResultT, double>) {
                return stmt.extract_double(col_idx);
            }
        }

        // Execute and extract results
        template <size_t... Is>
        auto execute_impl(std::index_sequence<Is...> /*unused*/) -> std::expected<ResultType, Error> {
            static const std::string sql{sql_array.data.data(), sql_array.len};

            // Cache statement on first use
            if (cached_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_stmt_ = *prepare_result;
            }

            // Execute and get single row
            int step_result = cached_stmt_->step_raw();

            if (step_result != Statement::ROW_AVAILABLE) {
                cached_stmt_->reset();
                if (step_result == Statement::NO_MORE_ROWS) {
                    // Return zero/default values for empty table
                    if constexpr (sizeof...(Ops) == 1) {
                        return ResultType{};
                    } else {
                        return ResultType{typename OpResultType<Is>::type{}...};
                    }
                }
                return std::unexpected(Error{step_result, cached_stmt_->get_error_message()});
            }

            // Extract result(s)
            ResultType result;
            if constexpr (sizeof...(Ops) == 1) {
                result = extract_result<ResultType>(*cached_stmt_, 0);
            } else {
                result = ResultType{extract_result<typename OpResultType<Is>::type>(*cached_stmt_, Is)...};
            }

            cached_stmt_->reset();
            return result;
        }

        auto execute() -> std::expected<ResultType, Error>
            requires(sizeof...(Ops) > 0)
        {
            return execute_impl(std::make_index_sequence<sizeof...(Ops)>{});
        }

        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_stmt_ = nullptr;
    };

    // Convenience wrapper for single aggregate operations
    // These provide shortcut methods like queryset.sum<&Person::age>().select()
    // Requires at least one field EXCEPT for COUNT which can use COUNT(*)
    template <typename T, storm::db::DatabaseConnection ConnType, AggregateType AggType, std::meta::info... FieldInfos>
        requires(sizeof...(FieldInfos) > 0 || AggType == AggregateType::COUNT)
    class SingleAggregateStatement {
        using Error   = typename ConnType::Error;
        using Builder = AggregateBuilder<T, ConnType, AggregateOp<AggType, FieldInfos...>>;

      public:
        explicit SingleAggregateStatement(std::shared_ptr<ConnType> conn) : builder_(std::move(conn)) {}

        // Execute and return scalar result
        auto select() -> std::expected<typename Builder::ResultType, Error> {
            return builder_.select();
        }

        auto execute() -> std::expected<typename Builder::ResultType, Error> {
            return builder_.select();
        }

      private:
        Builder builder_;
    };

    // ============================================================================
    // AggregateStatement - Aggregate queries with WHERE/JOIN support
    // ============================================================================
    // This class provides full support for aggregate queries with WHERE and JOIN
    // clauses, following the DistinctStatement pattern for state management.
    //
    // Usage:
    //   qs.where(age > 30).count().select()
    //   qs.join<FK>().sum<^^T::field>().select()
    //   qs.where(id > 5).join<FK>().avg<^^T::salary>().select()
    //
    template <typename T, storm::db::DatabaseConnection ConnType, typename... Ops>
        requires(sizeof...(Ops) > 0)
    class AggregateStatement : private BaseStatement<T> {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Result type for a single operation
        template <size_t OpIdx> struct OpResultType {
            static constexpr auto op = std::tuple_element_t<OpIdx, std::tuple<Ops...>>{};

            // SUM/COUNT return int64_t, AVG/MIN/MAX return double
            using type = std::conditional_t<
                    op.agg_type == AggregateType::SUM || op.agg_type == AggregateType::COUNT,
                    int64_t,
                    double>;
        };

        // Deduce result type based on number of operations
        template <size_t... Is> static consteval auto deduce_result_type_helper(std::index_sequence<Is...> /*unused*/) {
            if constexpr (sizeof...(Ops) == 1) {
                return typename OpResultType<0>::type{}; // Single scalar value
            } else {
                return std::tuple<typename OpResultType<Is>::type...>{}; // Multiple values as tuple
            }
        }

      public:
        using ResultType = decltype(deduce_result_type_helper(std::make_index_sequence<sizeof...(Ops)>{}));

        explicit AggregateStatement(
                std::shared_ptr<ConnType>                  conn,
                orm::where::ExpressionVariantPtr           where_expr = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt  = std::nullopt
        )
            : conn_(std::move(conn)), where_expr_(std::move(where_expr)), join_stmt_(join_stmt) {}

        // Execute the query and return results
        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            return execute();
        }

        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            // Route to appropriate execution path based on WHERE/JOIN state
            return Base::dispatch_execute(
                    join_stmt_.has_value(),
                    static_cast<bool>(where_expr_),
                    [this]() -> std::expected<ResultType, Error> { return execute_simple(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_impl(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_join_impl(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_join_impl(); }
            );
        }

      private:
        // Build SQL for single operation
        template <size_t OpIdx> static consteval auto build_operation_sql() {
            constexpr auto op       = std::tuple_element_t<OpIdx, std::tuple<Ops...>>{};
            constexpr auto agg_name = get_agg_function_name(op.agg_type);

            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            result.append(agg_name);
            result.append("(");

            // Handle COUNT(DISTINCT field) specially
            if constexpr (op.agg_type == AggregateType::COUNT_DISTINCT) {
                result.append("DISTINCT ");
                // COUNT(DISTINCT) requires exactly one field
                static_assert(op.field_count == 1, "COUNT(DISTINCT) requires exactly one field");
                result.append(std::meta::identifier_of(op.field_infos[0]));
            } else if constexpr (op.field_count == 0) {
                result.append("*");
            } else {
                for (size_t i = 0; i < op.field_count; ++i) {
                    if (i > 0) {
                        result.append(" + ");
                    }
                    result.append(std::meta::identifier_of(op.field_infos[i]));
                }
            }

            result.append(")");
            return result;
        }

        // Build full SELECT statement (base SQL without WHERE/JOIN)
        template <size_t... Is> static consteval auto build_aggregate_sql(std::index_sequence<Is...> /*unused*/) {
            ConstexprString<2048> result;
            result.append("SELECT ");

            // Add each operation
            (([&result]() -> void {
                 if constexpr (Is > 0) {
                     result.append(", ");
                 }
                 constexpr auto op_sql = build_operation_sql<Is>();
                 result.append(op_sql);
             }()),
             ...);

            result.append(" FROM ");
            result.append(Base::get_table_name());

            return result;
        }

        static constexpr auto sql_array = build_aggregate_sql(std::make_index_sequence<sizeof...(Ops)>{});

        // Build aggregate SELECT clause only (for JOIN queries)
        template <size_t... Is>
        static consteval auto build_aggregate_select_clause(std::index_sequence<Is...> /*unused*/) {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;

            (([&result]() -> void {
                 if constexpr (Is > 0) {
                     result.append(", ");
                 }
                 constexpr auto op_sql = build_operation_sql<Is>();
                 result.append(op_sql);
             }()),
             ...);

            return result;
        }

        static constexpr auto select_clause_array =
                build_aggregate_select_clause(std::make_index_sequence<sizeof...(Ops)>{});

        // Extract single result value based on type
        template <typename ResultT> static auto extract_result(Statement& stmt, int col_idx) -> ResultT {
            if constexpr (std::is_same_v<ResultT, int64_t>) {
                return stmt.extract_int64(col_idx);
            } else if constexpr (std::is_same_v<ResultT, double>) {
                return stmt.extract_double(col_idx);
            }
        }

        // bind_where_params delegated to Base::bind_where_params<Statement, Error>()

        // Simple aggregate execution (no WHERE, no JOIN)
        [[nodiscard]] __attribute__((hot)) auto execute_simple() -> std::expected<ResultType, Error> {
            static const std::string sql{sql_array.data.data(), sql_array.len};

            // Cache statement on first use
            if (cached_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_stmt_ = *prepare_result;
            }

            return execute_and_extract(cached_stmt_);
        }

        // Aggregate with WHERE clause (no JOIN)
        [[nodiscard]] __attribute__((hot)) auto execute_where_impl() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

            // Build WHERE SQL
            std::string sql;
            sql.reserve(base_sql.size() + utilities::sql_len::LARGE_BUFFER);
            sql = base_sql;
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            // Bind WHERE parameters
            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_and_extract(*prepare_result);
        }

        // Aggregate with JOIN (no WHERE)
        [[nodiscard]] __attribute__((hot)) auto execute_join_impl() -> std::expected<ResultType, Error> {
            // Get JOIN SQL and replace SELECT clause with aggregate
            const std::string& join_sql = join_stmt_->get_complete_sql();

            // Build aggregate SQL with JOIN
            // Original: SELECT * FROM T INNER JOIN ...
            // Target:   SELECT AGG(...) FROM T INNER JOIN ...
            std::string sql = build_aggregate_join_sql(join_sql);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_and_extract(*prepare_result);
        }

        // Aggregate with WHERE and JOIN
        [[nodiscard]] __attribute__((hot)) auto execute_where_join_impl() -> std::expected<ResultType, Error> {
            // Get JOIN SQL and replace SELECT clause with aggregate
            const std::string& join_sql = join_stmt_->get_complete_sql();

            // Build aggregate SQL with JOIN and WHERE
            std::string sql = build_aggregate_join_sql(join_sql);
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            // Bind WHERE parameters
            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_and_extract(*prepare_result);
        }

        // Helper: Build aggregate SQL from JOIN SQL by replacing SELECT clause
        [[nodiscard]] auto build_aggregate_join_sql(const std::string& join_sql) const -> std::string {
            // Find position after "SELECT " and before " FROM"
            const size_t select_pos = join_sql.find("SELECT ");
            const size_t from_pos   = join_sql.find(" FROM ");

            if (select_pos == std::string::npos || from_pos == std::string::npos) [[unlikely]] {
                // Fallback: just use base SQL (should never happen with valid JOIN SQL)
                return std::string{sql_array.data.data(), sql_array.len};
            }

            // Build: SELECT <aggregates> FROM ... (rest of JOIN SQL)
            static const std::string agg_clause{select_clause_array.data.data(), select_clause_array.len};

            std::string result;
            result.reserve(utilities::sql_len::SELECT + agg_clause.size() + (join_sql.size() - from_pos));
            result = "SELECT ";
            result += agg_clause;
            result += join_sql.substr(from_pos); // " FROM ..." onwards

            return result;
        }

        // Common execution and extraction logic
        template <size_t... Is>
        [[nodiscard]] __attribute__((hot)) auto
        execute_and_extract_impl(Statement* stmt, std::index_sequence<Is...> /*unused*/)
                -> std::expected<ResultType, Error> {
            int step_result = stmt->step_raw();

            if (step_result != Statement::ROW_AVAILABLE) {
                stmt->reset();
                if (step_result == Statement::NO_MORE_ROWS) {
                    // Return zero/default values for empty table
                    if constexpr (sizeof...(Ops) == 1) {
                        return ResultType{};
                    } else {
                        return ResultType{typename OpResultType<Is>::type{}...};
                    }
                }
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            // Extract result(s)
            ResultType result;
            if constexpr (sizeof...(Ops) == 1) {
                result = extract_result<ResultType>(*stmt, 0);
            } else {
                result = ResultType{extract_result<typename OpResultType<Is>::type>(*stmt, Is)...};
            }

            stmt->reset();
            return result;
        }

        [[nodiscard]] auto execute_and_extract(Statement* stmt) -> std::expected<ResultType, Error> {
            return execute_and_extract_impl(stmt, std::make_index_sequence<sizeof...(Ops)>{});
        }

        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        mutable Statement*                  cached_stmt_ = nullptr;
    };

    // ============================================================================
    // GroupByAggregateStatement - Aggregate queries with GROUP BY support
    // ============================================================================
    // This class provides GROUP BY functionality with aggregates.
    // Returns multiple rows: one per unique group key combination.
    //
    // Usage:
    //   qs.group_by<^^Person::department>().count().select()
    //   qs.group_by<^^Person::dept, ^^Person::role>().sum<^^Person::salary>().select()
    //
    // Returns: plf::hive<std::tuple<GroupKeyTypes..., AggResultTypes...>>
    //
    template <typename T, storm::db::DatabaseConnection ConnType, typename GroupByFields, typename... Ops>
        requires(sizeof...(Ops) > 0)
    class GroupByAggregateStatement : private BaseStatement<T> {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // GroupByFields is expected to be FieldInfoPack<...>
        static constexpr size_t NumGroupFields = GroupByFields::size;

        // Get field type for group by column
        template <size_t Idx> struct GroupFieldType {
            static constexpr auto field_info = GroupByFields::template at<Idx>();
            using type                       = std::remove_cvref_t<decltype(std::declval<T>().[:field_info:])>;
        };

        // Result type for a single aggregate operation
        template <size_t OpIdx> struct OpResultType {
            static constexpr auto op = std::tuple_element_t<OpIdx, std::tuple<Ops...>>{};
            using type               = std::conditional_t<
                                  op.agg_type == AggregateType::SUM || op.agg_type == AggregateType::COUNT,
                                  int64_t,
                                  double>;
        };

        // Build the combined result type: tuple<GroupKeys..., AggResults...>
        template <size_t... GIs, size_t... AIs>
        static consteval auto
        build_result_tuple_type(std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/) {
            return std::tuple<typename GroupFieldType<GIs>::type..., typename OpResultType<AIs>::type...>{};
        }

        using TupleType = decltype(build_result_tuple_type(
                std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
        ));

      public:
        using ResultType = plf::hive<TupleType>;

        explicit GroupByAggregateStatement(
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

        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            return execute();
        }

        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            return Base::dispatch_execute(
                    join_stmt_.has_value(),
                    static_cast<bool>(where_expr_),
                    [this]() -> std::expected<ResultType, Error> { return execute_simple(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_impl(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_join_impl(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_join_impl(); }
            );
        }

      private:
        // Build GROUP BY field list
        template <size_t... Is> static consteval auto build_group_by_fields(std::index_sequence<Is...> /*unused*/) {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            (([&result]() -> void {
                 if constexpr (Is > 0) {
                     result.append(", ");
                 }
                 constexpr auto field_info = GroupByFields::template at<Is>();
                 result.append(std::meta::identifier_of(field_info));
             }()),
             ...);
            return result;
        }

        // Build SELECT clause with group fields and aggregates
        template <size_t... GIs, size_t... AIs>
        static consteval auto
        build_select_clause(std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/) {
            ConstexprString<2048> result;

            // Add group by fields first
            (([&result]() -> void {
                 if constexpr (GIs > 0) {
                     result.append(", ");
                 }
                 constexpr auto field_info = GroupByFields::template at<GIs>();
                 result.append(std::meta::identifier_of(field_info));
             }()),
             ...);

            // Add aggregate functions
            (([&result]() -> void {
                 result.append(", ");
                 constexpr auto op       = std::tuple_element_t<AIs, std::tuple<Ops...>>{};
                 constexpr auto agg_name = get_agg_function_name(op.agg_type);
                 result.append(agg_name);
                 result.append("(");
                 if constexpr (op.field_count == 0) {
                     result.append("*");
                 } else {
                     for (size_t i = 0; i < op.field_count; ++i) {
                         if (i > 0) {
                             result.append(" + ");
                         }
                         result.append(std::meta::identifier_of(op.field_infos[i]));
                     }
                 }
                 result.append(")");
             }()),
             ...);

            return result;
        }

        // Build full SQL: SELECT fields, aggs FROM table GROUP BY fields
        static consteval auto build_group_by_sql() {
            ConstexprString<utilities::buffer_size::SQL_LARGE> result;
            result.append("SELECT ");

            constexpr auto select_clause = build_select_clause(
                    std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
            );
            result.append(select_clause);

            result.append(" FROM ");
            result.append(Base::get_table_name());
            result.append(" GROUP BY ");

            constexpr auto group_fields = build_group_by_fields(std::make_index_sequence<NumGroupFields>{});
            result.append(group_fields);

            return result;
        }

        static constexpr auto sql_array = build_group_by_sql();

        // Extract a single column value based on type - delegates to BaseStatement
        template <typename FieldType> static auto extract_column(Statement& stmt, int col_idx) -> FieldType {
            return Base::template extract_column_value<FieldType>(stmt, col_idx);
        }

        // Extract one row into tuple
        template <size_t... GIs, size_t... AIs>
        static auto
        extract_row(Statement& stmt, std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/)
                -> TupleType {
            return TupleType{
                    extract_column<typename GroupFieldType<GIs>::type>(stmt, GIs)...,
                    extract_column<typename OpResultType<AIs>::type>(stmt, NumGroupFields + AIs)...
            };
        }

        // bind_where_params delegated to Base::bind_where_params<Statement, Error>()

        // Execute simple GROUP BY (no WHERE, no JOIN)
        [[nodiscard]] auto execute_simple() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

            // Check if we need dynamic SQL (ORDER BY, LIMIT, or OFFSET)
            const bool has_modifiers = order_by_wrapper_.has_value() || limit_.has_value() || offset_.has_value();

            std::string sql;
            if (has_modifiers) {
                sql = base_sql;
                Base::append_order_by(sql, order_by_wrapper_);
                Base::append_limit_offset(sql, limit_, offset_);
            }

            auto prepare_result = conn_->prepare_cached(has_modifiers ? sql : base_sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // Execute GROUP BY with WHERE
        [[nodiscard]] auto execute_where_impl() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

            std::string sql;
            sql.reserve(base_sql.size() + utilities::sql_len::XL_BUFFER);

            // Insert WHERE before GROUP BY
            size_t const group_by_pos = base_sql.find(" GROUP BY ");
            if (group_by_pos != std::string::npos) {
                sql = base_sql.substr(0, group_by_pos);
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr_);
                sql += base_sql.substr(group_by_pos);
            } else {
                sql = base_sql;
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr_);
            }

            // Append ORDER BY, LIMIT, OFFSET after GROUP BY
            Base::append_order_by(sql, order_by_wrapper_);
            Base::append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // Execute GROUP BY with JOIN
        [[nodiscard]] auto execute_join_impl() -> std::expected<ResultType, Error> {
            // Build SQL with JOIN and GROUP BY
            const std::string& join_sql = join_stmt_->get_complete_sql();

            // Extract FROM clause from JOIN SQL
            size_t from_pos = join_sql.find(" FROM ");
            if (from_pos == std::string::npos) [[unlikely]] {
                return std::unexpected(Error{-1, "Invalid JOIN SQL"});
            }

            static const std::string select_clause{
                    build_select_clause(
                            std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
                    )
                            .data.data(),
                    build_select_clause(
                            std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
                    )
                            .len
            };
            static const std::string group_clause{
                    build_group_by_fields(std::make_index_sequence<NumGroupFields>{}).data.data(),
                    build_group_by_fields(std::make_index_sequence<NumGroupFields>{}).len
            };

            std::string sql;
            sql.reserve(
                    select_clause.size() + join_sql.size() + group_clause.size() + utilities::sql_len::MEDIUM_BUFFER
            );
            sql = "SELECT ";
            sql += select_clause;
            sql += join_sql.substr(from_pos);
            sql += " GROUP BY ";
            sql += group_clause;

            // Append ORDER BY, LIMIT, OFFSET after GROUP BY
            Base::append_order_by(sql, order_by_wrapper_);
            Base::append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // Execute GROUP BY with WHERE and JOIN
        [[nodiscard]] auto execute_where_join_impl() -> std::expected<ResultType, Error> {
            const std::string& join_sql = join_stmt_->get_complete_sql();

            size_t from_pos = join_sql.find(" FROM ");
            if (from_pos == std::string::npos) [[unlikely]] {
                return std::unexpected(Error{-1, "Invalid JOIN SQL"});
            }

            static const std::string select_clause{
                    build_select_clause(
                            std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
                    )
                            .data.data(),
                    build_select_clause(
                            std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
                    )
                            .len
            };
            static const std::string group_clause{
                    build_group_by_fields(std::make_index_sequence<NumGroupFields>{}).data.data(),
                    build_group_by_fields(std::make_index_sequence<NumGroupFields>{}).len
            };

            std::string sql;
            sql.reserve(select_clause.size() + join_sql.size() + group_clause.size() + utilities::sql_len::XL_BUFFER);
            sql = "SELECT ";
            sql += select_clause;
            sql += join_sql.substr(from_pos);
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            sql += " GROUP BY ";
            sql += group_clause;

            // Append ORDER BY, LIMIT, OFFSET after GROUP BY
            Base::append_order_by(sql, order_by_wrapper_);
            Base::append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        // Common query execution loop
        [[nodiscard]] auto execute_query_loop(Statement* stmt) -> std::expected<ResultType, Error> {
            ResultType results;
            int        step_result = Statement::NO_MORE_ROWS;

            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                results.insert(extract_row(
                        *stmt, std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}
                ));
            }

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
        std::optional<OrderByWrapper>       order_by_wrapper_;
    };

    // ============================================================================
    // GroupByBuilder - Fluent builder for GROUP BY queries
    // ============================================================================
    // Returns a builder that allows chaining aggregate methods after group_by().
    //
    // Usage:
    //   qs.group_by<^^Person::department>().count().select()
    //   qs.group_by<^^Person::dept, ^^Person::role>().sum<^^Person::salary>().select()
    //   qs.where(age > 25).group_by<^^Person::years_exp>().count().select()
    //
    template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... GroupFieldInfos>
        requires(sizeof...(GroupFieldInfos) > 0)
    class GroupByBuilder {
        using GroupFields = FieldInfoPack<GroupFieldInfos...>;

      public:
        explicit GroupByBuilder(
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

        // COUNT aggregate - returns GroupByAggregateStatement
        // Usage: group_by<field>().count().select()
        // Returns GroupByAggregateStatement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto count() {
            using StmtType = GroupByAggregateStatement<
                    T,
                    ConnType,
                    GroupFields,
                    AggregateOp<AggregateType::COUNT, FieldInfos...>>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        // SUM aggregate - returns GroupByAggregateStatement
        // Usage: group_by<field>().sum<sum_field>().select()
        // Returns GroupByAggregateStatement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto sum() {
            using StmtType =
                    GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::SUM, FieldInfos...>>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        // AVG aggregate - returns GroupByAggregateStatement
        // Usage: group_by<field>().avg<avg_field>().select()
        // Returns GroupByAggregateStatement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto avg() {
            using StmtType =
                    GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::AVG, FieldInfos...>>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        // MIN aggregate - returns GroupByAggregateStatement
        // Usage: group_by<field>().min<min_field>().select()
        // Returns GroupByAggregateStatement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto min() {
            using StmtType =
                    GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::MIN, FieldInfos...>>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        // MAX aggregate - returns GroupByAggregateStatement
        // Usage: group_by<field>().max<max_field>().select()
        // Returns GroupByAggregateStatement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto max() {
            using StmtType =
                    GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::MAX, FieldInfos...>>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

      private:
        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
        std::optional<OrderByWrapper>       order_by_wrapper_;
    };

} // namespace storm::orm::statements
