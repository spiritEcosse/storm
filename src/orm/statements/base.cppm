module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_base;

import storm_db_concept;

import <expected>;
import <string>;
import <string_view>;
import <concepts>;
import <format>;
import <meta>;
import <array>;
import <utility>;

export namespace storm::orm::statements {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique };
    }

    // Shared reflection utilities for all statement types
    template <typename T> class BaseStatement {
      public:
        // Public accessors for optimization
        static constexpr auto get_primary_key() {
            return primary_key_;
        }
        static constexpr auto get_pk_name() {
            return pk_name_;
        }
        static constexpr auto get_table_name() {
            return table_name_;
        }

      protected:
        // Helper to find primary key using storm::meta attributes
        static consteval std::meta::info find_primary_key_impl() {
            for (std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
                    return member;
                }
            }
            throw "Model must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
        }

        // Common reflection data - computed once per template instantiation
        static constexpr auto primary_key_ = find_primary_key_impl();
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

        // Helper to get the number of fields
        static consteval size_t get_field_count() {
            return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
        }

        // Pre-compute all field members at compile-time
        template <size_t N> static consteval std::array<std::meta::info, N> get_all_field_members() {
            std::array<std::meta::info, N> result{};
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

            for (size_t i = 0; i < N && i < members.size(); ++i) {
                result[i] = members[i];
            }
            return result;
        }

      public:
        // Pre-computed field information - made public for QuerySet optimization
        static constexpr auto field_count_ = get_field_count();
        static constexpr auto all_members_ = get_all_field_members<field_count_>();

      protected:
        // Index sequence utilities for compile-time field binding
        using field_indices_t = std::make_index_sequence<field_count_>;

        // Helper template for compile-time field binding with index sequence
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto
        bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Use fold expression to bind all fields at compile-time indices
            // Each field binds at parameter index (Is + 1) since SQLite parameters start at 1
            auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, Is + 1) && ...);
            if (!bind_result) {
                // Find which field failed (this could be optimized further if needed)
                return get_first_bind_error<ConnType, Statement, Is...>(stmt, obj);
            }
            return {};
        }

        // Helper to bind a single field at compile-time index with error handling
        template <typename ConnType, size_t Index>
        [[nodiscard]] static auto
        bind_field_at_index(typename ConnType::Statement& stmt, const T& obj, int param_index) noexcept -> bool {
            if constexpr (Index < field_count_) {
                constexpr auto member      = all_members_[Index];
                auto           field_value = obj.[:member:];
                auto           result      = bind_value_by_type<ConnType>(stmt, param_index, field_value);
                return result.has_value();
            }
            return true;
        }

        // Helper to get the first binding error when fold expression fails
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto get_first_bind_error(Statement& stmt, const T& obj) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Try each field individually to find the first error
            auto try_bind = [&stmt, &obj](auto index_constant) -> std::expected<void, typename ConnType::Error> {
                constexpr size_t Index = decltype(index_constant)::value;
                if constexpr (Index < field_count_) {
                    constexpr auto member      = all_members_[Index];
                    auto           field_value = obj.[:member:];
                    return bind_value_by_type<ConnType>(stmt, Index + 1, field_value);
                }
                return {};
            };

            // Try each field to find the first error
            std::expected<void, typename ConnType::Error> first_error{};
            ((first_error = try_bind(std::integral_constant<size_t, Is>{}), first_error.has_value()) && ...);
            return first_error;
        }

        // Helper for bulk binding multiple objects with index sequence
        template <typename ConnType, typename Statement, typename ContainerType, size_t... Is>
        [[nodiscard]] static auto
        bind_all_objects_bulk_impl(Statement& stmt, const ContainerType& objects, std::index_sequence<Is...>) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;

            // Bind each object's fields sequentially
            for (const auto& obj : objects) {
                // Use fold expression to bind all fields for this object
                auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, param_index + Is) && ...);
                if (!bind_result) {
                    // Find which field failed for this object
                    return get_bulk_bind_error<ConnType, Statement, Is...>(stmt, obj, param_index);
                }
                param_index += field_count_; // Move to next object's parameter range
            }
            return {};
        }

        // Helper to get binding error for bulk operations
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto get_bulk_bind_error(Statement& stmt, const T& obj, int base_param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Try each field individually to find the first error
            auto try_bind = [&stmt,
                             &obj,
                             base_param_index](auto index_constant) -> std::expected<void, typename ConnType::Error> {
                constexpr size_t Index = decltype(index_constant)::value;
                if constexpr (Index < field_count_) {
                    constexpr auto member      = all_members_[Index];
                    auto           field_value = obj.[:member:];
                    return bind_value_by_type<ConnType>(stmt, base_param_index + Index, field_value);
                }
                return {};
            };

            // Try each field to find the first error
            std::expected<void, typename ConnType::Error> first_error{};
            ((first_error = try_bind(std::integral_constant<size_t, Is>{}), first_error.has_value()) && ...);
            return first_error;
        }

        // Common batch operation thresholds
        static constexpr size_t BATCH_THRESHOLD      = 50;
        static constexpr size_t MAX_SQLITE_VARIABLES = 999;

        // Common binding utilities for different types
        template <typename ConnType>
        [[nodiscard]] static auto
        bind_value_by_type(typename ConnType::Statement& stmt, int param_index, const auto& value) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Bind based on type - database-agnostic binding
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int>) {
                return stmt.bind_int(param_index, value);
            } else if constexpr (std::is_convertible_v<std::decay_t<decltype(value)>, std::string_view>) {
                return stmt.bind_text(param_index, std::string_view{value});
            } else {
                static_assert(
                        std::is_same_v<std::decay_t<decltype(value)>, int> ||
                                std::is_convertible_v<std::decay_t<decltype(value)>, std::string_view>,
                        "Unsupported field type for binding"
                );
            }
        }

        // Transaction management utilities
        template <typename ConnType>
        [[nodiscard]] static auto begin_transaction(ConnType& conn) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return conn.execute("BEGIN TRANSACTION");
        }

        template <typename ConnType>
        [[nodiscard]] static auto commit_transaction(ConnType& conn) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return conn.execute("COMMIT");
        }

        template <typename ConnType> static void rollback_transaction(ConnType& conn) noexcept {
            (void)conn.execute("ROLLBACK");
        }

        // Utility to determine if transaction should be used
        template <typename ContainerType> static constexpr bool should_use_transaction(const ContainerType& container) {
            return container.size() > 1;
        }

        // Unified statement execution logic for cached/non-cached connections
        template <typename ConnType, typename PrepareFunc, typename BindExecuteFunc>
        [[nodiscard]] static auto execute_statement(
                ConnType& conn, const std::string& sql, PrepareFunc&& prepare_func, BindExecuteFunc&& bind_execute_func
        ) noexcept -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {
            // Use cached prepared statement if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql).and_then(
                        [bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)](
                                auto* stmt
                        ) -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {
                            return bind_execute_func(*stmt);
                        }
                );
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql).and_then(
                        [bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)](
                                typename ConnType::Statement stmt
                        ) -> decltype(bind_execute_func(std::move(stmt))) { return bind_execute_func(std::move(stmt)); }
                );
            }
        }

        // Unified transaction wrapper for batch operations
        template <typename ConnType, typename Operation>
        [[nodiscard]] static auto
        execute_with_transaction(ConnType& conn, bool use_transaction, Operation&& op) noexcept -> decltype(op()) {
            if (!use_transaction) {
                return op();
            }

            // Begin transaction with monadic composition
            return begin_transaction(conn).and_then([&op, &conn]() -> decltype(op()) {
                auto op_result = op();
                if (!op_result) {
                    rollback_transaction(conn);
                    return op_result;
                }

                // Commit transaction
                if (auto commit_result = commit_transaction(conn); !commit_result) {
                    rollback_transaction(conn);
                    return std::unexpected(commit_result.error());
                }

                return op_result;
            });
        }

        // Generic helper for executing with cached or non-cached statements
        template <typename ConnType, typename ExecuteFunc>
        [[nodiscard]] static auto
        execute_with_statement(ConnType& conn, const std::string& sql, ExecuteFunc&& execute_func) noexcept
                -> decltype(execute_func(std::declval<typename ConnType::Statement&>())) {
            // Try cached statement first if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql).and_then([&execute_func](auto* stmt) { return execute_func(*stmt); });
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql).and_then([&execute_func](typename ConnType::Statement stmt) mutable {
                    return execute_func(stmt);
                });
            }
        }

        // Monadic helper for bind and execute operations
        template <typename BindResult, typename Statement>
        [[nodiscard]] static auto bind_and_execute(BindResult bind_result, Statement& stmt) noexcept -> BindResult {
            return bind_result.and_then([&stmt]() { return stmt.execute(); });
        }

        // Monadic helper for reset, bind, and execute sequence
        template <typename Statement, typename BindFunc>
        [[nodiscard]] static auto reset_bind_and_execute(Statement& stmt, BindFunc&& bind_func) noexcept
                -> decltype(bind_func(stmt)) {
            stmt.reset();
            return bind_func(stmt).and_then([&stmt]() { return stmt.execute(); });
        }

        // Generic batch execution template for statement classes
        template <typename StatementType, typename Objects>
        [[nodiscard]] static auto execute_standard_batch(
                StatementType& statement_instance, Objects&& objects, size_t variables_per_object
        ) noexcept -> decltype(statement_instance.execute_individual_batch(objects)) {
            using ConnType = typename StatementType::Connection;
            return execute_batch_optimized<ConnType>(
                    statement_instance.conn_,
                    objects,
                    variables_per_object,
                    [&statement_instance, &objects]() { return statement_instance.execute_bulk(objects); },
                    [&statement_instance, &objects]() { return statement_instance.execute_individual_batch(objects); }
            );
        }

        // Execute batch operations with optimal strategy selection
        template <typename ConnType, typename ContainerType, typename BulkExecutor, typename IndividualExecutor>
        [[nodiscard]] static auto execute_batch_optimized(
                ConnType&            conn,
                const ContainerType& items,
                size_t               items_per_variable, // How many variables each item uses
                BulkExecutor&&       bulk_executor,
                IndividualExecutor&& individual_executor
        ) noexcept -> decltype(individual_executor()) {
            if (items.empty()) {
                return {};
            }

            // Single item - use individual executor without transaction
            if (items.size() == 1) {
                return individual_executor();
            }

            // Calculate max bulk size based on SQLite variable limit
            const size_t max_bulk_size       = MAX_SQLITE_VARIABLES / items_per_variable;
            const size_t effective_threshold = std::min(BATCH_THRESHOLD, max_bulk_size);

            // Small batch - use bulk executor
            if (items.size() <= effective_threshold) {
                return bulk_executor();
            }

            // Large batch - use individual statements with transaction
            return execute_with_transaction(conn, true, individual_executor);
        }
    };

} // namespace storm::orm::statements