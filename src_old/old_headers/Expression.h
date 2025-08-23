#pragma once

// Legacy header placeholder; use C++23 modules (import storm.expression).
import <string>;
import <memory>;

namespace storm {

// Forward declarations
class ParameterBinder;

// Base expression interface
class Expression {
public:
    virtual ~Expression() = default;
    virtual std::string to_sql(ParameterBinder& binder) const = 0;
    virtual std::unique_ptr<Expression> clone() const = 0;
};

} // namespace storm
