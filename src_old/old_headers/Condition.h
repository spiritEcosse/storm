#pragma once

// Legacy header placeholder; use C++23 modules (import storm.condition).
import storm.expression;
import <string>;
import <optional>;
import <variant>;
import <iostream>;
import <memory>;
import <utility>;

namespace storm {

// Forward declarations
class ParameterBinder;

// Core value types that can be used in WHERE clauses 
using SqlValue = std::variant<
    std::string,
    int, long, long long,
    float, double,
    bool,
    std::nullopt_t
>;

// Compile-time operator types for type safety
enum class Op {
    EQ, NE, GT, LT, GE, LE, LIKE, IS, IN, BETWEEN, IS_NOT
};

// Collation options for string comparisons
enum class Collation {
    NONE, BINARY, NOCASE, RTRIM
};

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
    
    std::string to_sql(ParameterBinder& binder) const override {
        std::string sql = field_name_;
        
        // Add collation if specified (for string comparisons)
        if (collation_ != Collation::NONE) {
            sql += " COLLATE " + std::string(collation_to_sql(collation_));
        }
        
        sql += " " + std::string(op_to_sql(operator_)) + " ";
        
        if (operator_ == Op::BETWEEN) {
            std::string param1 = value_to_sql_safe(value_, binder);
            std::string param2 = value_to_sql_safe(*value2_, binder);
            sql += param1 + " AND " + param2;
        } else if (operator_ == Op::IS && std::holds_alternative<std::nullopt_t>(value_)) {
            // Special case for IS NULL
            sql += "NULL";
            std::cout << "IS NULL" << std::endl;
        } else {
            sql += value_to_sql_safe(value_, binder);
        }
        
        return sql;
    }
    
    std::unique_ptr<Expression> clone() const override {
        if (value2_) {
            return std::make_unique<Condition>(field_name_, value_, *value2_, collation_);
        }
        return std::make_unique<Condition>(field_name_, operator_, value_, collation_);
    }
    
private:
    // SAFE parameter binding - no direct string interpolation
    std::string value_to_sql_safe(const SqlValue& val, ParameterBinder& binder) const;
};

} // namespace storm
