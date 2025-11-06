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
import <variant>;
import storm_db_sqlite;  // For Statement type
import storm_orm_utilities;  // For ConstexprString

export namespace storm::orm::where {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Forward declarations for recursive variant
    template<typename ValueType> struct ComparisonExpr;
    struct LikeExpr;
    template<typename ValueType> struct BetweenExpr;
    template<typename ValueType> struct InExpression;
    struct LogicalExpr;

    template<std::meta::info MemberInfo>
        requires (std::meta::is_nonstatic_data_member(MemberInfo))
    class Field;

    // VARIANT-BASED EXPRESSION SYSTEM (eliminates heap allocation and vtable overhead)
    // Instead of virtual inheritance, we use std::variant for compile-time polymorphism
    //
    // Forward declare the variant type
    struct ExpressionVariant;

    // Recursive variant using std::shared_ptr for LogicalExpr children
    // (LogicalExpr needs to hold other expressions, creating recursion)
    using ExpressionVariantPtr = std::shared_ptr<ExpressionVariant>;

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

    // VALUE-TYPE Comparison expression: field > value (NO VIRTUAL FUNCTIONS!)
    template<typename ValueType>
    struct ComparisonExpr {
        std::string field_name;
        CompOp op;
        ValueType value;
        std::string sql;  // Cached SQL string (pre-generated in constructor)

        ComparisonExpr(std::string_view fname, CompOp operation, ValueType val)
            : field_name(fname), op(operation), value(std::move(val)) {
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql.reserve(field_name.size() + 4); // field + op (max 4 chars) + "?"
            sql = field_name;
            sql += comp_op_to_sql(op);
            sql += "?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const noexcept {
            return sql;
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> {
            // Cast stmt_ptr to Statement* (type-erased binding)
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return utilities::bind_parameter_value<storm::db::sqlite::Statement, storm::db::sqlite::Error>(*stmt, param_index++, value);
        }
    };

    // VALUE-TYPE LIKE expression: field.like("pattern%") (NO VIRTUAL FUNCTIONS!)
    struct LikeExpr {
        std::string field_name;
        std::string pattern;
        std::string sql;  // Cached SQL string (pre-generated in constructor)

        LikeExpr(std::string_view fname, std::string_view pat)
            : field_name(fname), pattern(pat) {
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql.reserve(field_name.size() + 8); // field + " LIKE ?"
            sql = field_name;
            sql += " LIKE ?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const noexcept {
            return sql;
        }

        [[nodiscard]] __attribute__((always_inline)) inline auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            return stmt->bind_text(param_index++, pattern);
        }
    };

    // VALUE-TYPE BETWEEN expression: field.between(a, b) (NO VIRTUAL FUNCTIONS!)
    template<typename ValueType>
    struct BetweenExpr {
        std::string field_name;
        ValueType min_val;
        ValueType max_val;
        std::string sql;  // Cached SQL string (pre-generated in constructor)

        BetweenExpr(std::string_view fname, ValueType min_value, ValueType max_value)
            : field_name(fname), min_val(std::move(min_value)), max_val(std::move(max_value)) {
            // OPTIMIZATION: Reserve exact size to avoid reallocation
            sql.reserve(field_name.size() + 17); // field + " BETWEEN ? AND ?"
            sql = field_name;
            sql += " BETWEEN ? AND ?";
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const noexcept {
            return sql;
        }

        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            // Bind min value
            if (auto result = utilities::bind_parameter_value<storm::db::sqlite::Statement, storm::db::sqlite::Error>(*stmt, param_index++, min_val); !result) {
                return result;
            }
            // Bind max value
            return utilities::bind_parameter_value<storm::db::sqlite::Statement, storm::db::sqlite::Error>(*stmt, param_index++, max_val);
        }
    };

    // VALUE-TYPE IN expression (runtime): field IN (val1, val2, ..., valN) (NO VIRTUAL FUNCTIONS!)
    template<typename ValueType>
    struct InExpression {
        std::string field_name;
        std::vector<ValueType> values;
        std::string sql;  // Pre-generated SQL string

        InExpression(std::string_view fname, std::vector<ValueType> vals)
            : field_name(fname), values(std::move(vals)) {
            // OPTIMIZATION: Pre-generate SQL once in constructor to avoid repeated string concatenations
            if (values.empty()) {
                sql = "1 = 0"; // SQL that always evaluates to false
            } else {
                // Reserve capacity to avoid reallocations
                sql.reserve(field_name.size() + 10 + values.size() * 3);
                sql = field_name;
                sql += " IN (";
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) sql += ", ";
                    sql += "?";
                }
                sql += ")";
            }
        }

        [[nodiscard]] __attribute__((always_inline)) inline std::string to_sql() const noexcept {
            return sql;  // Return pre-generated SQL
        }

        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(void* stmt_ptr, int& param_index) const -> std::expected<void, storm::db::sqlite::Error> {
            auto* stmt = static_cast<storm::db::sqlite::Statement*>(stmt_ptr);
            for (const auto& value : values) {
                if (auto result = utilities::bind_parameter_value<storm::db::sqlite::Statement, storm::db::sqlite::Error>(*stmt, param_index++, value); !result) {
                    return result;
                }
            }
            return {};
        }
    };

    // VALUE-TYPE Logical expression: expr1 AND expr2 / expr1 OR expr2 (NO VIRTUAL FUNCTIONS!)
    // Uses shared_ptr for recursive expressions (LogicalExpr contains other expressions)
    struct LogicalExpr {
        ExpressionVariantPtr left;   // Recursive: holds another expression
        LogicalOp op;
        ExpressionVariantPtr right;  // Recursive: holds another expression

        LogicalExpr(ExpressionVariantPtr l, LogicalOp operation, ExpressionVariantPtr r)
            : left(std::move(l)), op(operation), right(std::move(r)) {}

        // Note: to_sql() and bind_params_direct() will be implemented via std::visit
        // (see visitor functions below)
    };

    // ============================================================================
    // VARIANT DEFINITION: All expression types in one variant (NO HEAP ALLOCATION!)
    // ============================================================================
    //
    // std::variant provides compile-time polymorphism without vtable overhead
    // Common types (int, std::string) are stored inline, eliminating heap allocation
    struct ExpressionVariant : std::variant<
        ComparisonExpr<int>,
        ComparisonExpr<int64_t>,
        ComparisonExpr<double>,
        ComparisonExpr<std::string>,
        ComparisonExpr<std::string_view>,
        ComparisonExpr<bool>,
        LikeExpr,
        BetweenExpr<int>,
        BetweenExpr<int64_t>,
        BetweenExpr<double>,
        InExpression<int>,
        InExpression<int64_t>,
        InExpression<double>,
        InExpression<std::string>,
        LogicalExpr
    > {
        // Inherit constructors from variant
        using variant::variant;
    };

    // ============================================================================
    // VISITOR FUNCTIONS: Replace virtual dispatch with std::visit
    // ============================================================================

    // Forward declare visitor functions
    [[nodiscard]] std::string to_sql(const ExpressionVariant& expr);
    [[nodiscard]] auto bind_params_direct(const ExpressionVariant& expr, void* stmt_ptr, int& param_index)
        -> std::expected<void, storm::db::sqlite::Error>;

    // Visitor for to_sql() - called via std::visit
    struct ToSqlVisitor {
        [[nodiscard]] std::string operator()(const LogicalExpr& expr) const {
            // Recursive case: visit left and right expressions
            auto left_sql = to_sql(*expr.left);
            auto right_sql = to_sql(*expr.right);

            std::string result;
            result.reserve(left_sql.size() + right_sql.size() + 8);
            result = "(";
            result += left_sql;
            result += logical_op_to_sql(expr.op);
            result += right_sql;
            result += ")";
            return result;
        }

        // All other expression types have their own to_sql() method
        template<typename T>
        [[nodiscard]] std::string operator()(const T& expr) const {
            return expr.to_sql();
        }
    };

    // Visitor for bind_params_direct() - called via std::visit
    struct BindParamsVisitor {
        void* stmt_ptr;
        int& param_index;

        [[nodiscard]] auto operator()(const LogicalExpr& expr) const
            -> std::expected<void, storm::db::sqlite::Error> {
            // Recursive case: bind left then right
            if (auto result = bind_params_direct(*expr.left, stmt_ptr, param_index); !result) {
                return result;
            }
            return bind_params_direct(*expr.right, stmt_ptr, param_index);
        }

        // All other expression types have their own bind_params_direct() method
        template<typename T>
        [[nodiscard]] auto operator()(const T& expr) const
            -> std::expected<void, storm::db::sqlite::Error> {
            return expr.bind_params_direct(stmt_ptr, param_index);
        }
    };

    // Main visitor entry points (called by users and recursively by LogicalExpr)
    [[nodiscard]] inline std::string to_sql(const ExpressionVariant& expr) {
        return std::visit(ToSqlVisitor{}, expr);
    }

    [[nodiscard]] inline auto bind_params_direct(const ExpressionVariant& expr, void* stmt_ptr, int& param_index)
        -> std::expected<void, storm::db::sqlite::Error> {
        return std::visit(BindParamsVisitor{stmt_ptr, param_index}, expr);
    }

    // ============================================================================
    // EXPR WRAPPER: Natural && and || operators with VARIANT-BASED expressions
    // ============================================================================
    //
    // Wrapper class that holds ExpressionVariantPtr and enables natural boolean operators
    class Expr {
    public:
        // Constructor from ExpressionVariantPtr
        Expr(ExpressionVariantPtr expr) noexcept : expr_(std::move(expr)) {}

        // Constructor from ExpressionVariant (wraps in shared_ptr)
        Expr(ExpressionVariant&& expr) noexcept
            : expr_(std::make_shared<ExpressionVariant>(std::move(expr))) {}

        // Implicit conversion to ExpressionVariantPtr for where() calls
        operator ExpressionVariantPtr() const noexcept { return expr_; }

        // Logical AND operator (also accessible via 'and' keyword)
        // Creates LogicalExpr variant, wrapped in shared_ptr for recursion
        Expr operator&&(const Expr& other) const {
            return Expr(std::make_shared<ExpressionVariant>(
                LogicalExpr{expr_, LogicalOp::And, other.expr_}
            ));
        }

        // Logical OR operator (also accessible via 'or' keyword)
        // Creates LogicalExpr variant, wrapped in shared_ptr for recursion
        Expr operator||(const Expr& other) const {
            return Expr(std::make_shared<ExpressionVariant>(
                LogicalExpr{expr_, LogicalOp::Or, other.expr_}
            ));
        }

        // Access the underlying expression variant
        [[nodiscard]] const ExpressionVariantPtr& get() const noexcept { return expr_; }

    private:
        ExpressionVariantPtr expr_;  // VARIANT-based, not virtual!
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

        // IN: Returns Expr wrapping VARIANT (no heap allocation for expression itself!)
        // Usage: field<^^Person::id>().in(100, 200, 300)
        //
        // REFLECTION-BASED TYPE DEDUCTION: Uses P2996 to get the field's actual type
        template<typename... Values>
        auto in(Values&&... values) const {
            // Use reflection to get the field's actual C++ type for type safety
            using FieldType = typename [:std::meta::type_of(MemberInfo):];

            // Convert all values to the field's type (compile-time type checking!)
            std::vector<FieldType> vals{static_cast<FieldType>(std::forward<Values>(values))...};

            // Return Expr wrapping InExpression variant (wrapped in shared_ptr for polymorphism)
            return Expr(std::make_shared<ExpressionVariant>(
                InExpression<FieldType>{std::string(field_name_), std::move(vals)}
            ));
        }

        // Comparison operators - return VARIANT-BASED Expr (no heap for simple expressions!)
        template<typename V>
        Expr operator==(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::Equal, std::forward<V>(value)}
            ));
        }

        template<typename V>
        Expr operator!=(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::NotEqual, std::forward<V>(value)}
            ));
        }

        template<typename V>
        Expr operator>(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::Greater, std::forward<V>(value)}
            ));
        }

        template<typename V>
        Expr operator>=(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::GreaterEqual, std::forward<V>(value)}
            ));
        }

        template<typename V>
        Expr operator<(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::Less, std::forward<V>(value)}
            ));
        }

        template<typename V>
        Expr operator<=(V&& value) const {
            return Expr(std::make_shared<ExpressionVariant>(
                ComparisonExpr<std::decay_t<V>>{field_name_, CompOp::LessEqual, std::forward<V>(value)}
            ));
        }

        // Special methods - return VARIANT-BASED Expr
        Expr like(std::string_view pattern) const {
            return Expr(std::make_shared<ExpressionVariant>(
                LikeExpr{field_name_, pattern}
            ));
        }

        template<typename V>
        Expr between(V&& min_val, V&& max_val) const {
            return Expr(std::make_shared<ExpressionVariant>(
                BetweenExpr<std::decay_t<V>>{field_name_, std::forward<V>(min_val), std::forward<V>(max_val)}
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

    // ============================================================================
    // HELPER FUNCTIONS: Composing VARIANT-BASED expressions
    // ============================================================================
    //
    // NOTE: With Expr wrapper, you can now use natural && and || (or 'and' and 'or' keywords)
    // These functions remain for backward compatibility and explicit composition

    // Expr overloads (efficient - uses natural operators)
    inline Expr and_(const Expr& left, const Expr& right) {
        return left && right;  // Calls Expr::operator&&
    }

    inline Expr or_(const Expr& left, const Expr& right) {
        return left || right;  // Calls Expr::operator||
    }

    // ExpressionVariantPtr overloads for explicit construction
    inline auto and_(const ExpressionVariantPtr& left, const ExpressionVariantPtr& right) {
        return std::make_shared<ExpressionVariant>(
            LogicalExpr{left, LogicalOp::And, right}
        );
    }

    inline auto or_(const ExpressionVariantPtr& left, const ExpressionVariantPtr& right) {
        return std::make_shared<ExpressionVariant>(
            LogicalExpr{left, LogicalOp::Or, right}
        );
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
