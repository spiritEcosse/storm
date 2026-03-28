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
        using enum AggregateType;
        switch (type) {
        case SUM:
            return "SUM";
        case COUNT:
            return "COUNT";
        case AVG:
            return "AVG";
        case MIN:
            return "MIN";
        case MAX:
            return "MAX";
        case COUNT_DISTINCT:
            return "COUNT";
        }
        return "";
    }
    // LCOV_EXCL_STOP

    // Aggregate operation descriptor
    template <AggregateType Type, std::meta::info... FieldInfos> struct AggregateOp {
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

    // Empty marker for no GROUP BY
    struct NoGroupBy {
        static constexpr size_t size = 0;
    };

    // GROUP BY fields holder
    template <std::meta::info... Infos> struct GroupByFields {
        static constexpr size_t                                        size = sizeof...(Infos);
        static constexpr std::array<std::meta::info, sizeof...(Infos)> values{Infos...};

        // LCOV_EXCL_START - compile-time only
        template <size_t I> static consteval auto at() -> std::meta::info {
            static_assert(I < size, "Index out of bounds");
            return values[I];
        }
        // LCOV_EXCL_STOP
    };

    // ============================================================================
    // Compile-time SQL Building Utilities
    // ============================================================================

    // LCOV_EXCL_START - compile-time only
    template <typename Op> consteval auto build_single_op_sql() {
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
                if (i > 0) {
                    result.append(" + ");
                }
                result.append(std::meta::identifier_of(Op::field_infos[i]));
            }
        }

        result.append(")");
        return result;
    }

    template <typename... Ops, size_t... Is> consteval auto build_ops_list(std::index_sequence<Is...> /*unused*/) {
        ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
        (([&result]() {
             if constexpr (Is > 0) {
                 result.append(", ");
             }
             constexpr auto op_sql = build_single_op_sql<std::tuple_element_t<Is, std::tuple<Ops...>>>();
             result.append(op_sql);
         }()),
         ...);
        return result;
    }

    template <typename GroupFields, size_t... Is>
    consteval auto build_group_fields_sql(std::index_sequence<Is...> /*unused*/) {
        ConstexprString<utilities::buffer_size::SQL_SMALL> result;
        (([&result]() {
             if constexpr (Is > 0) {
                 result.append(", ");
             }
             result.append(std::meta::identifier_of(GroupFields::template at<Is>()));
         }()),
         ...);
        return result;
    }
    // LCOV_EXCL_STOP

    // Result type: SUM/COUNT -> int64_t, AVG/MIN/MAX -> double
    template <typename Op>
    using OpResult = std::
            conditional_t<Op::agg_type == AggregateType::SUM || Op::agg_type == AggregateType::COUNT, int64_t, double>;

    // ============================================================================
    // AggregateStatement - Single class for all aggregate queries
    // ============================================================================
    template <typename T, storm::db::DatabaseConnection ConnType, typename GroupFields, typename... Ops>
    class AggregateStatement : private BaseStatement<T> {
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr bool   HasGroupBy     = !std::is_same_v<GroupFields, NoGroupBy>;
        static constexpr size_t NumGroupFields = GroupFields::size;
        static constexpr size_t NumOps         = sizeof...(Ops);

        // ---- Result Type Deduction ----
        template <size_t Idx> struct GroupFieldType {
            static constexpr auto field_info = GroupFields::template at<Idx>();
            using type                       = std::remove_cvref_t<decltype(std::declval<T>().[:field_info:])>;
        };

        // LCOV_EXCL_START - compile-time only
        template <size_t... GIs, size_t... AIs>
        static consteval auto
        deduce_grouped_type(std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/) {
            return std::tuple<
                    typename GroupFieldType<GIs>::type...,
                    OpResult<std::tuple_element_t<AIs, std::tuple<Ops...>>>...>{};
        }

        static consteval auto deduce_simple_type() {
            if constexpr (NumOps == 1) {
                return OpResult<std::tuple_element_t<0, std::tuple<Ops...>>>{};
            } else {
                return []<size_t... Is>(std::index_sequence<Is...> /*unused*/)
                               -> std::tuple<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>...> {
                    return std::tuple<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>...>{};
                }(std::make_index_sequence<NumOps>{});
            }
        }
        // LCOV_EXCL_STOP

        using GroupedTuple = decltype(deduce_grouped_type(
                std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<NumOps>{}
        ));

      public:
        using ResultType = std::conditional_t<HasGroupBy, plf::hive<GroupedTuple>, decltype(deduce_simple_type())>;

        explicit AggregateStatement(
                ConnType*                                  conn,
                orm::where::ExpressionVariantPtr           where_expr       = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt        = std::nullopt,
                const std::optional<int>&                  limit            = std::nullopt,
                const std::optional<int>&                  offset           = std::nullopt,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt,
                orm::where::ExpressionVariantPtr           having_expr      = nullptr
        )
            : conn_(conn)
            , where_expr_(std::move(where_expr))
            , join_stmt_(join_stmt)
            , limit_(limit)
            , offset_(offset)
            , order_by_wrapper_(order_by_wrapper)
            , having_expr_(std::move(having_expr)) {}

        // HAVING clause - only available when GROUP BY is present
        auto having(orm::where::ExpressionVariantPtr expr)
            requires HasGroupBy
        {
            return AggregateStatement<T, ConnType, GroupFields, Ops...>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, std::move(expr)
            };
        }

        // Chaining methods (only for non-GROUP BY queries building aggregates)
        template <std::meta::info... FieldInfos> auto sum() {
            return AggregateStatement<T, ConnType, GroupFields, Ops..., AggregateOp<AggregateType::SUM, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto count() {
            return AggregateStatement<
                    T,
                    ConnType,
                    GroupFields,
                    Ops...,
                    AggregateOp<AggregateType::COUNT, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto avg() {
            return AggregateStatement<T, ConnType, GroupFields, Ops..., AggregateOp<AggregateType::AVG, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto min() {
            return AggregateStatement<T, ConnType, GroupFields, Ops..., AggregateOp<AggregateType::MIN, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto max() {
            return AggregateStatement<T, ConnType, GroupFields, Ops..., AggregateOp<AggregateType::MAX, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        // Scalar aggregate: no GROUP BY → returns single value via .get()
        [[nodiscard]] __attribute__((always_inline)) auto get() -> std::expected<ResultType, Error>
            requires(NumOps > 0 && !HasGroupBy)
        {
            return execute();
        }

        // Grouped aggregate: GROUP BY → returns collection via .select()
        [[nodiscard]] __attribute__((always_inline)) auto select() -> std::expected<ResultType, Error>
            requires(NumOps > 0 && HasGroupBy)
        {
            return execute();
        }

        [[nodiscard]] __attribute__((flatten)) auto execute() -> std::expected<ResultType, Error>
            requires(NumOps > 0)
        {
            // Inline dispatch to avoid lambda capture + call overhead on hot path
            if (join_stmt_.has_value() && where_expr_) {
                return execute_where_join();
            }
            if (join_stmt_.has_value()) {
                return execute_join();
            }
            if (where_expr_) {
                return execute_where();
            }
            return execute_simple();
        }

      private:
        // ---- Compile-time SQL Generation ----
        // LCOV_EXCL_START - compile-time only
        static consteval auto build_select_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;

            if constexpr (HasGroupBy) {
                constexpr auto group_sql =
                        build_group_fields_sql<GroupFields>(std::make_index_sequence<NumGroupFields>{});
                result.append(group_sql);
                result.append(", ");
            }

            constexpr auto ops_sql = build_ops_list<Ops...>(std::make_index_sequence<NumOps>{});
            result.append(ops_sql);
            return result;
        }

        static consteval auto build_base_sql() {
            ConstexprString<utilities::buffer_size::SQL_LARGE> result;
            result.append("SELECT ");
            constexpr auto select_clause = build_select_clause();
            result.append(select_clause);
            result.append(" FROM ");
            result.append(Base::get_table_name());

            if constexpr (HasGroupBy) {
                result.append(" GROUP BY ");
                constexpr auto group_sql =
                        build_group_fields_sql<GroupFields>(std::make_index_sequence<NumGroupFields>{});
                result.append(group_sql);
            }
            return result;
        }
        // LCOV_EXCL_STOP

        static constexpr auto sql_array_         = build_base_sql();
        static constexpr auto select_clause_arr_ = build_select_clause();
        static constexpr auto group_fields_arr_ =
                build_group_fields_sql<GroupFields>(std::make_index_sequence<NumGroupFields>{});

        // Static string cache
        static inline const std::string base_sql_{sql_array_.data.data(), sql_array_.len};
        static inline const std::string select_clause_{select_clause_arr_.data.data(), select_clause_arr_.len};
        static inline const std::string group_clause_{group_fields_arr_.data.data(), group_fields_arr_.len};

        // Simple aggregate extraction (single row)
        template <size_t... Is>
        [[nodiscard]] auto extract_simple_result(Statement* stmt, std::index_sequence<Is...> /*unused*/)
                -> std::expected<ResultType, Error> {
            int step_result = stmt->step_raw();

            // LCOV_EXCL_START — LLVM instrumentation bug: counters don't fire despite
            // code executing (verified via fprintf). if constexpr branches also untraceable.
            // Tested by mock: AggregateWithWhereStepErrorInExtractSimpleResult,
            //                 AggregateWithWhereStepNoRowsInExtractSimpleResult
            if (step_result != Statement::ROW_AVAILABLE) {
                stmt->reset();
                if (step_result == Statement::NO_MORE_ROWS) {
                    if constexpr (NumOps == 1) {
                        return ResultType{};
                    } else {
                        return ResultType{OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>{}...};
                    }
                }
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }
            // LCOV_EXCL_STOP

            ResultType result;
            // LCOV_EXCL_START — if constexpr: only one branch instantiated per NumOps
            if constexpr (NumOps == 1) {
                result = Base::template extract_column_value<ResultType>(stmt, 0);
            } else {
                result = ResultType{
                        Base::template extract_column_value<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>>(
                                stmt, Is
                        )...
                };
            }
            // LCOV_EXCL_STOP

            stmt->reset();
            return result;
        }

        // GROUP BY extraction (multiple rows)
        template <size_t... GIs, size_t... AIs>
        static auto extract_grouped_row(
                Statement* stmt, std::index_sequence<GIs...> /*unused*/, std::index_sequence<AIs...> /*unused*/
        ) -> GroupedTuple {
            return GroupedTuple{
                    Base::template extract_column_value<typename GroupFieldType<GIs>::type>(stmt, GIs)...,
                    Base::template extract_column_value<OpResult<std::tuple_element_t<AIs, std::tuple<Ops...>>>>(
                            stmt, NumGroupFields + AIs
                    )...
            };
        }

        [[nodiscard]] auto extract_grouped_results(Statement* stmt) -> std::expected<ResultType, Error> {
            ResultType results;
            int        step_result = Statement::NO_MORE_ROWS;

            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                results.insert(extract_grouped_row(
                        stmt, std::make_index_sequence<NumGroupFields>{}, std::make_index_sequence<NumOps>{}
                ));
            }

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        // ---- Unified Extraction Dispatcher ----
        [[nodiscard]] auto extract_results(Statement* stmt) -> std::expected<ResultType, Error> {
            if constexpr (HasGroupBy) {
                return extract_grouped_results(stmt);
            } else {
                return extract_simple_result(stmt, std::make_index_sequence<NumOps>{});
            }
        }

        // ---- Inline Helpers ----
        void insert_where_clause(std::string& sql) const {
            if constexpr (HasGroupBy) {
                size_t const group_pos    = sql.find(" GROUP BY ");
                std::string  where_clause = " WHERE ";
                where_clause += orm::where::to_sql(*where_expr_);
                sql.insert(group_pos, where_clause);
            } else {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr_);
            }
        }

        void insert_having_clause(std::string& sql) const {
            sql += " HAVING ";
            sql += orm::where::to_sql(*having_expr_);
        }

        void append_modifiers(std::string& sql) const {
            Base::template append_order_by<ConnType>(sql, order_by_wrapper_);
            Base::template append_limit_offset<ConnType>(sql, limit_, offset_);
        }

        [[nodiscard]] auto prepare_and_extract(const std::string& sql) -> std::expected<ResultType, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            return extract_results(*prepare_result);
        }

        [[nodiscard]] auto prepare_bind_extract(const std::string& sql) -> std::expected<ResultType, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            int  param_index = 1;
            auto bind_result = // NOSONAR(S1659)
                    orm::where::bind_params_direct<Statement, Error>(*where_expr_, *prepare_result, param_index);
            if (!bind_result) [[unlikely]] {
                (*prepare_result)->reset();
                return std::unexpected(bind_result.error());
            }
            if (having_expr_) {
                auto having_bind =
                        Base::template bind_having_params<Statement, Error>(*prepare_result, having_expr_, param_index);
                if (!having_bind) [[unlikely]] {
                    return std::unexpected(having_bind.error());
                }
            }
            return extract_results(*prepare_result);
        }

        [[nodiscard]] auto prepare_bind_having_extract(const std::string& sql) -> std::expected<ResultType, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            int  param_index = 1;
            auto having_bind =
                    Base::template bind_having_params<Statement, Error>(*prepare_result, having_expr_, param_index);
            if (!having_bind) [[unlikely]] {
                return std::unexpected(having_bind.error());
            }
            return extract_results(*prepare_result);
        }

        // ---- SQL Builders ----
        [[nodiscard]] auto build_join_sql() const -> std::string {
            const std::string& join_sql = join_stmt_->get_complete_sql();
            // join_sql always contains " FROM " — built at compile time by
            // JoinStatement::build_complete_sql_array() which unconditionally appends it.
            const size_t from_pos = join_sql.find(" FROM ");

            std::string result;
            result.reserve(select_clause_.size() + join_sql.size() + utilities::sql_len::MEDIUM_BUFFER);
            result = "SELECT ";
            result += select_clause_;
            result += join_sql.substr(from_pos);

            if constexpr (HasGroupBy) {
                result += " GROUP BY ";
                result += group_clause_;
            }

            return result;
        }

        // ---- Inline extraction for simple aggregate (no trailing reset) ----
        // OPTIMIZATION: Step + extract single result row without trailing reset.
        // The next call will reset at the top, eliminating double-reset overhead.
        template <size_t... Is>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_simple_no_reset(Statement* stmt, std::index_sequence<Is...> /*unused*/)
                -> std::expected<ResultType, Error> {
            int step_result = stmt->step_raw();

            if (step_result != Statement::ROW_AVAILABLE) [[unlikely]] {
                stmt->reset();
                if (step_result == Statement::NO_MORE_ROWS) {
                    return ResultType{};
                }
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            if constexpr (NumOps == 1) {
                return Base::template extract_column_value<ResultType>(stmt, 0);
            } else {
                return ResultType{
                        Base::template extract_column_value<OpResult<std::tuple_element_t<Is, std::tuple<Ops...>>>>(
                                stmt, Is
                        )...
                };
            }
        }

        // ---- Execution Paths ----
        [[nodiscard]] __attribute__((hot)) auto execute_simple() -> std::expected<ResultType, Error> {
            if constexpr (HasGroupBy) {
                const bool has_modifiers = order_by_wrapper_.has_value() || limit_.has_value() || offset_.has_value();
                if (having_expr_) {
                    std::string sql = base_sql_;
                    insert_having_clause(sql);
                    if (has_modifiers) {
                        append_modifiers(sql);
                    }
                    return prepare_bind_having_extract(sql);
                }
                if (has_modifiers) {
                    std::string sql = base_sql_;
                    append_modifiers(sql);
                    return prepare_and_extract(sql);
                }
                return prepare_and_extract(base_sql_);
            } else {
                // OPTIMIZATION: Thread-local statement pointer cache.
                // Persists across AggregateStatement instances to avoid
                // hash-map lookup in prepare_cached() on every call.
                // Safe because connections are thread-local (project design invariant).
                static thread_local Statement* tl_stmt = nullptr;
                static thread_local void*      tl_conn = nullptr;

                if (tl_conn == static_cast<void*>(conn_) && tl_stmt != nullptr) [[likely]] {
                    // Fast path: reuse cached pointer, just reset
                    tl_stmt->reset();
                } else {
                    // Slow path: first call or connection changed
                    auto prepare_result = conn_->prepare_cached(base_sql_);
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    tl_stmt = *prepare_result;
                    tl_conn = static_cast<void*>(conn_);
                    // prepare_cached already called reset()
                }

                // Extract without trailing reset (next call resets at the top)
                return extract_simple_no_reset(tl_stmt, std::make_index_sequence<NumOps>{});
            }
        }

        [[nodiscard]] __attribute__((hot)) auto execute_where() -> std::expected<ResultType, Error> {
            std::string sql = base_sql_;
            insert_where_clause(sql);
            if constexpr (HasGroupBy) {
                if (having_expr_) {
                    insert_having_clause(sql);
                }
                append_modifiers(sql);
            }
            return prepare_bind_extract(sql);
        }

        [[nodiscard]] __attribute__((hot)) auto execute_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_sql();
            if constexpr (HasGroupBy) {
                if (having_expr_) {
                    insert_having_clause(sql);
                }
                append_modifiers(sql);
            }
            if (having_expr_) {
                return prepare_bind_having_extract(sql);
            }
            return prepare_and_extract(sql);
        }

        [[nodiscard]] __attribute__((hot)) auto execute_where_join() -> std::expected<ResultType, Error> {
            std::string sql = build_join_sql();
            insert_where_clause(sql);
            if constexpr (HasGroupBy) {
                if (having_expr_) {
                    insert_having_clause(sql);
                }
                append_modifiers(sql);
            }
            return prepare_bind_extract(sql);
        }

        ConnType*                           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
        std::optional<OrderByWrapper>       order_by_wrapper_;
        orm::where::ExpressionVariantPtr    having_expr_;
    };

    // ============================================================================
    // GroupByBuilder - Fluent builder for GROUP BY queries
    // ============================================================================
    // Provides a fluent interface for building GROUP BY aggregate queries.
    // Created by QuerySet::group_by<>() and returns AggregateStatement
    // when an aggregate method (count, sum, avg, min, max) is called.
    // ============================================================================
    template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... GroupFieldInfos>
        requires(sizeof...(GroupFieldInfos) > 0)
    class GroupByBuilder {
        using GBFields = GroupByFields<GroupFieldInfos...>;

      public:
        explicit GroupByBuilder(
                ConnType*                                  conn,
                orm::where::ExpressionVariantPtr           where_expr       = nullptr,
                const std::optional<JoinStatementWrapper>& join_stmt        = std::nullopt,
                const std::optional<int>&                  limit            = std::nullopt,
                const std::optional<int>&                  offset           = std::nullopt,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt,
                orm::where::ExpressionVariantPtr           having_expr      = nullptr
        )
            : conn_(conn)
            , where_expr_(std::move(where_expr))
            , join_stmt_(join_stmt)
            , limit_(limit)
            , offset_(offset)
            , order_by_wrapper_(order_by_wrapper)
            , having_expr_(std::move(having_expr)) {}

        // HAVING clause - stores expression and returns new GroupByBuilder
        auto having(orm::where::ExpressionVariantPtr expr) {
            return GroupByBuilder<T, ConnType, GroupFieldInfos...>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, std::move(expr)
            };
        }

        template <std::meta::info... FieldInfos> auto count() {
            return AggregateStatement<T, ConnType, GBFields, AggregateOp<AggregateType::COUNT, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto sum() {
            return AggregateStatement<T, ConnType, GBFields, AggregateOp<AggregateType::SUM, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto avg() {
            return AggregateStatement<T, ConnType, GBFields, AggregateOp<AggregateType::AVG, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto min() {
            return AggregateStatement<T, ConnType, GBFields, AggregateOp<AggregateType::MIN, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

        template <std::meta::info... FieldInfos> auto max() {
            return AggregateStatement<T, ConnType, GBFields, AggregateOp<AggregateType::MAX, FieldInfos...>>{
                    conn_, where_expr_, join_stmt_, limit_, offset_, order_by_wrapper_, having_expr_
            };
        }

      private:
        ConnType*                           conn_;
        orm::where::ExpressionVariantPtr    where_expr_;
        std::optional<JoinStatementWrapper> join_stmt_;
        std::optional<int>                  limit_;
        std::optional<int>                  offset_;
        std::optional<OrderByWrapper>       order_by_wrapper_;
        orm::where::ExpressionVariantPtr    having_expr_;
    };

} // namespace storm::orm::statements
