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
        template <size_t... Is> static consteval auto get_field_types_helper(std::index_sequence<Is...> /*unused*/) {
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
        template <size_t... Is>
        static consteval auto calculate_field_list_size(std::index_sequence<Is...> /*unused*/) -> size_t {
            return (get_field_size<Is>() + ...);
        }

        // Compile-time field list generation (returns ConstexprString)
        template <size_t... Is>
        static consteval auto build_field_list_constexpr(std::index_sequence<Is...> /*unused*/) {
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
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
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

        // Alias for execute() - provides familiar QuerySet-like API
        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            return execute();
        }

        // Execute SELECT DISTINCT query on the specified field(s)
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute() -> std::expected<ResultType, Error> {
            static const std::string base_sql{distinct_sql_array.data.data(), distinct_sql_array.len};

            // Build SQL: base or JOIN with DISTINCT injected
            std::string sql;
            if (join_stmt_.has_value()) {
                // Inject DISTINCT into JOIN's SELECT clause
                const std::string& join_sql = join_stmt_->get_complete_sql();
                const size_t       pos      = join_sql.find("SELECT ");
                if (pos == std::string::npos) [[unlikely]] {
                    return std::unexpected(Error{-1, "JOIN SQL missing SELECT clause"});
                }
                sql.reserve(join_sql.size() + utilities::sql_len::LARGE_BUFFER);
                sql = join_sql.substr(0, pos + utilities::sql_len::SELECT);
                sql += "DISTINCT ";
                sql += join_sql.substr(pos + utilities::sql_len::SELECT);
            } else {
                sql = base_sql;
            }

            // Append WHERE clause if present
            if (where_expr_) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr_);
            }

            // Append ORDER BY, LIMIT, OFFSET
            if (order_by_wrapper_.has_value() && !order_by_wrapper_->empty()) {
                sql += order_by_wrapper_->get_order_by_sql();
            }
            if (limit_.has_value()) {
                sql += " LIMIT ";
                sql += std::to_string(limit_.value());
            } else if (offset_.has_value()) {
                sql += " LIMIT -1";
            }
            if (offset_.has_value()) {
                sql += " OFFSET ";
                sql += std::to_string(offset_.value());
            }

            // Prepare statement
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }

            // Bind WHERE parameters if present
            if (where_expr_) {
                int  param_index = 1;
                auto bind_result = orm::where::bind_params_direct(*where_expr_, *prepare_result, param_index);
                if (!bind_result) [[unlikely]] {
                    (*prepare_result)->reset();
                    return std::unexpected(bind_result.error());
                }
            }

            return execute_query_loop(*prepare_result);
        }

      private:
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

        // OPTIMIZATION: Insert tuple by extracting columns in-place
        // Constructs tuple directly in hive without intermediate temporaries
        // Template parameter R delays evaluation until method is called (avoids void& when NumFields == 0)
        template <size_t... Is, typename R = ResultType>
        void insert_tuple_from_columns(R& results, Statement* stmt, std::index_sequence<Is...> /*unused*/)
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
