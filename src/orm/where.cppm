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
import storm_orm_utilities; // For ConstexprString

export namespace storm::orm::where {

    // VARIANT-BASED EXPRESSION SYSTEM (eliminates heap allocation and vtable overhead)
    // Instead of virtual inheritance, we use std::variant for compile-time polymorphism
    //
    // Forward declare the variant type
    struct ExpressionVariant;

    // Recursive variant using std::shared_ptr for LogicalExpr children
    // (LogicalExpr needs to hold other expressions, creating recursion)
    using ExpressionVariantPtr = std::shared_ptr<ExpressionVariant>;

    // Type-erased statement pointer for parameter binding.
    // This is intentionally type-erased (void*) to allow the WHERE expression system
    // to bind parameters to any database statement type without knowing the concrete
    // type at compile time. The actual Statement* conversion happens in bind_params_direct().
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) - intentional type erasure
    using ErasedStatementPtr = void*; // NOSONAR(cpp:S5008) - type erasure requires void*
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Comparison operators
    enum class CompOp { Equal, NotEqual, Greater, GreaterEqual, Less, LessEqual };

    constexpr auto comp_op_to_sql(CompOp op) noexcept -> std::string_view {
        switch (op) {
        case CompOp::Equal:
            return " = ";
        case CompOp::NotEqual:
            return " != ";
        case CompOp::Greater:
            return " > ";
        case CompOp::GreaterEqual:
            return " >= ";
        case CompOp::Less:
            return " < ";
        case CompOp::LessEqual:
            return " <= ";
        }
        return " = "; // LCOV_EXCL_LINE - unreachable: all enum values handled above
    }

    // Logical operators
    enum class LogicalOp { And, Or };

    constexpr auto logical_op_to_sql(LogicalOp op) noexcept -> std::string_view {
        switch (op) {
        case LogicalOp::And:
            return " AND ";
        case LogicalOp::Or:
            return " OR ";
        }
        return " AND "; // LCOV_EXCL_LINE - unreachable: all enum values handled above
    }

    // VALUE-TYPE Comparison expression: field > value (NO VIRTUAL FUNCTIONS!)
    template <typename ValueType> struct ComparisonExpr {
        std::string field_name_;
        CompOp      op_;
        ValueType   value_;
        std::string sql_; // Cached SQL string (pre-generated in constructor)

        ComparisonExpr(std::string_view field_name, CompOp op, ValueType value)
            : field_name_(field_name), op_(op), value_(std::move(value)), sql_(field_name) {
            // Pre-generate SQL in constructor for consistency with InExpression
            // and to benchmark whether simple concatenation benefits from caching
            sql_ += comp_op_to_sql(op);
            sql_ += "?";
        }

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return sql_;
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_params_direct(ErasedStatementPtr stmt_ptr, int& param_index) const -> std::expected<void, ErrorType> {
            auto* stmt = static_cast<StmtType*>(stmt_ptr);
            return utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, value_);
        }
    };

    // LIKE expression: field.like("pattern%")
    struct LikeExpr {
        std::string field_name_;
        std::string pattern_;
        std::string sql_; // Cached SQL string (pre-generated in constructor)

        LikeExpr(std::string_view field_name, std::string_view pattern)
            : field_name_(field_name), pattern_(pattern), sql_(field_name) {
            // Pre-generate SQL in constructor for consistency with ComparisonExpr
            sql_ += " LIKE ?";
        }

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return sql_;
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_params_direct(ErasedStatementPtr stmt_ptr, int& param_index) const -> std::expected<void, ErrorType> {
            auto* stmt = static_cast<StmtType*>(stmt_ptr);
            return stmt->bind_text(param_index++, pattern_);
        }
    };

    // BETWEEN expression: field.between(a, b)
    template <typename ValueType> struct BetweenExpr {
        std::string field_name_;
        ValueType   min_val_;
        ValueType   max_val_;
        std::string sql_; // Cached SQL string (pre-generated in constructor)

        BetweenExpr(std::string_view field_name, ValueType min_val, ValueType max_val)
            : field_name_(field_name), min_val_(std::move(min_val)), max_val_(std::move(max_val)), sql_(field_name) {
            // Pre-generate SQL in constructor for consistency with ComparisonExpr
            sql_ += " BETWEEN ? AND ?";
        }

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return sql_;
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(ErasedStatementPtr stmt_ptr, int& param_index) const
                -> std::expected<void, ErrorType> {
            auto* stmt = static_cast<StmtType*>(stmt_ptr);
            // Bind min value
            if (auto result = utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, min_val_);
                !result) {
                return result;
            }
            // Bind max value
            return utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, max_val_);
        }
    };

    // IN expression (runtime): field IN (val1, val2, ..., valN)
    template <typename ValueType> struct InExpression {
        std::string            field_name_;
        std::vector<ValueType> values_;
        std::string            sql_; // Pre-generated SQL string

        InExpression(std::string_view field_name, std::vector<ValueType> values)
            : field_name_(field_name), values_(std::move(values)) {
            // OPTIMIZATION: Pre-generate SQL once in constructor to avoid repeated string concatenations
            if (values_.empty()) {
                sql_ = "1 = 0"; // SQL that always evaluates to false
            } else {
                // Reserve capacity to avoid reallocations
                sql_.reserve(field_name_.size() + utilities::sql_len::IN_CLAUSE + (values_.size() * 3));
                sql_ = field_name_;
                sql_ += " IN (";
                for (size_t i = 0; i < values_.size(); ++i) {
                    if (i > 0) {
                        sql_ += ", ";
                    }
                    sql_ += "?";
                }
                sql_ += ")";
            }
        }

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return sql_; // Return pre-generated SQL
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((hot)) auto bind_params_direct(ErasedStatementPtr stmt_ptr, int& param_index) const
                -> std::expected<void, ErrorType> {
            auto* stmt = static_cast<StmtType*>(stmt_ptr);
            for (const auto& value : values_) {
                if (auto result = utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, value);
                    !result) {
                    return result;
                }
            }
            return {};
        }
    };

    // Logical expression: expr1 AND expr2 / expr1 OR expr2
    struct LogicalExpr {
        ExpressionVariantPtr left; // Recursive: holds another expression
        LogicalOp            op;
        ExpressionVariantPtr right; // Recursive: holds another expression

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
                                       ComparisonExpr<const char*>,
                                       ComparisonExpr<bool>,
                                       LikeExpr,
                                       BetweenExpr<int>,
                                       BetweenExpr<int64_t>,
                                       BetweenExpr<double>,
                                       InExpression<int>,
                                       InExpression<int64_t>,
                                       InExpression<double>,
                                       InExpression<std::string>,
                                       LogicalExpr> {
        // Inherit constructors from variant
        using variant::variant;
    };

    // Recursive variant using std::shared_ptr for LogicalExpr children
    // (LogicalExpr needs to hold other expressions, creating recursion)
    using ExpressionVariantPtr = std::shared_ptr<ExpressionVariant>;

    // ============================================================================
    // VISITOR FUNCTIONS: Replace virtual dispatch with std::visit
    // ============================================================================

    // Forward declare visitor functions
    [[nodiscard]] auto to_sql(const ExpressionVariant& expr) -> std::string;

    template <typename StmtType, typename ErrorType>
    [[nodiscard]] auto bind_params_direct(const ExpressionVariant& expr, ErasedStatementPtr stmt_ptr, int& param_index)
            -> std::expected<void, ErrorType>;

    // Visitor for to_sql() - called via std::visit
    struct ToSqlVisitor {
        [[nodiscard]] auto operator()(const LogicalExpr& expr) const -> std::string {
            // Recursive case: visit left and right expressions
            auto left_sql  = to_sql(*expr.left);
            auto right_sql = to_sql(*expr.right);

            std::string result;
            result.reserve(left_sql.size() + right_sql.size() + utilities::sql_len::LOGICAL_OP_PARENS);
            result = "(";
            result += left_sql;
            result += logical_op_to_sql(expr.op);
            result += right_sql;
            result += ")";
            return result;
        }

        // All other expression types have their own to_sql() method
        template <typename T> [[nodiscard]] auto operator()(const T& expr) const -> std::string {
            return expr.to_sql();
        }
    };

    // Visitor for bind_params_direct() - called via std::visit
    template <typename StmtType, typename ErrorType> struct BindParamsVisitor {
        ErasedStatementPtr stmt_ptr;
        int* param_index; // Pointer instead of reference (cppcoreguidelines-avoid-const-or-ref-data-members)

        [[nodiscard]] auto operator()(const LogicalExpr& expr) const -> std::expected<void, ErrorType> {
            // Recursive case: bind left then right
            if (auto result = bind_params_direct<StmtType, ErrorType>(*expr.left, stmt_ptr, *param_index); !result) {
                return result;
            }
            return bind_params_direct<StmtType, ErrorType>(*expr.right, stmt_ptr, *param_index);
        }

        // All other expression types have their own bind_params_direct() method
        template <typename T> [[nodiscard]] auto operator()(const T& expr) const -> std::expected<void, ErrorType> {
            return expr.template bind_params_direct<StmtType, ErrorType>(stmt_ptr, *param_index);
        }
    };

    // Main visitor entry points (called by users and recursively by LogicalExpr)
    [[nodiscard]] inline auto to_sql(const ExpressionVariant& expr) -> std::string {
        return std::visit(ToSqlVisitor{}, expr);
    }

    template <typename StmtType, typename ErrorType>
    [[nodiscard]] inline auto
    bind_params_direct(const ExpressionVariant& expr, ErasedStatementPtr stmt_ptr, int& param_index)
            -> std::expected<void, ErrorType> {
        return std::visit(
                BindParamsVisitor<StmtType, ErrorType>{.stmt_ptr = stmt_ptr, .param_index = &param_index}, expr
        );
    }

    // Expression wrapper to enable natural && and || operators without ambiguity
    // Solves the problem of shared_ptr's implicit bool conversion conflicting with built-in operators
    class Expr {
      public:
        // Constructor from ExpressionVariantPtr
        Expr(ExpressionVariantPtr expr) noexcept : expr_(std::move(expr)) {}

        // Constructor from ExpressionVariant (wraps in shared_ptr)
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - std::move IS used in initializer list
        explicit Expr(ExpressionVariant&& expr) : expr_(std::make_shared<ExpressionVariant>(std::move(expr))) {}

        // Implicit conversion to ExpressionVariantPtr for where() calls
        operator ExpressionVariantPtr() const noexcept { // NOLINT(google-explicit-constructor)
            return expr_;
        }

        // Logical AND operator (also accessible via 'and' keyword)
        auto operator&&(const Expr& other) const -> Expr {
            return {std::make_shared<ExpressionVariant>(LogicalExpr{expr_, LogicalOp::And, other.expr_})};
        }

        // Logical OR operator (also accessible via 'or' keyword)
        auto operator||(const Expr& other) const -> Expr {
            return {std::make_shared<ExpressionVariant>(LogicalExpr{expr_, LogicalOp::Or, other.expr_})};
        }

        // Access the underlying expression
        [[nodiscard]] auto get() const noexcept -> const ExpressionVariantPtr& {
            return expr_;
        }

      private:
        ExpressionVariantPtr expr_; // VARIANT-based, not virtual!
    };

    // Field proxy - stores reflection info as template parameter
    // Provides compile-time IN expression and runtime comparison/special methods
    template <std::meta::info MemberInfo>
        requires(std::meta::is_nonstatic_data_member(MemberInfo))
    class Field {
      public:
        static constexpr auto field_name_sv = std::meta::identifier_of(MemberInfo);
        using FieldType                     = typename[:std::meta::type_of(MemberInfo):];

        // IN: Returns Expr wrapping VARIANT (no heap allocation for expression itself!)
        // Usage: field<^^Person::id>().in(100, 200, 300)
        template <typename... Values>
            requires(std::constructible_from<FieldType, Values> && ...)
        auto in(Values&&... values) const {
            return Expr(
                    std::make_shared<ExpressionVariant>(InExpression<FieldType>{
                            std::string(field_name_sv), {FieldType{std::forward<Values>(values)}...}
                    })
            );
        }

        // Comparison operators - return runtime Expr for flexibility
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator==(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::Equal, std::forward<V>(value)
                    })
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator!=(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::NotEqual, std::forward<V>(value)
                    })
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator>(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::Greater, std::forward<V>(value)
                    })
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator>=(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::GreaterEqual, std::forward<V>(value)
                    })
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator<(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::Less, std::forward<V>(value)
                    })
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto operator<=(V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            std::string(field_name_sv), CompOp::LessEqual, std::forward<V>(value)
                    })
            );
        }

        // Special methods - return VARIANT-BASED Expr
        [[nodiscard]] auto like(std::string_view pattern) const -> Expr {
            return Expr(std::make_shared<ExpressionVariant>(LikeExpr{std::string(field_name_sv), pattern}));
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto between(V&& min_val, V&& max_val) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(BetweenExpr<std::decay_t<V>>{
                            std::string(field_name_sv), std::forward<V>(min_val), std::forward<V>(max_val)
                    })
            );
        }
    };

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
    template <std::meta::info MemberInfo>
        requires(
                std::meta::is_nonstatic_data_member(MemberInfo) && std::meta::has_identifier(MemberInfo)
        ) // Ensures field has a name
    constexpr auto field() {
        // Additional compile-time validation: field must be accessible
        static_assert(
                std::meta::is_nonstatic_data_member(MemberInfo),
                "field<> requires a non-static data member reflection (use ^^Type::member syntax)"
        );
        return Field<MemberInfo>();
    }

} // namespace storm::orm::where
