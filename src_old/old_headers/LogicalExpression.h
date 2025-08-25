#pragma once

// Legacy header placeholder; use C++23 modules (import storm.logical_expression).
import storm.expression;
import <vector>;
import <memory>;

namespace storm {

    // Logical combination (AND/OR)
    class LogicalExpression : public Expression {
      private:
        enum class LogicOp { AND, OR };
        LogicOp                                  logic_op_;
        std::vector<std::unique_ptr<Expression>> expressions_;

      public:
        explicit LogicalExpression(LogicOp op) : logic_op_(op) {}

        void add(std::unique_ptr<Expression> expr) {
            expressions_.push_back(std::move(expr));
        }

        std::string                 to_sql(ParameterBinder& binder) const override;
        std::unique_ptr<Expression> clone() const override;

        static std::unique_ptr<LogicalExpression> make_and();
        static std::unique_ptr<LogicalExpression> make_or();
    };

} // namespace storm
