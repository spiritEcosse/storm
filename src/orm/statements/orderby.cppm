module;

#include <meta>

export module storm_orm_statements_orderby;

import storm_orm_utilities;

import <array>;
import <concepts>;
import <string>;
import <meta>;
import <utility>;

namespace detail {
    // Free consteval helper — avoids generic-lambda-in-consteval compiler bug
    template <size_t N, auto Arg>
    consteval void process_order_by_arg(std::array<std::pair<std::meta::info, bool>, N>& result, size_t& idx) {
        if constexpr (std::same_as<decltype(Arg), bool>) {
            result[idx - 1].second = Arg;
        } else {
            result[idx++] = {Arg, true}; // default ASC
        }
    }
} // namespace detail

export namespace storm::orm::statements {

    namespace buffer_size = storm::orm::utilities::buffer_size;
    using storm::orm::utilities::ConstexprString;

    // ============================================================================
    // ORDER BY Clause - variadic pack processing (no recursive specializations)
    // ============================================================================

    template <auto... Args> struct OrderByClause {
        // Count non-bool args (each is a field; bools are directions)
        static constexpr size_t count = ((std::same_as<decltype(Args), bool> ? 0 : 1) + ... + 0);

        static constexpr auto fields = [] consteval // NOSONAR(cpp:S1659)
                -> std::array<std::pair<std::meta::info, bool>, count> {
            std::array<std::pair<std::meta::info, bool>, count> result{};
            size_t                                              idx = 0;
            (detail::process_order_by_arg<count, Args>(result, idx), ...);
            return result;
        }();

        static constexpr auto empty() -> bool {
            return count == 0;
        }

        // Generate ORDER BY SQL fragment at compile-time
        template <size_t BufferSize = buffer_size::SQL_MEDIUM>
        static consteval auto to_sql() -> ConstexprString<BufferSize> {
            ConstexprString<BufferSize> result;

            if constexpr (count == 0) {
                return result;
            }

            result.append(" ORDER BY ");

            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    result.append(", ");
                }

                constexpr auto field_info = fields[i].first;
                constexpr auto field_name = std::meta::identifier_of(field_info);
                result.append(field_name);

                constexpr bool ascending = fields[i].second;
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

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((append_field<Is>(result, Is > 0)), ...);
            }(std::make_index_sequence<count>{});

            return result;
        }

      private:
        template <size_t I> static auto append_field(std::string& result, bool add_comma) -> void {
            if (add_comma) {
                result += ", ";
            }

            constexpr auto field_info = fields[I].first;
            result += std::string(std::meta::identifier_of(field_info));

            constexpr bool ascending = fields[I].second;
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
