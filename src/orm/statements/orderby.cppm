module;

#include <meta>

export module storm_orm_statements_orderby;

import storm_orm_utilities;

import <string>;
import <meta>;

export namespace storm::orm::statements {

    using storm::orm::utilities::OrderByClause;

    // ============================================================================
    // ORDER BY Wrapper (following JoinStatementWrapper pattern)
    // ============================================================================
    // Lightweight type-erased wrapper for ORDER BY clauses
    // Uses function pointer to static compile-time generated SQL (zero heap allocation)
    struct OrderByWrapper {
        const std::string& (*get_order_by_sql_fn)();

        [[nodiscard]] const std::string& get_order_by_sql() const {
            return get_order_by_sql_fn();
        }

        [[nodiscard]] bool empty() const {
            return get_order_by_sql().empty();
        }
    };

    // Helper to create OrderByWrapper from template args
    // Each unique Args... combination gets its own static SQL string
    template <auto... Args> inline OrderByWrapper make_order_by_wrapper() {
        // Static compile-time generated SQL (one instance per unique Args... combination)
        // Stored in .rodata section (no heap allocation)
        static const std::string order_by_sql = OrderByClause<Args...>::to_sql_runtime();

        return OrderByWrapper{.get_order_by_sql_fn = []() -> const std::string& { return order_by_sql; }};
    }

} // namespace storm::orm::statements
