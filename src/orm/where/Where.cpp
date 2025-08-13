module;

// Module global fragment
// (no legacy standard includes)

// Module implementation unit
module storm.where;

// Import required modules for implementation
import storm.logical_expression;
import storm.parameter_binder;
import storm.core_types; // For SqlValue
import <string>;
import <memory>;
import <variant>; // For std::variant used in SqlValue

namespace storm {

// Copy constructor implementation
Where::Where(const Where& other) : root_(other.root_ ? other.root_->clone() : nullptr) {}

// Assignment operator implementation
Where& Where::operator=(const Where& other) {
    if (this != &other) {
        root_ = other.root_ ? other.root_->clone() : nullptr;
    }
    return *this;
}

// Logical operators implementation
Where operator and(const Where& lhs, const Where& rhs) { // NOSONAR
    auto and_expr = LogicalExpression::make_and();
    if (lhs.root_) and_expr->add(lhs.root_->clone());
    if (rhs.root_) and_expr->add(rhs.root_->clone());
    return Where(std::move(and_expr));
}

Where operator or(const Where& lhs, const Where& rhs) { // NOSONAR
    auto or_expr = LogicalExpression::make_or();
    if (lhs.root_) or_expr->add(lhs.root_->clone());
    if (rhs.root_) or_expr->add(rhs.root_->clone());
    return Where(std::move(or_expr));
}

// to_query implementation
QueryResult Where::to_query() const {
    auto binder = std::make_shared<ParameterBinder>();
    std::string sql = root_ ? root_->to_sql(*binder) : "";
    return QueryResult(std::move(sql), std::move(binder));
}

} // namespace storm
