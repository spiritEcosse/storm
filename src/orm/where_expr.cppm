module;

#include <meta>

export module storm_orm_where_expr;

import <string>;
import <vector>;
import <variant>;
import <memory>;
import <concepts>;
import <sstream>;
import <tuple>;

export namespace storm::orm::where {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Type-erased parameter value for WHERE clause binding
    using ParamValue = std::variant<
        int, int64_t, long, long long,
        uint64_t, unsigned long, unsigned long long,
        short, unsigned short, unsigned int,
        double, float, bool,
        std::string, std::string_view,
        std::nullptr_t
    >;

    // Forward declarations
    class Expression;
    template<typename T, typename FieldType> class Field;
    class Expr;

    // Base expression interface
    class Expression {
    public:
        virtual ~Expression() = default;
        virtual std::string to_sql() const = 0;
        virtual void collect_params(std::vector<ParamValue>& params) const = 0;
    };

    // Comparison operators
    enum class CompOp {
        Equal,
        NotEqual,
        Greater,
        GreaterEqual,
        Less,
        LessEqual
    };

    inline std::string comp_op_to_sql(CompOp op) {
        switch (op) {
            case CompOp::Equal: return " = ";
            case CompOp::NotEqual: return " != ";
            case CompOp::Greater: return " > ";
            case CompOp::GreaterEqual: return " >= ";
            case CompOp::Less: return " < ";
            case CompOp::LessEqual: return " <= ";
        }
        return " = ";
    }

    // Logical operators
    enum class LogicalOp {
        And,
        Or
    };

    inline std::string logical_op_to_sql(LogicalOp op) {
        switch (op) {
            case LogicalOp::And: return " AND ";
            case LogicalOp::Or: return " OR ";
        }
        return " AND ";
    }

    // Comparison expression: field > value
    template<typename FieldType, typename ValueType>
    class ComparisonExpr : public Expression {
    public:
        ComparisonExpr(std::string field_name, CompOp op, ValueType value)
            : field_name_(std::move(field_name)), op_(op), value_(std::move(value)) {}

        std::string to_sql() const override {
            return field_name_ + comp_op_to_sql(op_) + "?";
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            // Convert value to ParamValue
            if constexpr (std::is_convertible_v<ValueType, ParamValue>) {
                params.emplace_back(value_);
            } else if constexpr (std::is_same_v<ValueType, const char*> ||
                                std::is_array_v<std::remove_reference_t<ValueType>>) {
                params.emplace_back(std::string_view{value_});
            } else {
                params.emplace_back(value_);
            }
        }

    private:
        std::string field_name_;
        CompOp op_;
        ValueType value_;
    };

    // LIKE expression: field.like("pattern%")
    class LikeExpr : public Expression {
    public:
        LikeExpr(std::string field_name, std::string pattern)
            : field_name_(std::move(field_name)), pattern_(std::move(pattern)) {}

        std::string to_sql() const override {
            return field_name_ + " LIKE ?";
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            params.emplace_back(pattern_);
        }

    private:
        std::string field_name_;
        std::string pattern_;
    };

    // BETWEEN expression: field.between(a, b)
    template<typename ValueType>
    class BetweenExpr : public Expression {
    public:
        BetweenExpr(std::string field_name, ValueType min_val, ValueType max_val)
            : field_name_(std::move(field_name)), min_val_(std::move(min_val)), max_val_(std::move(max_val)) {}

        std::string to_sql() const override {
            return field_name_ + " BETWEEN ? AND ?";
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            params.emplace_back(min_val_);
            params.emplace_back(max_val_);
        }

    private:
        std::string field_name_;
        ValueType min_val_;
        ValueType max_val_;
    };

    // IN expression: field.in(1, 2, 3)
    template<typename... ValueTypes>
    class InExpr : public Expression {
    public:
        InExpr(std::string field_name, ValueTypes... values)
            : field_name_(std::move(field_name)), values_(std::make_tuple(std::move(values)...)) {}

        std::string to_sql() const override {
            std::string sql = field_name_ + " IN (";
            for (size_t i = 0; i < sizeof...(ValueTypes); ++i) {
                if (i > 0) sql += ", ";
                sql += "?";
            }
            sql += ")";
            return sql;
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            std::apply([&params](auto&&... args) {
                (params.emplace_back(std::forward<decltype(args)>(args)), ...);
            }, values_);
        }

    private:
        std::string field_name_;
        std::tuple<ValueTypes...> values_;
    };

    // Logical expression: expr1 AND expr2 / expr1 OR expr2
    class LogicalExpr : public Expression {
    public:
        LogicalExpr(std::shared_ptr<Expression> left, LogicalOp op, std::shared_ptr<Expression> right)
            : left_(std::move(left)), op_(op), right_(std::move(right)) {}

        std::string to_sql() const override {
            return "(" + left_->to_sql() + logical_op_to_sql(op_) + right_->to_sql() + ")";
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            left_->collect_params(params);
            right_->collect_params(params);
        }

    private:
        std::shared_ptr<Expression> left_;
        LogicalOp op_;
        std::shared_ptr<Expression> right_;
    };

    // Expression wrapper to enable natural && and || operators without ambiguity
    // Solves the problem of shared_ptr's implicit bool conversion conflicting with built-in operators
    class Expr {
    public:
        // Constructor from shared_ptr<Expression>
        Expr(std::shared_ptr<Expression> expr) : expr_(std::move(expr)) {}

        // Implicit conversion to shared_ptr<Expression> for where() calls
        operator std::shared_ptr<Expression>() const { return expr_; }

        // Logical AND operator (also accessible via 'and' keyword)
        Expr operator&&(const Expr& other) const {
            return Expr(std::make_shared<LogicalExpr>(expr_, LogicalOp::And, other.expr_));
        }

        // Logical OR operator (also accessible via 'or' keyword)
        Expr operator||(const Expr& other) const {
            return Expr(std::make_shared<LogicalExpr>(expr_, LogicalOp::Or, other.expr_));
        }

        // Access the underlying expression
        const std::shared_ptr<Expression>& get() const { return expr_; }

    private:
        std::shared_ptr<Expression> expr_;
    };

    // Field proxy - represents a database field with type safety
    template<typename T, typename FieldType>
    class Field {
    public:
        using ModelType = T;
        using ValueType = FieldType;

        explicit Field(std::string field_name) : field_name_(std::move(field_name)) {}

        // Comparison operators - return Expr for natural && and || usage
        template<typename V>
        Expr operator==(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::Equal, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator!=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::NotEqual, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator>(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::Greater, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator>=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::GreaterEqual, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator<(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::Less, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator<=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<FieldType, std::decay_t<V>>>(
                field_name_, CompOp::LessEqual, std::forward<V>(value)
            ));
        }

        // Special methods - return Expr for consistency
        Expr like(std::string_view pattern) const {
            return Expr(std::make_shared<LikeExpr>(field_name_, std::string(pattern)));
        }

        template<typename V>
        Expr between(V&& min_val, V&& max_val) const {
            return Expr(std::make_shared<BetweenExpr<std::decay_t<V>>>(
                field_name_, std::forward<V>(min_val), std::forward<V>(max_val)
            ));
        }

        template<typename... Values>
        Expr in(Values&&... values) const {
            return Expr(std::make_shared<InExpr<std::decay_t<Values>...>>(
                field_name_, std::forward<Values>(values)...
            ));
        }

        const std::string& get_field_name() const { return field_name_; }

    private:
        std::string field_name_;
    };

    // Helper functions for composing expressions
    // NOTE: With Expr wrapper class, you can now use natural && and || (or 'and' and 'or' keywords)
    // These functions remain for backward compatibility and explicit composition

    // Expr overloads (efficient - no conversion needed)
    inline Expr and_(const Expr& left, const Expr& right) {
        return left && right;
    }

    inline Expr or_(const Expr& left, const Expr& right) {
        return left || right;
    }

    // shared_ptr overloads for backward compatibility
    inline auto and_(const std::shared_ptr<Expression>& left, const std::shared_ptr<Expression>& right) {
        return std::make_shared<LogicalExpr>(left, LogicalOp::And, right);
    }

    inline auto or_(const std::shared_ptr<Expression>& left, const std::shared_ptr<Expression>& right) {
        return std::make_shared<LogicalExpr>(left, LogicalOp::Or, right);
    }

    // Pure C++26 Reflection-Based Field Helper (No Macro Needed!)
    // Usage: field<^^Person::age>() instead of field(Person, age)
    //
    // This function uses compile-time reflection to extract:
    // - Field name via std::meta::identifier_of()
    // - Field type via std::meta::type_of()
    // - Parent class via std::meta::parent_of()
    //
    // Benefits over macro approach:
    // ✅ Module-friendly (no header file needed)
    // ✅ Compile-time validated (^^Person::age fails if member doesn't exist)
    // ✅ Refactoring-safe (IDEs understand ^^Person::age)
    // ✅ Type-safe with full reflection information
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    constexpr auto field() {
        // Extract compile-time information from reflection
        constexpr auto field_name_view = std::meta::identifier_of(MemberInfo);
        constexpr auto parent_type_info = std::meta::parent_of(MemberInfo);
        constexpr auto field_type_info = std::meta::type_of(MemberInfo);

        // Convert to C++ types using splice syntax
        using T = typename [:parent_type_info:];
        using FieldType = typename [:field_type_info:];

        // Create Field with compile-time extracted name
        return Field<T, FieldType>(std::string(field_name_view));
    }

} // namespace storm::orm::where
