module;

// LINT-EXCLUDE-FILE: file-size, duplicate, complexity
// Pre-existing structural debt tracked under storm issue #264.
// Removed once #264's refactor phases land.

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
import <format>;
import <ranges>;
import <type_traits>;
import <utility>;
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
        using enum CompOp;
        switch (op) {
        case Equal:
            return " = ";
        case NotEqual:
            return " != ";
        case Greater:
            return " > ";
        case GreaterEqual:
            return " >= ";
        case Less:
            return " < ";
        case LessEqual:
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

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return std::format("{}{}?", field_name_, comp_op_to_sql(op_));
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_params_direct(ErasedStatementPtr stmt_ptr, int& param_index) const -> std::expected<void, ErrorType> {
            auto* stmt = static_cast<StmtType*>(stmt_ptr);
            return utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, value_);
        }
    };

    // NULL check expression: field IS NULL / field IS NOT NULL
    struct NullCheckExpr {
        std::string field_name_;
        bool        is_null_; // true = IS NULL, false = IS NOT NULL

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return is_null_ ? std::format("{} IS NULL", field_name_) : std::format("{} IS NOT NULL", field_name_);
        }

        template <typename StmtType, typename ErrorType>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_params_direct(ErasedStatementPtr /*stmt_ptr*/, int& /*param_index*/) const
                -> std::expected<void, ErrorType> {
            return {};
        }
    };

    // LIKE expression: field.like("pattern%")
    struct LikeExpr {
        std::string field_name_;
        std::string pattern_;

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return std::format("{} LIKE ?", field_name_);
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

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            return std::format("{} BETWEEN ? AND ?", field_name_);
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

        [[nodiscard]] __attribute__((always_inline)) auto to_sql() const -> std::string {
            if (values_.empty()) {
                return "1 = 0"; // SQL that always evaluates to false
            }
            auto placeholders = std::views::repeat(std::string_view("?"), values_.size()) |
                                std::views::join_with(std::string_view(", "));
            return std::format("{} IN ({})", field_name_, std::string(placeholders.begin(), placeholders.end()));
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
                                       ComparisonExpr<float>,
                                       ComparisonExpr<std::string>,
                                       ComparisonExpr<std::string_view>,
                                       ComparisonExpr<const char*>,
                                       ComparisonExpr<bool>,
                                       NullCheckExpr,
                                       LikeExpr,
                                       BetweenExpr<int>,
                                       BetweenExpr<int64_t>,
                                       BetweenExpr<double>,
                                       BetweenExpr<float>,
                                       BetweenExpr<std::string>,
                                       InExpression<int>,
                                       InExpression<int64_t>,
                                       InExpression<double>,
                                       InExpression<float>,
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
            return std::format("({}{}{})", to_sql(*expr.left), logical_op_to_sql(expr.op), to_sql(*expr.right));
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
        explicit Expr(ExpressionVariantPtr expr) noexcept : expr_(std::move(expr)) {}

        // Constructor from ExpressionVariant (wraps in shared_ptr)
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - std::move IS used in initializer list
        explicit Expr(ExpressionVariant&& expr) : expr_(std::make_shared<ExpressionVariant>(std::move(expr))) {}

        // Implicit conversion to ExpressionVariantPtr for where() calls
        operator ExpressionVariantPtr() const noexcept { // NOLINT(google-explicit-constructor)
            return expr_;
        }

        // Logical AND operator (also accessible via 'and' keyword)
        auto operator&&(const Expr& other) const -> Expr {
            return Expr(std::make_shared<ExpressionVariant>(LogicalExpr{expr_, LogicalOp::And, other.expr_}));
        }

        // Logical OR operator (also accessible via 'or' keyword)
        auto operator||(const Expr& other) const -> Expr {
            return Expr(std::make_shared<ExpressionVariant>(LogicalExpr{expr_, LogicalOp::Or, other.expr_}));
        }

        // Access the underlying expression
        [[nodiscard]] auto get() const noexcept -> const ExpressionVariantPtr& {
            return expr_;
        }

      private:
        ExpressionVariantPtr expr_; // VARIANT-based, not virtual!
    };

    // Concept: field type is std::optional<T> (nullable in the database)
    template <typename T>
    concept NullableField = requires { typename T::value_type; };

    // CollatedField proxy - wraps field name with COLLATE clause
    // Created via field<^^Person::name>().collate(Collate::NoCase)
    // All comparison operators produce SQL like: "name COLLATE NOCASE = ?"
    template <std::meta::info MemberInfo>
        requires(std::meta::is_nonstatic_data_member(MemberInfo))
    class CollatedField {
      public:
        using FieldType = typename[:std::meta::type_of(MemberInfo):];

        explicit CollatedField(utilities::Collate col) : collated_name_(make_collated_name(col)) {}

        template <typename... Values>
            requires(std::constructible_from<FieldType, Values> && ...)
        auto in(Values&&... values) const {
            return Expr(
                    std::make_shared<ExpressionVariant>(InExpression<FieldType>{
                            .field_name_ = collated_name_, .values_ = {FieldType{std::forward<Values>(values)}...}
                    })
            );
        }

        template <typename V> auto operator==(V&& value) const -> Expr {
            return make_comparison(CompOp::Equal, std::forward<V>(value));
        }

        template <typename V> auto operator!=(V&& value) const -> Expr {
            return make_comparison(CompOp::NotEqual, std::forward<V>(value));
        }

        template <typename V> auto operator>(V&& value) const -> Expr {
            return make_comparison(CompOp::Greater, std::forward<V>(value));
        }

        template <typename V> auto operator>=(V&& value) const -> Expr {
            return make_comparison(CompOp::GreaterEqual, std::forward<V>(value));
        }

        template <typename V> auto operator<(V&& value) const -> Expr {
            return make_comparison(CompOp::Less, std::forward<V>(value));
        }

        template <typename V> auto operator<=(V&& value) const -> Expr {
            return make_comparison(CompOp::LessEqual, std::forward<V>(value));
        }

        [[nodiscard]] auto is_null() const -> Expr
            requires NullableField<FieldType>
        {
            return Expr(
                    std::make_shared<ExpressionVariant>(NullCheckExpr{.field_name_ = collated_name_, .is_null_ = true})
            );
        }

        [[nodiscard]] auto is_not_null() const -> Expr
            requires NullableField<FieldType>
        {
            return Expr(
                    std::make_shared<ExpressionVariant>(NullCheckExpr{.field_name_ = collated_name_, .is_null_ = false})
            );
        }

        auto operator==(std::nullopt_t /*unused*/) const -> Expr
            requires NullableField<FieldType>
        {
            return is_null();
        }
        auto operator!=(std::nullopt_t /*unused*/) const -> Expr
            requires NullableField<FieldType>
        {
            return is_not_null();
        }

        [[nodiscard]] auto like(std::string_view pattern) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(
                            LikeExpr{.field_name_ = collated_name_, .pattern_ = std::string(pattern)}
                    )
            );
        }

        template <typename V> auto between(V&& min_val, V&& max_val) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(BetweenExpr<std::decay_t<V>>{
                            .field_name_ = collated_name_,
                            .min_val_    = std::forward<V>(min_val),
                            .max_val_    = std::forward<V>(max_val)
                    })
            );
        }

      private:
        std::string collated_name_;

        static auto make_collated_name(utilities::Collate col) -> std::string {
            return std::format("{}{}", std::meta::identifier_of(MemberInfo), utilities::collate_to_sql(col));
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto make_comparison(CompOp op, V&& value) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(ComparisonExpr<std::decay_t<V>>{
                            .field_name_ = collated_name_, .op_ = op, .value_ = std::forward<V>(value)
                    })
            );
        }
    };

    // Field proxy - stores reflection info as template parameter
    // Provides compile-time IN expression and runtime comparison/special methods
    template <std::meta::info MemberInfo>
        requires(std::meta::is_nonstatic_data_member(MemberInfo))
    class Field {
      public:
        static constexpr auto field_name_sv = std::meta::identifier_of(MemberInfo);
        using FieldType                     = typename[:std::meta::type_of(MemberInfo):];

        // Return a CollatedField proxy for COLLATE-qualified comparisons
        [[nodiscard]] auto collate(utilities::Collate col) const -> CollatedField<MemberInfo> {
            return CollatedField<MemberInfo>(col);
        }

        // IN: Returns Expr wrapping VARIANT (no heap allocation for expression itself!)
        // Usage: field<^^Person::id>().in(100, 200, 300)
        // For enum types, converts to underlying int automatically
        template <typename... Values>
            requires(std::constructible_from<FieldType, Values> && ...)
        auto in(Values&&... values) const {
            if constexpr (std::is_enum_v<FieldType>) {
                using StoredType = int;
                return Expr(
                        std::make_shared<ExpressionVariant>(InExpression<StoredType>{
                                .field_name_ = std::string(field_name_sv),
                                .values_     = {static_cast<StoredType>(static_cast<std::underlying_type_t<FieldType>>(
                                        FieldType{std::forward<Values>(values)}
                                ))...}
                        })
                );
            } else {
                return Expr(
                        std::make_shared<ExpressionVariant>(InExpression<FieldType>{
                                .field_name_ = std::string(field_name_sv),
                                .values_     = {FieldType{std::forward<Values>(values)}...}
                        })
                );
            }
        }

        // Comparison operators — enum values are auto-converted to underlying int
        template <typename V> auto operator==(V&& value) const -> Expr {
            return make_comp(CompOp::Equal, std::forward<V>(value));
        }
        template <typename V> auto operator!=(V&& value) const -> Expr {
            return make_comp(CompOp::NotEqual, std::forward<V>(value));
        }
        template <typename V> auto operator>(V&& value) const -> Expr {
            return make_comp(CompOp::Greater, std::forward<V>(value));
        }
        template <typename V> auto operator>=(V&& value) const -> Expr {
            return make_comp(CompOp::GreaterEqual, std::forward<V>(value));
        }
        template <typename V> auto operator<(V&& value) const -> Expr {
            return make_comp(CompOp::Less, std::forward<V>(value));
        }
        template <typename V> auto operator<=(V&& value) const -> Expr {
            return make_comp(CompOp::LessEqual, std::forward<V>(value));
        }

        // NULL check methods — constrained to std::optional<T> fields only
        [[nodiscard]] auto is_null() const -> Expr
            requires NullableField<FieldType>
        {
            return Expr(
                    std::make_shared<ExpressionVariant>(
                            NullCheckExpr{.field_name_ = std::string(field_name_sv), .is_null_ = true}
                    )
            );
        }

        [[nodiscard]] auto is_not_null() const -> Expr
            requires NullableField<FieldType>
        {
            return Expr(
                    std::make_shared<ExpressionVariant>(
                            NullCheckExpr{.field_name_ = std::string(field_name_sv), .is_null_ = false}
                    )
            );
        }

        auto operator==(std::nullopt_t /*unused*/) const -> Expr
            requires NullableField<FieldType>
        {
            return is_null();
        }
        auto operator!=(std::nullopt_t /*unused*/) const -> Expr
            requires NullableField<FieldType>
        {
            return is_not_null();
        }

        // Special methods - return VARIANT-BASED Expr
        [[nodiscard]] auto like(std::string_view pattern) const -> Expr {
            return Expr(
                    std::make_shared<ExpressionVariant>(
                            LikeExpr{.field_name_ = std::string(field_name_sv), .pattern_ = std::string(pattern)}
                    )
            );
        }

        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto between(V&& min_val, V&& max_val) const -> Expr {
            using D = std::decay_t<V>;
            if constexpr (std::is_enum_v<D>) {
                using StoredType = int;
                return Expr(
                        std::make_shared<ExpressionVariant>(BetweenExpr<StoredType>{
                                .field_name_ = std::string(field_name_sv),
                                .min_val_    = static_cast<StoredType>(static_cast<std::underlying_type_t<D>>(min_val)),
                                .max_val_    = static_cast<StoredType>(static_cast<std::underlying_type_t<D>>(max_val))
                        })
                );
            } else {
                return Expr(
                        std::make_shared<ExpressionVariant>(BetweenExpr<D>{
                                .field_name_ = std::string(field_name_sv),
                                .min_val_    = std::forward<V>(min_val),
                                .max_val_    = std::forward<V>(max_val)
                        })
                );
            }
        }

      private:
        // Helper: create comparison expression, converting enum values to int
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) - std::forward IS used in braced initializer
        template <typename V> auto make_comp(CompOp op, V&& value) const -> Expr {
            using D = std::decay_t<V>;
            if constexpr (std::is_enum_v<D>) {
                return Expr(
                        std::make_shared<ExpressionVariant>(ComparisonExpr<int>{
                                .field_name_ = std::string(field_name_sv),
                                .op_         = op,
                                .value_      = static_cast<int>(static_cast<std::underlying_type_t<D>>(value))
                        })
                );
            } else {
                return Expr(
                        std::make_shared<ExpressionVariant>(ComparisonExpr<D>{
                                .field_name_ = std::string(field_name_sv), .op_ = op, .value_ = std::forward<V>(value)
                        })
                );
            }
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
