module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_aggregate;

import storm_db_concept;
import storm_orm_statements_base;
import storm_orm_utilities;

import <expected>;
import <string>;
import <vector>;
import <tuple>;
import <array>;
import <meta>;
import <cstdint>;

export namespace storm::orm::statements {

    // Import utilities for compile-time SQL generation
    using storm::orm::utilities::ConstexprString;

    // Aggregate function types
    enum class AggregateType { SUM, COUNT, AVG, MIN, MAX };

    // Helper to get SQL function name from AggregateType
    constexpr std::string_view get_agg_function_name(AggregateType type) {
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
        }
        return "";
    }

    // Aggregate operation descriptor
    template <AggregateType Type, std::meta::info... FieldInfos> struct AggregateOp {
        static constexpr AggregateType agg_type    = Type;
        static constexpr size_t        field_count = sizeof...(FieldInfos);
        static constexpr auto          get_field_infos() {
            if constexpr (sizeof...(FieldInfos) > 0) {
                return std::array{FieldInfos...};
            } else {
                return std::array<std::meta::info, 0>{};
            }
        }
        static constexpr auto field_infos = get_field_infos();
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
        template <size_t... Is> static consteval auto deduce_result_type_helper(std::index_sequence<Is...>) {
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

        explicit AggregateBuilder(ConnType& conn) : conn_(conn) {}

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

            ConstexprString<512> result;
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
        template <size_t... Is> static consteval auto build_aggregate_sql(std::index_sequence<Is...>) {
            ConstexprString<2048> result;
            result.append("SELECT ");

            // Add each operation
            (([&result]() {
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
        template <typename ResultT> static ResultT extract_result(Statement& stmt, int col_idx) {
            if constexpr (std::is_same_v<ResultT, int64_t>) {
                return stmt.extract_int64(col_idx);
            } else if constexpr (std::is_same_v<ResultT, double>) {
                return stmt.extract_double(col_idx);
            }
        }

        // Execute and extract results
        template <size_t... Is> auto execute_impl(std::index_sequence<Is...>) -> std::expected<ResultType, Error> {
            static const std::string sql{sql_array.data.data(), sql_array.len};

            // Cache statement on first use
            if (!cached_stmt_) {
                auto prepare_result = conn_.prepare_cached(sql);
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

        ConnType&          conn_;
        mutable Statement* cached_stmt_ = nullptr;
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
        explicit SingleAggregateStatement(ConnType& conn) : builder_(conn) {}

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

} // namespace storm::orm::statements
