// Legacy header placeholder; use C++23 modules (import storm.where).
#pragma once

import <string>;
import <memory>;
import <type_traits>;
import <concepts>;
import <span>;
import <unordered_map>;
import storm.reflect;
import storm.utils;
import storm.expression;
import storm.condition;
import storm.logical_expression;
import storm.parameter_binder;
import storm.field;

namespace storm {

    // Query result contains both SQL and parameters
    struct QueryResult {
        std::string                      sql;
        std::shared_ptr<ParameterBinder> binder;

        explicit QueryResult(std::string s, std::shared_ptr<ParameterBinder> b)
            : sql(std::move(s)), binder(std::move(b)) {}

        // Helper to get parameters for your database library
        const std::unordered_map<std::string, SqlValue, TransparentStringHash, std::equal_to<>>& parameters() const {
            return binder->get_parameters();
        }
    };

    // Main WHERE clause builder - NOW INJECTION SAFE
    class Where {
      private:
        std::unique_ptr<Expression> root_;

      public:
        Where() = default;
        explicit Where(std::unique_ptr<Expression> expr) : root_(std::move(expr)) {}

        // Copy constructor
        Where(const Where& other);

        // Move constructor
        Where(Where&&) = default;

        // Destructor
        ~Where() = default;

        // Assignment operators
        Where& operator=(const Where& other);
        Where& operator=(Where&&) = default;

        // Logical operators - 'and' and 'or'
        friend Where operator and(const Where& lhs, const Where& rhs); // NOSONAR
        friend Where operator or(const Where& lhs, const Where& rhs);  // NOSONAR

        // SAFE SQL generation with parameter binding
        QueryResult to_query() const;

        explicit operator bool() const {
            return root_ != nullptr;
        }
    };
} // namespace storm
