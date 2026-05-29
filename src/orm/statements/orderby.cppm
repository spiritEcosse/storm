module;

#include <meta>

export module storm_orm_statements_orderby;

import std;

import storm_orm_utilities;

namespace detail {

    using storm::orm::utilities::Collate;

    struct OrderByField {
        std::meta::info field = std::meta::info{};
        bool            asc   = true;
        Collate         col   = Collate::None;
    };

    // Free consteval helper — avoids generic-lambda-in-consteval compiler bug
    template <std::size_t N, auto Arg>
    consteval void process_order_by_arg(std::array<OrderByField, N>& result, std::size_t& idx) {
        if constexpr (std::same_as<decltype(Arg), bool>) {
            result[idx - 1].asc = Arg;
        } else if constexpr (std::same_as<decltype(Arg), Collate>) {
            result[idx - 1].col = Arg;
        } else {
            result[idx] = {.field = Arg, .asc = true, .col = Collate::None};
            ++idx;
        }
    }

    // Count field args (skip bool and Collate modifiers)
    template <auto Arg> consteval auto is_field_arg() -> bool {
        return !std::same_as<decltype(Arg), bool> && !std::same_as<decltype(Arg), Collate>;
    }

} // namespace detail

export namespace storm::orm::statements {

    namespace buffer_size = storm::orm::utilities::buffer_size;
    using storm::orm::utilities::collate_to_sql;
    using storm::orm::utilities::ConstexprString;

    // ============================================================================
    // ORDER BY Clause - variadic pack processing (no recursive specializations)
    // ============================================================================

    template <auto... Args> struct OrderByClause {
        // Count field args (skip bool and Collate modifiers)
        static constexpr std::size_t count = ((detail::is_field_arg<Args>() ? 1 : 0) + ... + 0);

        static constexpr auto fields = [] consteval // NOSONAR(cpp:S1659)
                -> std::array<detail::OrderByField, count> {
            std::array<detail::OrderByField, count> result{};
            std::size_t                             idx = 0;
            (detail::process_order_by_arg<count, Args>(result, idx), ...);
            return result;
        }();

        static constexpr auto empty() -> bool {
            return count == 0;
        }

        // Generate ORDER BY SQL fragment at compile-time
        template <std::size_t BufferSize = buffer_size::SQL_MEDIUM>
        static consteval auto to_sql() -> ConstexprString<BufferSize> {
            ConstexprString<BufferSize> result;

            if constexpr (count == 0) {
                return result;
            }

            result.append(" ORDER BY ");

            for (std::size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    result.append(", ");
                }

                constexpr auto field_info = fields[i].field;
                constexpr auto field_name = std::meta::identifier_of(field_info);
                result.append(field_name);

                constexpr auto collation = fields[i].col;
                result.append(collate_to_sql(collation));

                constexpr bool ascending = fields[i].asc;
                if (ascending) {
                    result.append(" ASC");
                } else {
                    result.append(" DESC");
                }
            }

            return result;
        }

        // Generate ORDER BY SQL fragment at runtime
        static auto to_sql_runtime() -> std::string {
            if constexpr (count == 0) {
                return "";
            }

            std::string result = " ORDER BY ";

            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((append_field<Is>(result, Is > 0)), ...);
            }(std::make_index_sequence<count>{});

            return result;
        }

      private:
        template <std::size_t I> static auto append_field(std::string& result, bool add_comma) -> void {
            if (add_comma) {
                result += ", ";
            }

            constexpr auto field_info = fields[I].field;
            result += std::string(std::meta::identifier_of(field_info));

            constexpr auto collation = fields[I].col;
            result += collate_to_sql(collation);

            constexpr bool ascending = fields[I].asc;
            result += ascending ? " ASC" : " DESC";
        }
    };

    // ============================================================================
    // ORDER BY Wrapper (following JoinStatementWrapper pattern)
    // ============================================================================
    // Lightweight type-erased wrapper for ORDER BY clauses
    // Uses function pointer to static compile-time generated SQL (zero heap allocation)
    struct OrderByWrapper {
        auto (*get_order_by_sql_fn)() -> const std::string&;

        [[nodiscard]] auto get_order_by_sql() const -> const std::string& {
            return get_order_by_sql_fn();
        }

        [[nodiscard]] auto empty() const -> bool {
            return get_order_by_sql().empty();
        }
    };

    // Helper to create OrderByWrapper from template args
    // Each unique Args... combination gets its own static SQL string
    template <auto... Args> inline auto make_order_by_wrapper() -> OrderByWrapper {
        // Static compile-time generated SQL (one instance per unique Args... combination)
        // Stored in .rodata section (no heap allocation)
        static const std::string order_by_sql = OrderByClause<Args...>::to_sql_runtime();

        return OrderByWrapper{.get_order_by_sql_fn = []() -> const std::string& { return order_by_sql; }};
    }

} // namespace storm::orm::statements
