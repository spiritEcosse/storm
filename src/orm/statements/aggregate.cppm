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

    using storm::orm::utilities::ConstexprString;

    // Aggregate function types
    enum class AggregateType { SUM, COUNT, AVG, MIN, MAX, COUNT_DISTINCT };

    // LCOV_EXCL_START - compile-time only
    constexpr auto get_agg_function_name(AggregateType type) -> std::string_view {
        switch (type) {
        case AggregateType::SUM:            return "SUM";
        case AggregateType::COUNT:          return "COUNT";
        case AggregateType::AVG:            return "AVG";
        case AggregateType::MIN:            return "MIN";
        case AggregateType::MAX:            return "MAX";
        case AggregateType::COUNT_DISTINCT: return "COUNT";
        }
        return "";
    }
    // LCOV_EXCL_STOP

    // Aggregate operation descriptor
    template <AggregateType Type, std::meta::info... FieldInfos>
    struct AggregateOp {
        static constexpr AggregateType agg_type    = Type;
        static constexpr size_t        field_count = sizeof...(FieldInfos);
        // LCOV_EXCL_START - compile-time only
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
    template <std::meta::info... Infos>
    struct FieldInfoPack {
        static constexpr size_t                                        size = sizeof...(Infos);
        static constexpr std::array<std::meta::info, sizeof...(Infos)> values{Infos...};

        template <size_t I>
        static consteval auto at() -> std::meta::info {
            static_assert(I < size, "Index out of bounds");
            return values[I];
        }
    };

    // ============================================================================
    // Shared SQL Building Utilities (used by all aggregate classes)
    // ============================================================================

    // LCOV_EXCL_START - compile-time only
    // Build SQL for single aggregate operation: "AGG(field1 + field2)" or "COUNT(*)"
    template <typename Op>
    consteval auto build_single_op_sql() {
        constexpr auto agg_name = get_agg_function_name(Op::agg_type);

        ConstexprString<utilities::buffer_size::SQL_SMALL> result;
        result.append(agg_name);
        result.append("(");

        if constexpr (Op::agg_type == AggregateType::COUNT_DISTINCT) {
            result.append("DISTINCT ");
            static_assert(Op::field_count == 1, "COUNT(DISTINCT) requires exactly one field");
            result.append(std::meta::identifier_of(Op::field_infos[0]));
        } else if constexpr (Op::field_count == 0) {
            result.append("*");
        } else {
            for (size_t i = 0; i < Op::field_count; ++i) {
                if (i > 0) result.append(" + ");
                result.append(std::meta::identifier_of(Op::field_infos[i]));
            }
        }

        result.append(")");
        return result;
    }

    // Build comma-separated list of aggregate operations
    template <typename... Ops, size_t... Is>
    consteval auto build_ops_list(std::index_sequence<Is...> /*unused*/) {
        ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
        (([&result]() -> void {
             if constexpr (Is > 0) result.append(", ");
             constexpr auto op_sql = build_single_op_sql<std::tuple_element_t<Is, std::tuple<Ops...>>>();
             result.append(op_sql);
         }()), ...);
        return result;
    }
    // LCOV_EXCL_STOP

    // Result type helper - SUM/COUNT return int64_t, AVG/MIN/MAX return double
    template <typename Op>
    using OpResult = std::conditional_t<
            Op::agg_type == AggregateType::SUM || Op::agg_type == AggregateType::COUNT,
            int64_t,
            double>;

    // ============================================================================
    // AggregateStatement - Unified aggregate queries with optional WHERE/JOIN
    // ============================================================================
    // Supports both single and multiple aggregates, with optional WHERE and JOIN.
    //
    // Usage:
    //   qs.count().select()                              // Single aggregate
    //   qs.where(age > 30).sum<^^T::salary>().select()   // With WHERE
    //   qs.aggregate().sum<^^T::a>().count().select()    // Multiple aggregates
    //
    template <typename T, storm::db::DatabaseConnection ConnType, typename... Ops>
    class AggregateStatement : private BaseStatement<T> {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Deduce result type: single scalar for 1 op, tuple for multiple
        template <size_t... Is>
        static consteval auto deduce_result_type(std::index_sequence<Is...> /*unused*/) {
            if constexpr (sizeof...(Ops) == 0) {
                struct EmptyTag {};
                return EmptyTag{};
            } else if constexpr (sizeof...(Ops) == 1) {
                return OpResult<std::tuple_element_t<0, std::tuple<Ops...>>>{};
            } else {
                return std::tuple<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>...>{};
            }
        }

      public:
        using ResultType = decltype(deduce_result_type(std::make_index_sequence<sizeof...(Ops)>{}));

        explicit AggregateStatement(
                std::shared_ptr<ConnType>                  conn,
                orm::where::ExpressionVariantPtr           where_expr = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt  = std::nullopt
        )
            : conn_(std::move(conn)), where_expr_(std::move(where_expr)), join_stmt_(join_stmt) {}

        // Chaining methods for building multiple aggregates
        template <std::meta::info... FieldInfos>
        auto sum() {
            return AggregateStatement<T, ConnType, Ops..., AggregateOp<AggregateType::SUM, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_};
        }

        template <std::meta::info... FieldInfos>
        auto count() {
            return AggregateStatement<T, ConnType, Ops..., AggregateOp<AggregateType::COUNT, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_};
        }

        template <std::meta::info... FieldInfos>
        auto avg() {
            return AggregateStatement<T, ConnType, Ops..., AggregateOp<AggregateType::AVG, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_};
        }

        template <std::meta::info... FieldInfos>
        auto min() {
            return AggregateStatement<T, ConnType, Ops..., AggregateOp<AggregateType::MIN, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_};
        }

        template <std::meta::info... FieldInfos>
        auto max() {
            return AggregateStatement<T, ConnType, Ops..., AggregateOp<AggregateType::MAX, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_};
        }

        // Execute the query
        [[nodiscard]] auto select() -> std::expected<ResultType, Error>
            requires(sizeof...(Ops) > 0)
        {
            return execute();
        }

        [[nodiscard]] auto execute() -> std::expected<ResultType, Error>
            requires(sizeof...(Ops) > 0)
        {
            return Base::dispatch_execute(
                    join_stmt_.has_value(),
                    static_cast<bool>(where_expr_),
                    [this]() -> std::expected<ResultType, Error> { return execute_simple(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_join(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_join(); }
            );
        }

      private:
        // Build full SELECT statement
        static consteval auto build_sql() {
            ConstexprString<2048> result;
            result.append("SELECT ");
            constexpr auto ops_sql = build_ops_list<Ops...>(std::make_index_sequence<sizeof...(Ops)>{});
            result.append(ops_sql);
            result.append(" FROM ");
            result.append(Base::get_table_name());
            return result;
        }

        static constexpr auto sql_array     = build_sql();
        static constexpr auto ops_sql_array = build_ops_list<Ops...>(std::make_index_sequence<sizeof...(Ops)>{});

        // Extract result value based on type
        template <typename ResultT>
        static auto extract_result(Statement& stmt, int col_idx) -> ResultT {
            if constexpr (std::is_same_v<ResultT, int64_t>) {
                return stmt.extract_int64(col_idx);
            } else {
                return stmt.extract_double(col_idx);
            }
        }

        // Common extraction logic
        template <size_t... Is>
        [[nodiscard]] auto extract_results(Statement* stmt, std::index_sequence<Is...> /*unused*/)
                -> std::expected<ResultType, Error> {
            int step_result = stmt->step_raw();

            if (step_result != Statement::ROW_AVAILABLE) {
                stmt->reset();
                if (step_result == Statement::NO_MORE_ROWS) {
                    if constexpr (sizeof...(Ops) == 1) {
                        return ResultType{};
                    } else {
                        return ResultType{OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>{}...};
                    }
                }
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            ResultType result;
            if constexpr (sizeof...(Ops) == 1) {
                result = extract_result<ResultType>(*stmt, 0);
            } else {
                result = ResultType{extract_result<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>>(*stmt, Is)...};
            }

            stmt->reset();
            return result;
        }

        // Simple execution (no WHERE, no JOIN)
        [[nodiscard]] __attribute__((hot)) auto execute_simple() -> std::expected<ResultType, Error> {
            static const std::string sql{sql_array.data.data(), sql_array.len};

            if (cached_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_stmt_ = *prepare_result;
            }

            return extract_results(cached_stmt_, std::make_index_sequence<sizeof...(Ops)>{});
        }

        // Execution with WHERE
        [[nodiscard]] __attribute__((hot)) auto execute_where() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

            std::string sql;
            sql.reserve(base_sql.size() + utilities::sql_len::LARGE_BUFFER);
            sql = base_sql;
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return extract_results(*prepare_result, std::make_index_sequence<sizeof...(Ops)>{});
        }

        // Execution with JOIN
        [[nodiscard]] __attribute__((hot)) auto execute_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_sql();

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return extract_results(*prepare_result, std::make_index_sequence<sizeof...(Ops)>{});
        }

        // Execution with WHERE and JOIN
        [[nodiscard]] __attribute__((hot)) auto execute_where_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_sql();
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            auto bind_result = Base::template bind_where_params<Statement, Error>(*prepare_result, where_expr_);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            return extract_results(*prepare_result, std::make_index_sequence<sizeof...(Ops)>{});
        }

        // Build aggregate SQL with JOIN
        [[nodiscard]] auto build_join_sql() const -> std::string {
            const std::string& join_sql = join_stmt_->get_complete_sql();

            const size_t from_pos = join_sql.find(" FROM ");
            if (from_pos == std::string::npos) [[unlikely]] {
                return std::string{sql_array.data.data(), sql_array.len};
            }

            static const std::string ops_clause{ops_sql_array.data.data(), ops_sql_array.len};

            std::string result;
            result.reserve(utilities::sql_len::SELECT + ops_clause.size() + (join_sql.size() - from_pos));
            result = "SELECT ";
            result += ops_clause;
            result += join_sql.substr(from_pos);

            return result;
        }

        std::shared_ptr<ConnType>           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        mutable Statement*                  cached_stmt_ = nullptr;
    };

    // Type alias for backward compatibility with queryset's aggregate() method
    template <typename T, storm::db::DatabaseConnection ConnType, typename... Ops>
    using AggregateBuilder = AggregateStatement<T, ConnType, Ops...>;

    // ============================================================================
    // GroupByAggregateStatement - GROUP BY with aggregates
    // ============================================================================
    // Returns multiple rows: one per unique group key combination.
    //
    // Usage:
    //   qs.group_by<^^Person::department>().count().select()
    //   qs.group_by<^^Person::dept, ^^Person::role>().sum<^^Person::salary>().select()
    //
    template <typename T, storm::db::DatabaseConnection ConnType, typename GroupByFields, typename... Ops>
        requires(sizeof...(Ops) > 0)
    class GroupByAggregateStatement : private BaseStatement<T> {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t NumGroupFields = GroupByFields::size;

        // Get field type for group by column
        template <size_t Idx>
        struct GroupFieldType {
            static constexpr auto field_info = GroupByFields::template at<Idx>();
            using type = std::remove_cvref_t<decltype(std::declval<T>().[:field_info:])>;
        };

        // Build the combined result type: tuple<GroupKeys..., AggResults...>
        template <size_t... GIs, size_t... AIs>
        static consteval auto build_tuple_type(std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/) {
            return std::tuple<
                    typename GroupFieldType<GIs>::type...,
                    OpResult<std::tuple_element_t<AIs, std::tuple<Ops...>>>...>{};
        }

        using TupleType = decltype(build_tuple_type(
                std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}));

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

        [[nodiscard]] auto select() -> std::expected<ResultType, Error> { return execute(); }

        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            return Base::dispatch_execute(
                    join_stmt_.has_value(),
                    static_cast<bool>(where_expr_),
                    [this]() -> std::expected<ResultType, Error> { return execute_simple(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_join(); },
                    [this]() -> std::expected<ResultType, Error> { return execute_where_join(); }
            );
        }

      private:
        // LCOV_EXCL_START - compile-time only
        template <size_t... Is>
        static consteval auto build_group_fields(std::index_sequence<Is...> /*unused*/) {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            (([&result]() -> void {
                 if constexpr (Is > 0) result.append(", ");
                 result.append(std::meta::identifier_of(GroupByFields::template at<Is>()));
             }()), ...);
            return result;
        }

        template <size_t... GIs, size_t... AIs>
        static consteval auto build_select_clause(std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/) {
            ConstexprString<2048> result;

            // Group by fields
            (([&result]() -> void {
                 if constexpr (GIs > 0) result.append(", ");
                 result.append(std::meta::identifier_of(GroupByFields::template at<GIs>()));
             }()), ...);

            // Aggregate functions
            (([&result]() -> void {
                 result.append(", ");
                 constexpr auto op_sql = build_single_op_sql<std::tuple_element_t<AIs, std::tuple<Ops...>>>();
                 result.append(op_sql);
             }()), ...);

            return result;
        }

        static consteval auto build_sql() {
            ConstexprString<utilities::buffer_size::SQL_LARGE> result;
            result.append("SELECT ");
            constexpr auto select_clause = build_select_clause(
                    std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{});
            result.append(select_clause);
            result.append(" FROM ");
            result.append(Base::get_table_name());
            result.append(" GROUP BY ");
            constexpr auto group_fields = build_group_fields(std::make_index_sequence<NumGroupFields>{});
            result.append(group_fields);
            return result;
        }
        // LCOV_EXCL_STOP

        static constexpr auto sql_array          = build_sql();
        static constexpr auto select_clause_arr  = build_select_clause(
                std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{});
        static constexpr auto group_fields_arr   = build_group_fields(std::make_index_sequence<NumGroupFields>{});

        // Static string cache (initialized once)
        static inline const std::string select_clause_{select_clause_arr.data.data(), select_clause_arr.len};
        static inline const std::string group_clause_{group_fields_arr.data.data(), group_fields_arr.len};

        template <typename FieldType>
        static auto extract_column(Statement& stmt, int col_idx) -> FieldType {
            return Base::template extract_column_value<FieldType>(stmt, col_idx);
        }

        template <size_t... GIs, size_t... AIs>
        static auto extract_row(Statement& stmt, std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/)
                -> TupleType {
            return TupleType{
                    extract_column<typename GroupFieldType<GIs>::type>(stmt, GIs)...,
                    extract_column<OpResult<std::tuple_element_t<AIs, std::tuple<Ops...>>>>(stmt, NumGroupFields + AIs)...};
        }

        [[nodiscard]] auto execute_query_loop(Statement* stmt) -> std::expected<ResultType, Error> {
            ResultType results;
            int        step_result = Statement::NO_MORE_ROWS;

            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                results.insert(extract_row(
                        *stmt, std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<sizeof...(Ops)>{}));
            }

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        [[nodiscard]] auto execute_simple() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

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

        [[nodiscard]] auto execute_where() -> std::expected<ResultType, Error> {
            static const std::string base_sql{sql_array.data.data(), sql_array.len};

            std::string sql;
            sql.reserve(base_sql.size() + utilities::sql_len::XL_BUFFER);

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

        // Build base SQL with JOIN (without GROUP BY or WHERE)
        [[nodiscard]] auto build_join_base_sql() const -> std::string {
            const std::string& join_sql = join_stmt_->get_complete_sql();

            size_t from_pos = join_sql.find(" FROM ");
            if (from_pos == std::string::npos) [[unlikely]] {
                return {};
            }

            std::string sql;
            sql.reserve(select_clause_.size() + join_sql.size() + utilities::sql_len::MEDIUM_BUFFER);
            sql = "SELECT ";
            sql += select_clause_;
            sql += join_sql.substr(from_pos);

            return sql;
        }

        [[nodiscard]] auto execute_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_base_sql();
            sql += " GROUP BY ";
            sql += group_clause_;
            Base::append_order_by(sql, order_by_wrapper_);
            Base::append_limit_offset(sql, limit_, offset_);

            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            return execute_query_loop(*prepare_result);
        }

        [[nodiscard]] auto execute_where_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_base_sql();
            sql += " WHERE ";
            sql += orm::where::to_sql(*where_expr_);
            sql += " GROUP BY ";
            sql += group_clause_;
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

        template <std::meta::info... FieldInfos>
        auto count() {
            return GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::COUNT, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        template <std::meta::info... FieldInfos>
        auto sum() {
            return GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::SUM, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        template <std::meta::info... FieldInfos>
        auto avg() {
            return GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::AVG, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        template <std::meta::info... FieldInfos>
        auto min() {
            return GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::MIN, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
        }

        template <std::meta::info... FieldInfos>
        auto max() {
            return GroupByAggregateStatement<T, ConnType, GroupFields, AggregateOp<AggregateType::MAX, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_};
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
