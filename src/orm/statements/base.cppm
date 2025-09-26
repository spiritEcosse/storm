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
    };

} // namespace storm::orm::statements