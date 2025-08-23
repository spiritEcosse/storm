module;


// Module implementation unit
module storm.logical_expression;

import <vector>;
import <memory>;
import <string>;

namespace storm {

std::string LogicalExpression::to_sql(ParameterBinder& binder) const {
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

std::unique_ptr<Expression> LogicalExpression::clone() const {
    auto result = std::make_unique<LogicalExpression>(logic_op_);
    for (const auto& expr : expressions_) {
        result->add(expr->clone());
    }
    return result;
}

std::unique_ptr<LogicalExpression> LogicalExpression::make_and() {
    return std::make_unique<LogicalExpression>(LogicOp::AND);
}

std::unique_ptr<LogicalExpression> LogicalExpression::make_or() {
    return std::make_unique<LogicalExpression>(LogicOp::OR);
}

} // namespace storm
