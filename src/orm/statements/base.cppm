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

export namespace storm::orm::statements {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique };
    }

    // Shared reflection utilities for all statement types
    template <typename T>
    class BaseStatement {
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
        template<size_t N>
        static consteval std::array<std::meta::info, N> get_all_field_members() {
            std::array<std::meta::info, N> result{};
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

            for (size_t i = 0; i < N && i < members.size(); ++i) {
                result[i] = members[i];
            }
            return result;
        }

        // Pre-computed field information
        static constexpr auto field_count_ = get_field_count();
        static constexpr auto all_members_ = get_all_field_members<field_count_>();

        // Common binding utilities for different types
        template <typename ConnType>
        [[nodiscard]] static auto bind_value_by_type(
                typename ConnType::Statement& stmt,
                int param_index,
                const auto& value) noexcept -> std::expected<void, typename ConnType::Error> {
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
        [[nodiscard]] static auto begin_transaction(ConnType& conn) noexcept -> std::expected<void, typename ConnType::Error> {
            return conn.execute("BEGIN TRANSACTION");
        }

        template <typename ConnType>
        [[nodiscard]] static auto commit_transaction(ConnType& conn) noexcept -> std::expected<void, typename ConnType::Error> {
            return conn.execute("COMMIT");
        }

        template <typename ConnType>
        static void rollback_transaction(ConnType& conn) noexcept {
            (void)conn.execute("ROLLBACK");
        }

        // Utility to determine if transaction should be used
        template <typename ContainerType>
        static constexpr bool should_use_transaction(const ContainerType& container) {
            return container.size() > 1;
        }

        // Unified statement execution logic for cached/non-cached connections
        template <typename ConnType, typename PrepareFunc, typename BindExecuteFunc>
        [[nodiscard]] static auto execute_statement(
                ConnType& conn,
                const std::string& sql,
                PrepareFunc&& prepare_func,
                BindExecuteFunc&& bind_execute_func) noexcept
                -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {

            // Use cached prepared statement if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql)
                        .and_then([bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)]
                                  (auto* stmt) -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {
                            return bind_execute_func(*stmt);
                        });
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql)
                        .and_then([bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)]
                                  (typename ConnType::Statement stmt) -> decltype(bind_execute_func(std::move(stmt))) {
                            return bind_execute_func(std::move(stmt));
                        });
            }
        }

        // Unified transaction wrapper for batch operations
        template <typename ConnType, typename Operation, typename Result = std::expected<void, typename ConnType::Error>>
        [[nodiscard]] static auto execute_with_transaction(
                ConnType& conn,
                bool use_transaction,
                Operation&& op) noexcept -> Result {

            if (!use_transaction) {
                return op();
            }

            // Begin transaction
            if (auto result = begin_transaction(conn); !result) {
                return std::unexpected(result.error());
            }

            // Execute operation
            auto op_result = op();

            if (!op_result) {
                // Rollback on failure
                rollback_transaction(conn);
                return op_result;
            }

            // Commit transaction
            if (auto result = commit_transaction(conn); !result) {
                rollback_transaction(conn);
                return std::unexpected(result.error());
            }

            return op_result;
        }
    };

} // namespace storm::orm::statements