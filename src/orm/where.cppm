module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (BindParamsVisitor casts the type-erased pointer once and calls each Expr's
// bind_impl; make_null_check_expr shared by CollatedField/Field).

#include <meta>

export module storm_orm_where;

import std;

import storm_orm_utilities;     // For ConstexprString
import storm_orm_field_attr;    // ValidFieldInfo — gate the field-selector NTTP (#478)
import storm_orm_relation_meta; // is_relation_field — reject m2m/reverse_fk members in Field<M> (#408)

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
    using ErasedStatementPtr = void*; // NOSONAR(cpp:S5008) - type erasure requires void*

    // Comparison operators
    enum class CompOp : std::uint8_t { Equal, NotEqual, Greater, GreaterEqual, Less, LessEqual };

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
    enum class LogicalOp : std::uint8_t { And, Or };

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
        [[nodiscard]] __attribute__((always_inline)) auto bind_impl(StmtType* stmt, int& param_index) const
                -> std::expected<void, ErrorType> {
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
        [[nodiscard]] __attribute__((always_inline)) auto bind_impl(StmtType* /*stmt*/, int& /*param_index*/) const
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
        [[nodiscard]] __attribute__((always_inline)) auto bind_impl(StmtType* stmt, int& param_index) const
                -> std::expected<void, ErrorType> {
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
        [[nodiscard]] __attribute__((hot)) auto bind_impl(StmtType* stmt, int& param_index) const
                -> std::expected<void, ErrorType> {
            if (auto result = utilities::bind_parameter_value<StmtType, ErrorType>(*stmt, param_index++, min_val_);
                !result) {
                return result;
            }
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
        [[nodiscard]] __attribute__((hot)) auto bind_impl(StmtType* stmt, int& param_index) const
                -> std::expected<void, ErrorType> {
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
                                       ComparisonExpr<std::int64_t>,
                                       ComparisonExpr<double>,
                                       ComparisonExpr<float>,
                                       ComparisonExpr<std::string>,
                                       ComparisonExpr<bool>,
                                       ComparisonExpr<std::chrono::year_month_day>,
                                       ComparisonExpr<std::chrono::system_clock::time_point>,
                                       ComparisonExpr<utilities::UUID>,
                                       NullCheckExpr,
                                       LikeExpr,
                                       BetweenExpr<int>,
                                       BetweenExpr<std::int64_t>,
                                       BetweenExpr<double>,
                                       BetweenExpr<float>,
                                       BetweenExpr<std::string>,
                                       BetweenExpr<std::chrono::year_month_day>,
                                       BetweenExpr<std::chrono::system_clock::time_point>,
                                       InExpression<int>,
                                       InExpression<std::int64_t>,
                                       InExpression<double>,
                                       InExpression<float>,
                                       InExpression<std::string>,
                                       InExpression<std::chrono::year_month_day>,
                                       InExpression<std::chrono::system_clock::time_point>,
                                       InExpression<utilities::UUID>,
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

    // Main visitor entry points (called by users and recursively by LogicalExpr)
    [[nodiscard]] inline auto to_sql(const ExpressionVariant& expr) -> std::string {
        return std::visit(ToSqlVisitor{}, expr);
    }

    // bind_params_direct uses a lambda visitor that captures param_index by reference,
    // so no raw pointer/reference data members are required.
    // Every Expr type exposes a small `bind_impl(StmtType*, int&)`. The type-erased
    // pointer cast used to be duplicated in each Expr's own `bind_params_direct` — that's
    // the duplicate this visitor now centralises.
    template <typename StmtType, typename ErrorType>
    [[nodiscard]] inline auto
    bind_params_direct(const ExpressionVariant& expr, ErasedStatementPtr stmt_ptr, int& param_index)
            -> std::expected<void, ErrorType> {
        return std::visit(
                [stmt_ptr, &param_index](const auto& node) -> std::expected<void, ErrorType> {
                    using NodeT = std::remove_cvref_t<decltype(node)>;
                    if constexpr (std::is_same_v<NodeT, LogicalExpr>) {
                        if (auto result = bind_params_direct<StmtType, ErrorType>(*node.left, stmt_ptr, param_index);
                            !result) {
                            return result;
                        }
                        return bind_params_direct<StmtType, ErrorType>(*node.right, stmt_ptr, param_index);
                    } else {
                        return node
                                .template bind_impl<StmtType, ErrorType>(static_cast<StmtType*>(stmt_ptr), param_index);
                    }
                },
                expr
        );
    }

    // Expression wrapper to enable natural && and || operators without ambiguity
    // Solves the problem of shared_ptr's implicit bool conversion conflicting with built-in operators
    class Expr {
      public:
        // Constructor from ExpressionVariantPtr
        explicit Expr(ExpressionVariantPtr expr) noexcept : expr_(std::move(expr)) {}

        explicit Expr(ExpressionVariant&& expr) : expr_(std::make_shared<ExpressionVariant>(std::move(expr))) {}

        // Implicit conversion to ExpressionVariantPtr for where() calls
        operator ExpressionVariantPtr() const noexcept {
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

    // Free helpers for null-check expression construction. Both CollatedField
    // and Field used to spell out the four nullable methods (is_null, is_not_null,
    // operator==(nullopt_t), operator!=(nullopt_t)) body-for-body identical.
    // Now both classes route through these one-line helpers.
    [[nodiscard]] inline auto make_null_check_expr(std::string field_name, bool is_null) -> Expr {
        return Expr(
                std::make_shared<ExpressionVariant>(
                        NullCheckExpr{.field_name_ = std::move(field_name), .is_null_ = is_null}
                )
        );
    }

    // Normalize a comparison operand to the type actually stored in ExpressionVariant.
    // The expression node can outlive the operand's source buffer (where() is deferred, #352),
    // so text operands (string_view / const char* / char arrays) are copied into an owning
    // std::string. Enum operands are stored as their underlying int. Narrow / unsigned integer
    // operands fold to the int / int64_t variant arm (no dedicated arm per source type — #407),
    // mirroring the enum fold. bool keeps its own arm. Everything else is decayed.
    template <typename V>
        requires utilities::BindableType<std::decay_t<V>>
    auto normalize_operand(V&& value) {
        using D = std::decay_t<V>;
        if constexpr (std::is_enum_v<D>) {
            return static_cast<int>(static_cast<std::underlying_type_t<D>>(value));
        } else if constexpr (!std::is_same_v<D, bool> && utilities::is_int64_source_v<D>) {
            return static_cast<std::int64_t>(value);
        } else if constexpr (!std::is_same_v<D, bool> && utilities::is_int_source_v<D>) {
            return static_cast<int>(value);
        } else if constexpr (std::is_convertible_v<D, std::string_view> && !std::is_same_v<D, std::string>) {
            return std::string(std::string_view(std::forward<V>(value)));
        } else {
            return D{std::forward<V>(value)};
        }
    }

    // Build a value-owning ComparisonExpr from any operand. See normalize_operand for the type rules.
    template <typename V>
    [[nodiscard]] auto make_comparison_expr(const std::string& field_name, CompOp op, V&& value) -> Expr {
        auto stored = normalize_operand(std::forward<V>(value));
        return Expr(
                std::make_shared<ExpressionVariant>(ComparisonExpr<decltype(stored)>{
                        .field_name_ = std::move(field_name), .op_ = op, .value_ = std::move(stored)
                })
        );
    }

    // Build a LikeExpr for `field_name LIKE pattern`. Shared by Field::like and
    // CollatedField::like so the two bodies cannot drift (same rationale as make_in_expr's
    // #578 note).
    [[nodiscard]] inline auto make_like_expr(std::string field_name, std::string_view pattern) -> Expr {
        return Expr(
                std::make_shared<ExpressionVariant>(
                        LikeExpr{.field_name_ = std::move(field_name), .pattern_ = std::string(pattern)}
                )
        );
    }

    // Build a value-owning BetweenExpr from its bounds. Both bounds go through normalize_operand
    // (#406): text operands are copied into an owning std::string (closes the deferred-bind dangle
    // #352 fixed for comparisons) and enums fold to int — the same rules as make_comparison_expr.
    template <typename V>
    [[nodiscard]] auto make_between_expr(const std::string& field_name, V&& min_val, V&& max_val) -> Expr {
        auto stored_min = normalize_operand(std::forward<V>(min_val));
        auto stored_max = normalize_operand(std::forward<V>(max_val));
        return Expr(
                std::make_shared<ExpressionVariant>(BetweenExpr<decltype(stored_min)>{
                        .field_name_ = std::move(field_name),
                        .min_val_    = std::move(stored_min),
                        .max_val_    = std::move(stored_max)
                })
        );
    }

    // Build a value-owning InExpression from a value pack constructed to TargetType, each element
    // routed through normalize_operand so the stored element type matches an existing variant arm
    // for folding categories — enums and narrow/unsigned ints fold to int/int64_t, text to
    // std::string (#407, same rules as make_comparison_expr). Shared by Field::in and
    // CollatedField::in so the two bodies cannot drift the way they did for #578: CollatedField::in
    // used to construct InExpression<FieldType> directly, naming a variant arm that does not exist
    // for a folding FieldType.
    template <typename TargetType, typename... Values>
        requires(std::constructible_from<TargetType, Values> && ...)
    [[nodiscard]] auto make_in_expr(const std::string& field_name, Values&&... values) -> Expr {
        using StoredType = decltype(normalize_operand(std::declval<TargetType>()));
        return Expr(
                std::make_shared<ExpressionVariant>(InExpression<StoredType>{
                        .field_name_ = std::move(field_name),
                        .values_     = {normalize_operand(TargetType{std::forward<Values>(values)})...}
                })
        );
    }

    // CollatedField proxy - wraps field name with COLLATE clause
    // Created via fields::Person.name.collate(Collate::NoCase)
    // All comparison operators produce SQL like: "name COLLATE NOCASE = ?"
    // NOTE: NOT gated on SingleColumnMember here (tried and reverted — #575 review): Field<M>
    // must instantiate for EVERY member (field_specs_for emits a FieldRef<M> unconditionally),
    // and Field::collate()'s non-deduced return type `-> CollatedField<MemberInfo>` requires
    // this class's constraints to be satisfied at THAT instantiation, not merely at the call
    // site — confirmed by compiling: a class-level gate here breaks model declaration for any
    // composite-PK FK member, hard-erroring inside field_specs_for's define_aggregate rather
    // than at collate()'s own call site. Field::collate() carries the gate instead (its
    // per-method requires-clause is checked at overload resolution, which IS deferred), and
    // that is the only construction path for this class in the tree — a CAUTION comment at
    // Field::field_name_sv documents the analogous invariant this class shares.
    template <std::meta::info MemberInfo>
        requires(storm::meta::ValidFieldInfo<MemberInfo> && !storm::meta::is_relation_field(MemberInfo))
    class CollatedField {
      public:
        using FieldType = typename[:std::meta::type_of(MemberInfo):];

        explicit CollatedField(utilities::Collate col) : collated_name_(make_collated_name(col)) {}

        // in() is rejected on ANY FK member (single- or composite-PK target), not only
        // composite (#575 review): make_in_expr's operand type is FieldType, which for an
        // FK member is the RELATED MODEL TYPE, not its key — normalize_operand can never
        // bind a model struct, so an unguarded in() on an FK member is a hard error deep
        // inside make_in_expr instead of a clean rejection at this call site. The other
        // comparison operators are unaffected: they route the raw operand through
        // normalize_operand directly, never through FieldType, so `sender == 1` works.
        template <typename... Values>
            requires(std::constructible_from<FieldType, Values> && ...) &&
                    storm::meta::SingleColumnMember<MemberInfo> && (!storm::meta::is_fk_field(MemberInfo))
        auto in(Values&&... values) const {
            return where::make_in_expr<FieldType>(collated_name_, std::forward<Values>(values)...);
        }

        template <typename V>
        auto operator==(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::Equal, std::forward<V>(value));
        }

        template <typename V>
        auto operator!=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::NotEqual, std::forward<V>(value));
        }

        template <typename V>
        auto operator>(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::Greater, std::forward<V>(value));
        }

        template <typename V>
        auto operator>=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::GreaterEqual, std::forward<V>(value));
        }

        template <typename V>
        auto operator<(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::Less, std::forward<V>(value));
        }

        template <typename V>
        auto operator<=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comparison(CompOp::LessEqual, std::forward<V>(value));
        }

        // clang-format off
        [[nodiscard]] auto is_null()     const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return make_null_check_expr(collated_name_, true); }
        [[nodiscard]] auto is_not_null() const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return make_null_check_expr(collated_name_, false); }
        auto operator==(std::nullopt_t /*unused*/)  const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return is_null(); }
        auto operator!=(std::nullopt_t /*unused*/)  const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return is_not_null(); }
        // clang-format on

        [[nodiscard]] auto like(std::string_view pattern) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return where::make_like_expr(collated_name_, pattern);
        }

        template <typename V>
        auto between(V&& min_val, V&& max_val) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return where::make_between_expr(collated_name_, std::forward<V>(min_val), std::forward<V>(max_val));
        }

      private:
        std::string collated_name_;

        static auto make_collated_name(utilities::Collate col) -> std::string {
            return std::format("{}{}", storm::meta::column_name_view<MemberInfo>, utilities::collate_to_sql(col));
        }

        template <typename V> auto make_comparison(CompOp op, V&& value) const -> Expr {
            return where::make_comparison_expr(collated_name_, op, std::forward<V>(value));
        }
    };

    // Field proxy - stores reflection info as template parameter
    // Provides compile-time IN expression and runtime comparison/special methods
    template <std::meta::info MemberInfo>
        requires(storm::meta::ValidFieldInfo<MemberInfo> && !storm::meta::is_relation_field(MemberInfo))
    class Field {
      public:
        // The SQL column name (#575): a plain member's identifier, or an FK member's
        // "<member>_id" — the same canonical writer ORDER BY/COUNT(DISTINCT) route
        // through (#570). Byte-identical to std::meta::identifier_of(MemberInfo) for a
        // non-FK member. CAUTION: for an FK to a COMPOSITE-PK target this still evaluates
        // (Field<M> must instantiate for every member — field_specs_for emits a FieldRef<M>
        // unconditionally) but yields a column that does NOT exist ("line_id" instead of
        // "line_order_id"/"line_product_id"), because column_name_view always assumes a
        // single "_id" suffix. Every method below gates on SingleColumnMember<MemberInfo>
        // before reading this; a new method that reads field_name_sv MUST gate the same way.
        static constexpr auto field_name_sv = storm::meta::column_name_view<MemberInfo>;
        using FieldType                     = typename[:std::meta::type_of(MemberInfo):];

        // Return a CollatedField proxy for COLLATE-qualified comparisons
        [[nodiscard]] auto collate(utilities::Collate col) const -> CollatedField<MemberInfo>
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return CollatedField<MemberInfo>(col);
        }

        // IN: Returns Expr wrapping VARIANT (no heap allocation for expression itself!)
        // Usage: fields::Person.id.in(100, 200, 300)
        // Each value is constructed to FieldType, then routed through normalize_operand so the
        // stored element type matches the variant arm — enums/narrow ints fold to int/int64_t,
        // text to std::string, temporal/UUID keep their own arm (#407, same rules as comparisons).
        // Rejected on an FK member (#575 review, see CollatedField::in's comment): FieldType
        // is the related model, not its key, and normalize_operand can never bind a struct.
        template <typename... Values>
            requires(std::constructible_from<FieldType, Values> && ...) &&
                    storm::meta::SingleColumnMember<MemberInfo> && (!storm::meta::is_fk_field(MemberInfo))
        auto in(Values&&... values) const {
            return where::make_in_expr<FieldType>(std::string(field_name_sv), std::forward<Values>(values)...);
        }

        // Comparison operators — enum values are auto-converted to underlying int.
        // Constrained to a single-column member (#575): an FK to a composite-PK target
        // spreads over "<member>_<part>" columns, so there is no single name to compare
        // against and no correct multi-column form for these operators either — rejected
        // at compile time rather than mis-emitted, matching the #570 ORDER BY/COUNT(DISTINCT)
        // policy. WHERE (a, b) = (?, ?) row-value syntax exists on both backends but is not
        // wired here: it would only ever be well-defined for ==/!=, not for >, LIKE, BETWEEN, etc.
        template <typename V>
        auto operator==(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::Equal, std::forward<V>(value));
        }
        template <typename V>
        auto operator!=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::NotEqual, std::forward<V>(value));
        }
        template <typename V>
        auto operator>(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::Greater, std::forward<V>(value));
        }
        template <typename V>
        auto operator>=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::GreaterEqual, std::forward<V>(value));
        }
        template <typename V>
        auto operator<(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::Less, std::forward<V>(value));
        }
        template <typename V>
        auto operator<=(V&& value) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return make_comp(CompOp::LessEqual, std::forward<V>(value));
        }

        // NULL check methods — constrained to std::optional<T> fields only
        // clang-format off
        [[nodiscard]] auto is_null()     const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return make_null_check_expr(std::string(field_name_sv), true); }
        [[nodiscard]] auto is_not_null() const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return make_null_check_expr(std::string(field_name_sv), false); }
        auto operator==(std::nullopt_t /*unused*/)  const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return is_null(); }
        auto operator!=(std::nullopt_t /*unused*/)  const -> Expr requires (NullableField<FieldType> && storm::meta::SingleColumnMember<MemberInfo>) { return is_not_null(); }
        // clang-format on

        // Special methods - return VARIANT-BASED Expr
        [[nodiscard]] auto like(std::string_view pattern) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return where::make_like_expr(std::string(field_name_sv), pattern);
        }

        template <typename V>
        auto between(V&& min_val, V&& max_val) const -> Expr
            requires storm::meta::SingleColumnMember<MemberInfo>
        {
            return where::make_between_expr(
                    std::string(field_name_sv), std::forward<V>(min_val), std::forward<V>(max_val)
            );
        }

      private:
        // Helper: create comparison expression, converting enum values to int
        template <typename V> auto make_comp(CompOp op, V&& value) const -> Expr {
            return where::make_comparison_expr(std::string(field_name_sv), op, std::forward<V>(value));
        }
    };

} // namespace storm::orm::where
