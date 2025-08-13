module;

// Module global fragment
// (no legacy standard includes)

// Define the module
export module storm.expression;

// Import required modules
import storm.core_types; // For shared types
import storm.parameter_binder; // For ParameterBinder
// Import standard header units
import <string>;
import <memory>;

export namespace storm {

// ParameterBinder is now imported from storm.parameter_binder

// Base expression interface
class Expression {
public:
    virtual ~Expression() = default;
    virtual std::string to_sql(ParameterBinder& binder) const = 0;
    virtual std::unique_ptr<Expression> clone() const = 0;
};

} // namespace storm
