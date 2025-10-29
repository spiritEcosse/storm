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
import <expected>;
import storm_db_sqlite;  // For Statement type
import storm_orm_utilities;  // For ConstexprString

export namespace storm::orm::where {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Type-erased parameter value for WHERE clause binding (kept for backward compatibility)
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
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    class Field;
    class Expr;

    // Base expression interface
    class Expression {
    public:
        virtual ~Expression() = default;
        virtual std::string to_sql() const = 0;

        // OLD API: Type-erased parameter collection (kept for backward compatibility)
        virtual void collect_params(std::vector<ParamValue>& params) const = 0;

        // NEW API: Direct parameter binding (eliminates std::variant overhead)
        // stmt_ptr must point to a Statement-like object with bind_int/bind_text/etc methods
        // Returns std::expected<void, storm::db::sqlite::Error>
        virtual auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> = 0;
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

    constexpr std::string_view comp_op_to_sql(CompOp op) {
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

    constexpr std::string_view logical_op_to_sql(LogicalOp op) {
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

        auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            // Cast stmt_ptr to Statement* (type-erased binding)
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return bind_single_value(stmt, param_index++, value_);
        }

        // Helper: Bind a single value directly to SQLite statement
        // Public so other expression types (BetweenExpr, InExpression) can use it
        template<typename T>
        static auto bind_single_value(storm::db::sqlite::Statement* stmt, int idx, const T& value) -> std::expected<void, storm::db::sqlite::Error> {
            using V = std::decay_t<T>;

            if constexpr (std::is_same_v<V, int>) {
                return stmt->bind_int(idx, value);
            } else if constexpr (std::is_same_v<V, int64_t> || std::is_same_v<V, long> || std::is_same_v<V, long long>) {
                return stmt->bind_int64(idx, static_cast<int64_t>(value));
            } else if constexpr (std::is_same_v<V, uint64_t> || std::is_same_v<V, unsigned long> || std::is_same_v<V, unsigned long long>) {
                return stmt->bind_int64(idx, static_cast<int64_t>(value));
            } else if constexpr (std::is_same_v<V, short> || std::is_same_v<V, unsigned short> || std::is_same_v<V, unsigned int>) {
                return stmt->bind_int(idx, static_cast<int>(value));
            } else if constexpr (std::is_same_v<V, double>) {
                return stmt->bind_double(idx, value);
            } else if constexpr (std::is_same_v<V, float>) {
                return stmt->bind_double(idx, static_cast<double>(value));
            } else if constexpr (std::is_same_v<V, bool>) {
                return stmt->bind_int(idx, value ? 1 : 0);
            } else if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
                return stmt->bind_text(idx, std::string_view{value});
            } else if constexpr (std::is_same_v<V, const char*> || std::is_array_v<T>) {
                return stmt->bind_text(idx, std::string_view{value});
            } else {
                // Fallback for unknown types
                return std::unexpected(storm::db::sqlite::Error{-1, "Unknown type for parameter binding"});
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

        auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return stmt->bind_text(param_index++, pattern_);
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

        auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            // Bind min value
            if (auto result = ComparisonExpr<int, ValueType>::bind_single_value(stmt, param_index++, min_val_); !result) {
                return result;
            }
            // Bind max value
            return ComparisonExpr<int, ValueType>::bind_single_value(stmt, param_index++, max_val_);
        }

    private:
        std::string field_name_;
        ValueType min_val_;
        ValueType max_val_;
    };

    // IN expression (runtime): field IN (val1, val2, ..., valN)
    template<typename ValueType>
    class InExpression : public Expression {
    public:
        InExpression(std::string field_name, std::vector<ValueType> values)
            : field_name_(std::move(field_name)), values_(std::move(values)) {}

        std::string to_sql() const override {
            if (values_.empty()) {
                return "1 = 0"; // SQL that always evaluates to false
            }
            std::string sql = field_name_ + " IN (";
            for (size_t i = 0; i < values_.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += "?";
            }
            sql += ")";
            return sql;
        }

        void collect_params(std::vector<ParamValue>& params) const override {
            for (const auto& value : values_) {
                params.emplace_back(value);
            }
        }

        auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            for (const auto& value : values_) {
                if (auto result = ComparisonExpr<int, ValueType>::bind_single_value(stmt, param_index++, value); !result) {
                    return result;
                }
            }
            return {};
        }

    private:
        std::string field_name_;
        std::vector<ValueType> values_;
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

        auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            // Bind left expression parameters, then right expression parameters
            if (auto result = left_->bind_params_direct(stmt_ptr, param_index); !result) {
                return result;
            }
            return right_->bind_params_direct(stmt_ptr, param_index);
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

    // Field proxy - stores reflection info as template parameter
    // Provides compile-time IN expression and runtime comparison/special methods
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    class Field {
    public:
        using ParentType = typename [:std::meta::parent_of(MemberInfo):];
        using FieldType = typename [:std::meta::type_of(MemberInfo):];

        static constexpr auto field_name_sv = std::meta::identifier_of(MemberInfo);

        constexpr Field() : field_name_(field_name_sv) {}

        // IN: Returns runtime Expr for consistency with other methods
        // Usage: field<^^Person::id>().in(100, 200, 300)
        template<typename... Values>
        auto in(Values&&... values) const {
            // Use common_type to find the appropriate type for all values
            using ValueType = std::common_type_t<std::decay_t<Values>...>;
            std::vector<ValueType> vals{static_cast<ValueType>(std::forward<Values>(values))...};
            return Expr(std::make_shared<InExpression<ValueType>>(
                std::string(field_name_), std::move(vals)
            ));
        }

        // Comparison operators - return runtime Expr for flexibility
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

        // Special methods - return runtime Expr
        Expr like(std::string_view pattern) const {
            return Expr(std::make_shared<LikeExpr>(field_name_, std::string(pattern)));
        }

        template<typename V>
        Expr between(V&& min_val, V&& max_val) const {
            return Expr(std::make_shared<BetweenExpr<std::decay_t<V>>>(
                field_name_, std::forward<V>(min_val), std::forward<V>(max_val)
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
    // Usage: field<^^Person::id>().in(100, 200, 300)
    //
    // Returns Field which provides:
    // - in() -> Expr (runtime expression, composable with AND/OR)
    // - Comparison operators (==, >, <, etc.) -> Expr (composable with AND/OR)
    // - Special methods (like, between) -> Expr (composable with AND/OR)
    //
    // All methods return runtime expressions that can be used with QuerySet::where()
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    constexpr auto field() {
        return Field<MemberInfo>();
    }

} // namespace storm::orm::where
