module;

// Define the module
export module storm.condition;

import <vector>;
import <string>;
import <string_view>;
import <iostream>;
import <memory>;
import <utility>;
import <variant>;
import <optional>;

// Import required modules
import storm.core_types; // For SqlValue, Op, Collation
import storm.expression;
import storm.parameter_binder; // For ParameterBinder

export namespace storm {

// ParameterBinder is imported via storm.expression which imports storm.parameter_binder
// SqlValue, Op, and Collation are now imported from storm.core_types

// Convert collation enum to SQL string
std::string_view collation_to_sql(Collation collation);

// Convert operator enum to SQL string
std::string_view op_to_sql(Op op);

// Leaf condition (field op value) - NOW INJECTION SAFE
class Condition : public Expression {
private:
    std::string field_name_;
    Op operator_;
    SqlValue value_;
    std::optional<SqlValue> value2_; // For BETWEEN
    Collation collation_ = Collation::NONE; // Default to no collation
    
public:
    template<typename T>
    Condition(std::string field_name, Op op, T&& value, Collation collation = Collation::NONE) 
        : field_name_(std::move(field_name)), operator_(op), value_(std::forward<T>(value)), collation_(collation) {}
    
    template<typename T1, typename T2>
    Condition(std::string field_name, T1&& value1, T2&& value2, Collation collation = Collation::NONE) // BETWEEN
        : field_name_(std::move(field_name)), operator_(Op::BETWEEN), 
          value_(std::forward<T1>(value1)), value2_(std::forward<T2>(value2)), collation_(collation) {}
    
    std::string to_sql(ParameterBinder& binder) const override;
    std::unique_ptr<Expression> clone() const override;
    
private:
    // SAFE parameter binding - no direct string interpolation
    std::string value_to_sql_safe(const SqlValue& val, ParameterBinder& binder) const;
};

} // namespace storm
