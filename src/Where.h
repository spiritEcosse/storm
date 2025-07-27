#pragma once

#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <memory>
#include <type_traits>
#include <concepts>
#include <unordered_map>
#include <fmt/format.h>
#include "Reflect.h"
#include "StringUtils.h"

namespace storm {

// Core value types that can be used in WHERE clauses
using SqlValue = std::variant<
    std::string,
    int, long, long long,
    float, double,
    bool,
    std::nullopt_t
>;

// Parameter binding system to prevent SQL injection
class ParameterBinder {
private:
    std::unordered_map<std::string, SqlValue> parameters_;
    mutable size_t param_counter_ = 0;
    
public:
    std::string add_parameter(const SqlValue& value) {
        std::string param_name = fmt::format("p{}", param_counter_++);
        parameters_[param_name] = value;
        return param_name;
    }
    
    const std::unordered_map<std::string, SqlValue>& get_parameters() const {
        return parameters_;
    }
    
    void clear() {
        parameters_.clear();
        param_counter_ = 0;
    }
    
    // Get parameter value for prepared statement binding
    SqlValue get_parameter_value(const std::string& param_name) const {
        auto it = parameters_.find(param_name);
        if (it != parameters_.end()) {
            return it->second;
        }
        throw std::runtime_error("Parameter not found: " + param_name);
    }
};

// Query result contains both SQL and parameters
struct QueryResult {
    std::string sql;
    std::shared_ptr<ParameterBinder> binder;
    
    QueryResult(std::string s, std::shared_ptr<ParameterBinder> b) 
        : sql(std::move(s)), binder(std::move(b)) {}
    
    // Helper to get parameters for your database library
    const std::unordered_map<std::string, SqlValue>& parameters() const {
        return binder->get_parameters();
    }
};

// Compile-time operator types for type safety
enum class Op {
    EQ, NE, GT, LT, GE, LE, LIKE, IS, IN, BETWEEN, IS_NOT
};

// Collation types for string comparisons
enum class Collation {
    NONE,    // Default collation
    BINARY,  // Binary comparison (case-sensitive)
    NOCASE,  // Case-insensitive comparison
    RTRIM    // Ignore trailing spaces
};

// Convert collation enum to SQL string
inline std::string_view collation_to_sql(Collation collation) {
    switch (collation) {
        case Collation::BINARY: return "BINARY";
        case Collation::NOCASE: return "NOCASE";
        case Collation::RTRIM: return "RTRIM";
        case Collation::NONE:
        default: return "";
    }
}

constexpr std::string_view op_to_sql(Op op) {
    switch (op) {
        case Op::EQ: return "=";
        case Op::NE: return "!=";
        case Op::GT: return ">";
        case Op::LT: return "<";
        case Op::GE: return ">=";
        case Op::LE: return "<=";
        case Op::LIKE: return "LIKE";
        case Op::IS: return "IS";
        case Op::IS_NOT: return "IS NOT";
        case Op::IN: return "IN";
        case Op::BETWEEN: return "BETWEEN";
    }
    return "";
}

// Forward declarations
class Where;
template<auto MemberPtr> requires std::is_member_object_pointer_v<decltype(MemberPtr)> class Field;

// Base expression interface
class Expression {
public:
    virtual ~Expression() = default;
    virtual std::string to_sql(ParameterBinder& binder) const = 0;
    virtual std::unique_ptr<Expression> clone() const = 0;
};

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
    std::string value_to_sql_safe(const SqlValue& val, ParameterBinder& binder) const {
        return std::visit([&binder](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullopt_t>) {
                return "NULL";  // NULL doesn't need parameterization
            } else {
                // All other values get parameterized
                std::string param_name = binder.add_parameter(SqlValue{v});
                return ":" + param_name;  // Using :param syntax (adjust for your DB)
            }
        }, val);
    }
};

// Logical combination (AND/OR)
class LogicalExpression : public Expression {
private:
    enum class LogicOp { AND, OR };
    LogicOp logic_op_;
    std::vector<std::unique_ptr<Expression>> expressions_;
    
public:
    LogicalExpression(LogicOp op) : logic_op_(op) {}
    
    void add(std::unique_ptr<Expression> expr) {
        expressions_.push_back(std::move(expr));
    }
    
    std::string to_sql(ParameterBinder& binder) const override {
        if (expressions_.empty()) return "";
        if (expressions_.size() == 1) return expressions_[0]->to_sql(binder);
        
        std::string result = "(";
        result += expressions_[0]->to_sql(binder);
        
        const char* op_str = (logic_op_ == LogicOp::AND) ? " AND " : " OR ";
        for (size_t i = 1; i < expressions_.size(); ++i) {
            result += op_str + expressions_[i]->to_sql(binder);
        }
        result += ")";
        return result;
    }
    
    std::unique_ptr<Expression> clone() const override {
        auto result = std::make_unique<LogicalExpression>(logic_op_);
        for (const auto& expr : expressions_) {
            result->add(expr->clone());
        }
        return result;
    }
    
    static std::unique_ptr<LogicalExpression> make_and() {
        return std::make_unique<LogicalExpression>(LogicOp::AND);
    }
    
    static std::unique_ptr<LogicalExpression> make_or() {
        return std::make_unique<LogicalExpression>(LogicOp::OR);
    }
};

// Main WHERE clause builder - NOW INJECTION SAFE
class Where {
private:
    std::unique_ptr<Expression> root_;
    
public:
    Where() = default;
    Where(std::unique_ptr<Expression> expr) : root_(std::move(expr)) {}
    
    // Copy constructor
    Where(const Where& other) : root_(other.root_ ? other.root_->clone() : nullptr) {}
    
    // Move constructor
    Where(Where&&) = default;
    
    // Assignment operators  
    Where& operator=(const Where& other) {
        if (this != &other) {
            root_ = other.root_ ? other.root_->clone() : nullptr;
        }
        return *this;
    }
    Where& operator=(Where&&) = default;
    
    // Logical operators - 'and' and 'or'
    friend Where operator and(const Where& lhs, const Where& rhs) {
        auto and_expr = LogicalExpression::make_and();
        if (lhs.root_) and_expr->add(lhs.root_->clone());
        if (rhs.root_) and_expr->add(rhs.root_->clone());
        return Where(std::move(and_expr));
    }
    
    friend Where operator or(const Where& lhs, const Where& rhs) {
        auto or_expr = LogicalExpression::make_or();
        if (lhs.root_) or_expr->add(lhs.root_->clone());
        if (rhs.root_) or_expr->add(rhs.root_->clone());
        return Where(std::move(or_expr));
    }
    
    // SAFE SQL generation with parameter binding
    QueryResult to_query() const {
        auto binder = std::make_shared<ParameterBinder>();
        std::string sql = root_ ? root_->to_sql(*binder) : "";
        return QueryResult(std::move(sql), std::move(binder));
    }
    
    operator bool() const { return root_ != nullptr; }
};

// Field proxy for fluent syntax
template<auto MemberPtr>
requires std::is_member_object_pointer_v<decltype(MemberPtr)>
class Field {
private:
    using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
    using FieldType = typename member_pointer_type<decltype(MemberPtr)>::type;
    
    std::string field_name_;
    Collation collation_ = Collation::NONE;
    
    std::string get_field_name() const {
        if (!field_name_.empty()) return field_name_;
        // Use the reflection system to get the proper field name
        std::string tableName = Reflect<ClassType>::get_struct_name();
        std::string fieldName = getFieldNameFromMemberPtr<MemberPtr>();
        return utils::formatFieldName(tableName, fieldName);
    }
    
public:
    explicit Field(Collation collation = Collation::NONE) 
        : collation_(collation) {}
    Field(std::string name, Collation collation = Collation::NONE) 
        : field_name_(std::move(name)), collation_(collation) {}
    
    // Comparison operators
    template<typename T>
    Where operator==(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::EQ, std::forward<T>(value), collation_));
    }
    
    template<typename T>
    Where operator!=(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::NE, std::forward<T>(value), collation_));
    }
    
    template<typename T>
    Where operator>(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::GT, std::forward<T>(value), collation_));
    }
    
    template<typename T>
    Where operator<(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::LT, std::forward<T>(value), collation_));
    }
    
    template<typename T>
    Where operator>=(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::GE, std::forward<T>(value), collation_));
    }
    
    template<typename T>
    Where operator<=(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::LE, std::forward<T>(value), collation_));
    }
    
    // Special operators
    template<typename T>
    Where like(T&& pattern) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::LIKE, std::forward<T>(pattern), collation_));
    }

    Where is_null() const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::IS, std::nullopt, collation_));
    }

    Where is_not_null() const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::IS_NOT, std::nullopt, collation_));
    }

    template<typename T>
    Where is(T&& value) const {
        return Where(std::make_unique<Condition>(get_field_name(), Op::IS, std::forward<T>(value), collation_));
    }
    
    template<typename T1, typename T2>
    Where between(T1&& value1, T2&& value2) const {
        return Where(std::make_unique<Condition>(get_field_name(), std::forward<T1>(value1), std::forward<T2>(value2), collation_));
    }
    
    // IN operator for multiple values
    template<typename Container>
    Where in(const Container& values) const {
        // For simplicity, we'll convert the IN operation to multiple OR conditions
        // This ensures compatibility with the existing parameter binding system
        if (values.empty()) {
            // Empty IN clause should match nothing - use impossible condition
            return Where(std::make_unique<Condition>(get_field_name(), Op::EQ, "__IMPOSSIBLE_VALUE__", collation_));
        }
        
        auto it = values.begin();
        Where result = Field<MemberPtr>(field_name_, collation_) == *it;
        ++it;
        
        for (; it != values.end(); ++it) {
            Where next_condition = Field<MemberPtr>(field_name_, collation_) == *it;
            result = result or next_condition;
        }
        
        return result;
    }
    
    // IN operator for initializer lists (to handle {"alice", "bob", "charlie"})
    template<typename T>
    Where in(std::initializer_list<T> values) const {
        return in(std::vector<T>(values));
    }
    
    // String pattern matching helpers
    template<typename T>
    Where startswith(T&& prefix) const {
        std::string pattern = std::string(prefix) + "%";
        return Where(std::make_unique<Condition>(get_field_name(), Op::LIKE, std::move(pattern), collation_));
    }
    
    template<typename T>
    Where endswith(T&& suffix) const {
        std::string pattern = "%" + std::string(suffix);
        return Where(std::make_unique<Condition>(get_field_name(), Op::LIKE, std::move(pattern), collation_));
    }
    
    // Collation methods
    Field<MemberPtr> collate_binary() const {
        return Field<MemberPtr>(field_name_, Collation::BINARY);
    }
    
    Field<MemberPtr> collate_nocase() const {
        return Field<MemberPtr>(field_name_, Collation::NOCASE);
    }
    
    Field<MemberPtr> collate_rtrim() const {
        return Field<MemberPtr>(field_name_, Collation::RTRIM);
    }
};

} // namespace storm
