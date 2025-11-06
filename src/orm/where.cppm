module;

#include <meta>

export module storm_orm_where;

import <string>;
import <vector>;
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
        [[nodiscard]] virtual std::string to_sql() const = 0;

        // Direct parameter binding (eliminates std::variant overhead)
        // stmt_ptr must point to a Statement-like object with bind_int/bind_text/etc methods
        // Returns std::expected<void, storm::db::sqlite::Error>
        [[nodiscard]] virtual auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> = 0;
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

    constexpr std::string_view comp_op_to_sql(CompOp op) noexcept {
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

    // CONSTEXPR SQL GENERATION: Compile-time SQL string generation for simple cases
    // This allows the compiler to optimize away SQL string construction in hot paths
    template<std::meta::info MemberInfo, CompOp Op>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    struct ConstexprComparisonSQL {
        static constexpr auto generate() noexcept {
            utilities::ConstexprString<256> result;
            result += std::meta::identifier_of(MemberInfo);
            result += comp_op_to_sql(Op);
            result += "?";
            return result;
        }

        static constexpr auto sql = generate();
        static constexpr std::string_view sql_view() noexcept {
            return std::string_view(sql.data(), sql.size());
        }
    };

    // CONSTEXPR LIKE SQL
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    struct ConstexprLikeSQL {
        static constexpr auto generate() noexcept {
            utilities::ConstexprString<256> result;
            result += std::meta::identifier_of(MemberInfo);
            result += " LIKE ?";
            return result;
        }

        static constexpr auto sql = generate();
        static constexpr std::string_view sql_view() noexcept {
            return std::string_view(sql.data(), sql.size());
        }
    };

    // CONSTEXPR BETWEEN SQL
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    struct ConstexprBetweenSQL {
        static constexpr auto generate() noexcept {
            utilities::ConstexprString<256> result;
            result += std::meta::identifier_of(MemberInfo);
            result += " BETWEEN ? AND ?";
            return result;
        }

        static constexpr auto sql = generate();
        static constexpr std::string_view sql_view() noexcept {
            return std::string_view(sql.data(), sql.size());
        }
    };

    // Logical operators
    enum class LogicalOp {
        And,
        Or
    };

    constexpr std::string_view logical_op_to_sql(LogicalOp op) noexcept {
        switch (op) {
            case LogicalOp::And: return " AND ";
            case LogicalOp::Or: return " OR ";
        }
        return " AND ";
    }

    // Comparison expression: field > value
    template<typename ValueType>
    class ComparisonExpr : public Expression {
    public:
        ComparisonExpr(std::string_view field_name, CompOp op, ValueType value)
            : field_name_(field_name), op_(op), value_(std::move(value)) {
            // Pre-generate SQL in constructor for consistency with InExpression
            // and to benchmark whether simple concatenation benefits from caching
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql_.reserve(field_name_.size() + 4); // field + op (max 4 chars) + "?"
            sql_ = field_name_;
            sql_ += comp_op_to_sql(op_);
            sql_ += "?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const override {
            return sql_;
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            // Cast stmt_ptr to Statement* (type-erased binding)
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return bind_single_value(stmt, param_index++, value_);
        }

        // Helper: Bind a single value directly to SQLite statement
        // Public so other expression types (BetweenExpr, InExpression) can use it
        // Delegates to unified bind_parameter_value utility
        template<typename T>
        [[nodiscard]] __attribute__((always_inline)) static inline auto bind_single_value(storm::db::sqlite::Statement* stmt, int idx, const T& value) -> std::expected<void, storm::db::sqlite::Error> {
            return utilities::bind_parameter_value<storm::db::sqlite::Statement, storm::db::sqlite::Error>(*stmt, idx, value);
        }

    private:
        std::string field_name_;
        CompOp op_;
        ValueType value_;
        std::string sql_;  // Cached SQL string (pre-generated in constructor)
    };

    // LIKE expression: field.like("pattern%")
    class LikeExpr : public Expression {
    public:
        LikeExpr(std::string_view field_name, std::string_view pattern)
            : field_name_(field_name), pattern_(pattern) {
            // Pre-generate SQL in constructor for consistency with ComparisonExpr
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql_.reserve(field_name_.size() + 8); // field + " LIKE ?"
            sql_ = field_name_;
            sql_ += " LIKE ?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const override {
            return sql_;
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return stmt->bind_text(param_index++, pattern_);
        }

    private:
        std::string field_name_;
        std::string pattern_;
        std::string sql_;  // Cached SQL string (pre-generated in constructor)
    };

    // BETWEEN expression: field.between(a, b)
    template<typename ValueType>
    class BetweenExpr : public Expression {
    public:
        BetweenExpr(std::string_view field_name, ValueType min_val, ValueType max_val)
            : field_name_(field_name), min_val_(std::move(min_val)), max_val_(std::move(max_val)) {
            // Pre-generate SQL in constructor for consistency with ComparisonExpr
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql_.reserve(field_name_.size() + 17); // field + " BETWEEN ? AND ?"
            sql_ = field_name_;
            sql_ += " BETWEEN ? AND ?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const override {
            return sql_;
        }

        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            // Bind min value
            if (auto result = ComparisonExpr<ValueType>::bind_single_value(stmt, param_index++, min_val_); !result) {
                return result;
            }
            // Bind max value
            return ComparisonExpr<ValueType>::bind_single_value(stmt, param_index++, max_val_);
        }

    private:
        std::string field_name_;
        ValueType min_val_;
        ValueType max_val_;
        std::string sql_;  // Cached SQL string (pre-generated in constructor)
    };

    // IN expression (runtime): field IN (val1, val2, ..., valN)
    template<typename ValueType>
    class InExpression : public Expression {
    public:
        InExpression(std::string_view field_name, std::vector<ValueType> values)
            : field_name_(field_name), values_(std::move(values)) {
            // OPTIMIZATION: Pre-generate SQL once in constructor to avoid repeated string concatenations
            if (values_.empty()) {
                sql_ = "1 = 0"; // SQL that always evaluates to false
            } else {
                // Reserve capacity to avoid reallocations
                sql_.reserve(field_name_.size() + 10 + values_.size() * 3);
                sql_ = field_name_;
                sql_ += " IN (";
                for (size_t i = 0; i < values_.size(); ++i) {
                    if (i > 0) sql_ += ", ";
                    sql_ += "?";
                }
                sql_ += ")";
            }
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const override {
            return sql_;  // Return pre-generated SQL
        }

        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            for (const auto& value : values_) {
                if (auto result = ComparisonExpr<ValueType>::bind_single_value(stmt, param_index++, value); !result) {
                    return result;
                }
            }
            return {};
        }

    private:
        std::string field_name_;
        std::vector<ValueType> values_;
        std::string sql_;  // Pre-generated SQL string
    };

    // Logical expression: expr1 AND expr2 / expr1 OR expr2
    class LogicalExpr : public Expression {
    public:
        LogicalExpr(std::shared_ptr<Expression> left, LogicalOp op, std::shared_ptr<Expression> right)
            : left_(std::move(left)), op_(op), right_(std::move(right)) {}

        [[nodiscard]] __attribute__((hot)) std::string to_sql() const override {
            // OPTIMIZATION: Pre-allocate string to avoid reallocations
            std::string result;
            auto left_sql = left_->to_sql();
            auto right_sql = right_->to_sql();
            result.reserve(left_sql.size() + right_sql.size() + 8); // 1 + left + op(max 5) + right + 1
            result = "(";
            result += left_sql;
            result += logical_op_to_sql(op_);
            result += right_sql;
            result += ")";
            return result;
        }

        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> override {
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
        Expr(std::shared_ptr<Expression> expr) noexcept : expr_(std::move(expr)) {}

        // Implicit conversion to shared_ptr<Expression> for where() calls
        operator std::shared_ptr<Expression>() const noexcept { return expr_; }

        // Logical AND operator (also accessible via 'and' keyword)
        Expr operator&&(const Expr& other) const {
            return Expr(std::make_shared<LogicalExpr>(expr_, LogicalOp::And, other.expr_));
        }

        // Logical OR operator (also accessible via 'or' keyword)
        Expr operator||(const Expr& other) const {
            return Expr(std::make_shared<LogicalExpr>(expr_, LogicalOp::Or, other.expr_));
        }

        // Access the underlying expression
        [[nodiscard]] const std::shared_ptr<Expression>& get() const noexcept { return expr_; }

    private:
        std::shared_ptr<Expression> expr_;
    };

    // Field proxy - stores reflection info as template parameter
    // Provides compile-time IN expression and runtime comparison/special methods
    //
    // P2996 REFLECTION FEATURES:
    // - MemberInfo contains compile-time reflection metadata
    // - field_name extracted via std::meta::identifier_of (zero runtime cost)
    // - Type information available via std::meta::type_of for automatic type conversion
    // - Compile-time validation ensures MemberInfo is a valid data member
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    class Field {
    public:
        static constexpr auto field_name_sv = std::meta::identifier_of(MemberInfo);

        constexpr Field() noexcept : field_name_(field_name_sv) {}

        // IN: Returns runtime Expr for consistency with other methods
        // Usage: field<^^Person::id>().in(100, 200, 300)
        //
        // REFLECTION-BASED TYPE DEDUCTION: Uses P2996 to get the field's actual type
        // and automatically converts values to match
        template<typename... Values>
        auto in(Values&&... values) const {
            // Use reflection to get the field's actual C++ type for type safety
            using FieldType = typename [:std::meta::type_of(MemberInfo):];

            // Convert all values to the field's type (compile-time type checking!)
            std::vector<FieldType> vals{static_cast<FieldType>(std::forward<Values>(values))...};

            return Expr(std::make_shared<InExpression<FieldType>>(
                std::string(field_name_), std::move(vals)
            ));
        }

        // Comparison operators - return runtime Expr for flexibility
        template<typename V>
        Expr operator==(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::Equal, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator!=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::NotEqual, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator>(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::Greater, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator>=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::GreaterEqual, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator<(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::Less, std::forward<V>(value)
            ));
        }

        template<typename V>
        Expr operator<=(V&& value) const {
            return Expr(std::make_shared<ComparisonExpr<std::decay_t<V>>>(
                field_name_, CompOp::LessEqual, std::forward<V>(value)
            ));
        }

        // Special methods - return runtime Expr
        Expr like(std::string_view pattern) const {
            return Expr(std::make_shared<LikeExpr>(field_name_, pattern));
        }

        template<typename V>
        Expr between(V&& min_val, V&& max_val) const {
            return Expr(std::make_shared<BetweenExpr<std::decay_t<V>>>(
                field_name_, std::forward<V>(min_val), std::forward<V>(max_val)
            ));
        }

        [[nodiscard]] const std::string& get_field_name() const noexcept { return field_name_; }

        // Get field name at compile-time for constexpr contexts
        [[nodiscard]] static constexpr std::string_view get_field_name_constexpr() noexcept {
            return field_name_sv;
        }

        // Get the reflected field's C++ type
        using field_type = typename [:std::meta::type_of(MemberInfo):];

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
    //
    // COMPILE-TIME VALIDATION: Uses P2996 to ensure MemberInfo is a valid field
    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo) &&
                  std::meta::has_identifier(MemberInfo))  // Ensures field has a name
    constexpr auto field() {
        // Additional compile-time validation: field must be accessible
        static_assert(std::meta::is_nonstatic_data_member(MemberInfo),
            "field<> requires a non-static data member reflection (use ^^Type::member syntax)");

        return Field<MemberInfo>();
    }

} // namespace storm::orm::where
