module;

// Module global fragment - third-party C headers (macros not exported by
// modules)
#include <sqlite3.h>

// Define the module
export module storm.query_set;

// Import required modules
import storm.core_types; // For SqlValue, Op, Collation
import storm.condition;  // For storm::Condition
import storm.aggregate;
import storm.where;
import storm.field; // For Field class

// Storm modules
import storm.statement.base;
import storm.statement.insert;
import storm.statement.select;
import storm.statement.update;
import storm.statement.remove;
import storm.expression;
import storm.connection;
import storm.reflect;
import storm.utils;
import storm.join_utils;
import storm.type_traits;

// Import standard header units in the global module fragment
import <string>;
import <utility>;
import <memory>;
import <any>;
import <vector>;
import <expected>;
import <unordered_map>;
import <type_traits>;
import <variant>;
import <map>;
import <ranges>;
import <cstdint>;
import <sstream>;
import <print>;
import <string_view>;
import <optional>;
import <concepts>;
import <format>;
import <algorithm>;
import <array>;
import <functional>;

export namespace storm {
    // Use the canonical SqlValue type from storm.core_types to avoid redundancy
    using ValueMap                              = std::map<std::string, SqlValue, std::less<>>;
    using ValueVectorMap                        = std::vector<ValueMap>;
    using ExpectedValueVectorMap                = std::expected<ValueVectorMap, std::string>;
    template <typename T> using ExpectedT       = std::expected<T, std::string>;
    template <typename T> using ExpectedVectorT = std::expected<std::vector<T>, std::string>;

    // Forward declaration for concepts
    template <class T> class QuerySet;

    // C++23 Concepts for compile-time validation
    template <typename R>
    concept AggregateReturnType = std::is_arithmetic_v<R> || std::same_as<R, std::string>;

    template <typename F>
    concept SimpleFunctionType = std::invocable<F, std::function<void(std::string_view)>>;

    template <class T> class QuerySet {
      private:
        std::shared_ptr<Connection>                                            conn;
        std::optional<storm::Where>                                            _whereExpression;
        std::vector<JoinWrapper>                                               join_clauses;
        std::vector<std::tuple<refl::FieldWrapper, bool, Collation>>           orderTerms;
        std::vector<refl::FieldWrapper>                                        distinctFields;
        std::vector<std::pair<refl::FieldWrapper, std::optional<std::string>>> onlyFields;
        std::vector<AggregateSpec>                                             functionsSet;
        std::vector<refl::FieldWrapper>                                        groupByFields;

        int _limit{};
        int _offset{};

      public:
        // Constructors
        QuerySet() = default;
        explicit QuerySet(std::shared_ptr<Connection> connection) : conn(std::move(connection)) {}

        // Copy constructor - deep copy unique_ptr members where needed (value types
        // copy trivially)
        QuerySet(const QuerySet& other)
            : conn(other.conn)
            , _whereExpression(other._whereExpression)
            , join_clauses(other.join_clauses)
            , orderTerms(other.orderTerms)
            , functionsSet(other.functionsSet)
            , _limit(other._limit)
            , _offset(other._offset) {
            // Value semantics: vectors copy directly
            distinctFields = other.distinctFields;
            onlyFields     = other.onlyFields;
            groupByFields  = other.groupByFields;
        }

        // Move constructor
        QuerySet(QuerySet&& other) noexcept = default;

        // Copy assignment operator
        QuerySet& operator=(const QuerySet& other) {
            if (this != &other) {
                QuerySet temp(other);    // Use copy constructor
                *this = std::move(temp); // Use move assignment
            }
            return *this;
        }

        // Move assignment operator
        QuerySet& operator=(QuerySet&& other) noexcept = default;

        // WHERE API (declarations)
        QuerySet<T>& where(const storm::Where& where_clause);
        QuerySet<T>& where(storm::Where&& where_clause);

        template <auto MemberPtr, typename Value> QuerySet<T>& where(Value&& value, storm::Op op = storm::Op::EQ);

        template <auto MemberPtr, typename Container> QuerySet<T>& where_in(const Container& values);

        template <auto MemberPtr, typename Value> QuerySet<T>& where_like(Value&& pattern);

        template <auto MemberPtr, typename T1, typename T2> QuerySet<T>& where_between(T1&& value1, T2&& value2);

        template <auto MemberPtr> QuerySet<T>& where_not_null();

        template <auto MemberPtr> QuerySet<T>& where_is_null();

        // ORDER BY API (declarations)
        // Single field
        template <auto Field, Collation CollationType = Collation::NONE> QuerySet<T>& order_by();

        // Multiple fields
        template <auto... Fields> QuerySet<T>& order_by_multiple();

        // Multiple field-direction pairs
        template <auto Field, auto Direction, auto... Rest> QuerySet<T>& order_by_mixed();

        // With collation support
        template <auto Field, auto Direction, auto Coll, auto... Rest> QuerySet<T>& order_by_full();

        // DISTINCT API (declarations)
        template <auto... Fields> QuerySet<T>& distinct();

        // ONLY API (declarations)
        template <auto... Fields> QuerySet<T>& only(const std::optional<std::string>& alias = std::nullopt);

        // GROUP BY API (declarations)
        template <auto... Fields> QuerySet<T>& group_by();

        template <auto FirstField, auto... RestFields>
        QuerySet<T>& group_concat(
                std::string_view alias          = "",
                std::string_view separator      = ",",
                std::string_view fieldSeparator = " ",
                bool             distinct       = false
        );

        // Overload with ORDER BY for multiple fields - requires explicit
        // specification
        template <auto OrderField, auto FirstField, auto... RestFields>
        QuerySet<T>& group_concat_order(
                std::string_view alias          = "",
                std::string_view separator      = ",",
                std::string_view fieldSeparator = " ",
                bool             distinct       = false
        );

        QuerySet<T>& limit(int limit_value);
        QuerySet<T>& offset(int offset_value);

        // Aggregate functions
        template <auto Field> QuerySet<T>& max(std::string_view alias = "");

        template <auto Field> QuerySet<T>& min(std::string_view alias = "");

        template <auto Field> QuerySet<T>& avg(std::string_view alias = "");

        template <auto Field> QuerySet<T>& count(std::string_view alias = "");

        template <auto Field> QuerySet<T>& sum(std::string_view alias = "");

        // Aggregate value methods that return direct values instead of QuerySet
        template <auto Field>
            requires std::is_member_object_pointer_v<decltype(Field)>
        constexpr std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> max_value() noexcept;

        template <auto Field>
            requires std::is_member_object_pointer_v<decltype(Field)>
        constexpr std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> min_value() noexcept;

        template <auto Field>
            requires std::is_member_object_pointer_v<decltype(Field)>
        constexpr std::expected<double, std::string> avg_value() noexcept;

        template <auto Field>
            requires std::is_member_object_pointer_v<decltype(Field)>
        constexpr std::expected<long long, std::string> count_value() noexcept;

        template <auto Field>
            requires std::is_member_object_pointer_v<decltype(Field)>
        constexpr std::expected<double, std::string> sum_value() noexcept;

        // FUNCTIONS API (declarations)
        template <typename... Args> QuerySet<T>& functions(Args&&... args);

        // JOIN API: append compile-time clause views and return self
        template <class U, auto MemberPtr> QuerySet<T>& join() {
            this->join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Inner>());
            return *this;
        }

        template <class U, auto MemberPtr> QuerySet<T>& left_join() {
            this->join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Left>());
            return *this;
        }

        // REMOVE API (declarations)
        std::expected<bool, std::string> remove(const T& obj);
        std::expected<bool, std::string> remove(const std::vector<T>& objs);
        std::expected<bool, std::string> remove();

        // UPDATE API (declarations)
        std::expected<bool, std::string>                                           update(T obj);
        std::expected<bool, std::string>                                           update(const T& obj);
        std::expected<bool, std::string>                                           update(std::vector<T> objs);
        std::expected<bool, std::string>                                           update(const std::vector<T>& objs);
        std::expected<bool, std::string>                                           update(std::span<const T> objects);
        template <auto MemberPtr, typename Value> std::expected<bool, std::string> update(Value&& value);

        // SELECT API (declarations)
        ExpectedVectorT<T>     select_all();
        ExpectedT<T>           select_one();
        ExpectedValueVectorMap select_values();

        // INSERT API (declarations)
        std::expected<int, std::string>              insert(T obj);
        std::expected<int, std::string>              insert(const T& obj);
        std::expected<std::vector<int>, std::string> insert(std::vector<T> objs);
        std::expected<std::vector<int>, std::string> insert(const std::vector<T>& objs);
        std::expected<std::vector<int>, std::string> insert(std::span<const T> objects);
        InsertStatement<T>                           stmt_insert(const T& obj);
        InsertStatement<T>                           stmt_insert(const std::vector<T>& objs);

      private:
        [[nodiscard]] std::expected<std::vector<int>, std::string>
        execute_insert(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return std::vector<int>{};
            return InsertStatement<T>(conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_update(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return false;
            return UpdateStatement<T>(conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_delete(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return false;
            return DeleteStatement<T>(conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_delete() noexcept {
            return DeleteStatement<T>(conn, std::move(_whereExpression)).execute();
        }

        template <typename FieldType, typename Value>
        [[nodiscard]] storm::Where create_condition(const FieldType& field_obj, Value&& value, storm::Op op) const {
            switch (op) {
            case storm::Op::EQ:
                return field_obj == std::forward<Value>(value);
            case storm::Op::NE:
                return field_obj != std::forward<Value>(value);
            case storm::Op::GT:
                return field_obj > std::forward<Value>(value);
            case storm::Op::LT:
                return field_obj < std::forward<Value>(value);
            case storm::Op::GE:
                return field_obj >= std::forward<Value>(value);
            case storm::Op::LE:
                return field_obj <= std::forward<Value>(value);
            case storm::Op::LIKE:
                return field_obj.like(std::forward<Value>(value));
            case storm::Op::IS:
                return field_obj.is(std::forward<Value>(value));
            default:
                return field_obj == std::forward<Value>(value);
            }
        }

        [[nodiscard]] std::string createDistinctClause() const {
            if (distinctFields.empty()) {
                return "";
            }

            return "DISTINCT ";
        }

        template <typename FieldType, typename Container>
        [[nodiscard]] storm::Where create_in_condition(const FieldType& field_obj, const Container& values) const {
            if (values.empty()) {
                // Return a condition that's always false for empty IN clause
                return storm::Where(std::make_unique<storm::Condition>("1", storm::Op::EQ, 0));
            }

            // For now, we'll create OR conditions for each value
            auto         it     = values.begin();
            storm::Where result = field_obj == *it;
            ++it;

            for (; it != values.end(); ++it) {
                result = result || (field_obj == *it);
            }

            return result;
        }

        template <typename U> [[nodiscard]] consteval std::string_view get_table_name() {
            return refl::reflect<U>::get_struct_name();
        }

        template <auto FirstField, auto... RestFields>
        std::pair<std::string, std::string>
        prepare_group_concat(std::string_view alias, std::string_view fieldSeparator) {
            // Validate all member pointers
            static_assert(std::is_member_pointer_v<decltype(FirstField)>, "FirstField must be a member pointer");
            (check_member_pointer<RestFields>(), ...);

            auto firstDesc = make_field_desc<FirstField>();

            // Generate alias
            std::string actual_alias =
                    alias.empty() ? std::format("group_concat_{}", firstDesc.field) : std::string(alias);

            // Build field expression
            std::string field_expr = build_field_expression<FirstField, RestFields...>(fieldSeparator);

            return {actual_alias, field_expr};
        }

        template <auto OrderField, auto FirstField, auto... RestFields>
        QuerySet<T>& group_concat_with_order_impl(
                std::string_view alias, std::string_view separator, std::string_view fieldSeparator, bool distinct
        );

        template <auto FirstField, auto... RestFields>
        std::string build_field_expression(std::string_view fieldSeparator) {
            auto        firstDesc  = make_field_desc<FirstField>();
            std::string field_expr = firstDesc.full_name();

            if constexpr (sizeof...(RestFields) == 0) {
                return field_expr;
            }

            (
                    [&field_expr, &fieldSeparator]<auto Field>() {
                        auto d     = make_field_desc<Field>();
                        field_expr = std::format("{}||'{}'||{}", field_expr, fieldSeparator, d.full_name());
                    }.template operator()<RestFields>(),
                    ...
            );
            return field_expr;
        }

        template <auto Field> consteval void check_member_pointer() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        }
        // Simplified aggregate query helper with C++23 improvements
        // Overload A: accept raw SQL for the aggregate function (e.g., "MAX(t.x) AS max_x")
        template <typename ReturnType>
        std::expected<ReturnType, std::string>
        execute_aggregate_query(std::string_view sql_function, std::string_view error_prefix);

        // Overload B: accept a Spec that exposes to_sql() -> std::string (e.g., AggregateSpec)
        template <typename ReturnType, typename Spec>
            requires requires(const Spec& s) { { s.to_sql() } -> std::convertible_to<std::string>; }
        std::expected<ReturnType, std::string>
        execute_aggregate_query(const Spec& spec, std::string_view error_prefix);
    };

    template <typename T> QuerySet<T>& QuerySet<T>::where(const storm::Where& where_clause) {
        if (this->_whereExpression) {
            // Combine with existing WHERE using AND
            this->_whereExpression = storm::Where{*this->_whereExpression && where_clause};
        } else {
            this->_whereExpression = where_clause;
        }
        return *this;
    }

    template <typename T> QuerySet<T>& QuerySet<T>::where(storm::Where&& where_clause) {
        if (this->_whereExpression) {
            this->_whereExpression = storm::Where{*this->_whereExpression && where_clause};
        } else {
            this->_whereExpression = std::move(where_clause);
        }
        return *this;
    }

    template <typename T>
    template <auto MemberPtr, typename Value>
    QuerySet<T>& QuerySet<T>::where(Value&& value, storm::Op op) {
        // Create a field object using the compile-time member pointer
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = create_condition(field_obj, std::forward<Value>(value), op);
        return where(std::move(condition));
    }

    template <typename T>
    template <auto MemberPtr, typename Container>
    QuerySet<T>& QuerySet<T>::where_in(const Container& values) {
        // Create a field object using the compile-time member pointer
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = create_in_condition(field_obj, values);
        return where(std::move(condition));
    }

    template <typename T>
    template <auto MemberPtr, typename Value>
    QuerySet<T>& QuerySet<T>::where_like(Value&& pattern) {
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = field_obj.like(std::forward<Value>(pattern));
        return where(std::move(condition));
    }

    template <typename T>
    template <auto MemberPtr, typename T1, typename T2>
    QuerySet<T>& QuerySet<T>::where_between(T1&& value1, T2&& value2) {
        // Create a field object using the compile-time member pointer
        auto field_obj = Field<MemberPtr>();
        // Use the between() method of the Field class
        storm::Where condition = field_obj.between(std::forward<T1>(value1), std::forward<T2>(value2));
        return where(std::move(condition));
    }

    template <typename T> template <auto MemberPtr> QuerySet<T>& QuerySet<T>::where_not_null() {
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = field_obj.is_not_null();
        return where(std::move(condition));
    }

    template <typename T> template <auto MemberPtr> QuerySet<T>& QuerySet<T>::where_is_null() {
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = field_obj.is_null();
        return where(std::move(condition));
    }

    // UPDATE implementation
    // 1. Single object - handles move
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(T obj) {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 2. Const ref - keeps user's original object
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(const T& obj) {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 3. Batch move - takes ownership of vector
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(std::vector<T> objs) {
        return execute_update(std::span<const T>{objs});
    }

    // 4. Batch const ref - keeps user's original vector
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(const std::vector<T>& objs) {
        return execute_update(std::span<const T>{objs});
    }

    // 5. Advanced flexibility - direct span
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(std::span<const T> objects) {
        return execute_update(objects);
    }

    // INSERT implementation
    // === MINIMAL NECESSARY OVERLOADS ===

    // 1. Single object - handles move
    template <typename T> std::expected<int, std::string> QuerySet<T>::insert(T obj) {
        return execute_insert(std::span<const T>{&obj, 1});
    }

    // 2. Const ref - keeps user's original object
    template <typename T> std::expected<int, std::string> QuerySet<T>::insert(const T& obj) {
        return execute_insert(std::span<const T>{&obj, 1});
    }

    // 3. Batch move - takes ownership of vector
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::vector<T> objs) {
        return execute_insert(std::span<const T>{objs});
    }

    // 4. Batch const ref - keeps user's original vector
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(const std::vector<T>& objs) {
        return execute_insert(std::span<const T>{objs});
    }

    // 5. Advanced flexibility - direct span
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::span<const T> objects) {
        return execute_insert(objects);
    }

    // Single object REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(const T& obj) {
        return execute_delete(std::span<const T>{&obj, 1});
    }

    // Batch REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(const std::vector<T>& objs) {
        return execute_delete(std::span<const T>{objs});
    }

    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove() {
        return execute_delete();
    }

    // DISTINCT implementation
    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::distinct() {
        distinctFields.reserve(distinctFields.size() + sizeof...(Fields));
        (distinctFields.emplace_back(Fields), ...);
        return *this;
    }

    template <typename T>
    template <auto... Fields>
    QuerySet<T>& QuerySet<T>::only(const std::optional<std::string>& alias) {
        onlyFields.reserve(onlyFields.size() + sizeof...(Fields));
        (onlyFields.emplace_back(Fields, alias), ...);
        return *this;
    }

    // GROUP BY implementation
    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::group_by() {
        groupByFields.reserve(groupByFields.size() + sizeof...(Fields));
        ((groupByFields.emplace_back(refl::FieldWrapper::create<Fields>())), ...);
        return *this;
    }

    // LIMIT/OFFSET implementation
    template <typename T> QuerySet<T>& QuerySet<T>::limit(int limit_value) {
        _limit = limit_value;
        return *this;
    }

    template <typename T> QuerySet<T>& QuerySet<T>::offset(int offset_value) {
        _offset = offset_value;
        return *this;
    }

    // Functions method implementation
    template <typename T> template <typename... Args> QuerySet<T>& QuerySet<T>::functions(Args&&... args) {
        // Reserve capacity
        this->functionsSet.reserve(functionsSet.size() + sizeof...(Args));

        // Process each function using fold expression
        (functionsSet.emplace_back(std::forward<Args>(args)), ...);
        return *this;
    }

    // ORDER BY implementations
    template <typename T> template <auto Field, Collation CollationType> QuerySet<T>& QuerySet<T>::order_by() {
        orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), true, CollationType);
        return *this;
    }

    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::order_by_multiple() {
        orderTerms.reserve(orderTerms.size() + sizeof...(Fields));
        ((orderTerms.emplace_back(refl::FieldWrapper::create<Fields>(), true, Collation::NONE)), ...);
        return *this;
    }

    // Modern compile-time version for multiple field-direction pairs
    template <typename T>
    template <auto Field, auto Direction, auto... Rest>
    QuerySet<T>& QuerySet<T>::order_by_mixed() {
        static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");
        static_assert(sizeof...(Rest) % 2 == 0, "Must provide field-direction pairs");
        orderTerms.reserve(orderTerms.size() + (sizeof...(Rest) / 2 + 1));
        orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), Direction, Collation::NONE);
        if constexpr (sizeof...(Rest) > 0) {
            order_by_mixed<Rest...>();
        }
        return *this;
    }

    // Modern compile-time version with collation support
    template <typename T>
    template <auto Field, auto Direction, auto Coll, auto... Rest>
    QuerySet<T>& QuerySet<T>::order_by_full() {
        static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");
        static_assert(std::is_same_v<decltype(Coll), Collation>, "Collation must be a Collation enum value");
        static_assert(
                sizeof...(Rest) % 3 == 0,
                "Must provide field-direction-collation triplets (field, bool, "
                "collation, ...)"
        );

        this->orderTerms.reserve(this->orderTerms.size() + (sizeof...(Rest) / 3 + 1));
        this->orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), Direction, Coll);

        if constexpr (sizeof...(Rest) > 0) {
            order_by_full<Rest...>();
        }

        return *this;
    }

    // GROUP_CONCAT_ORDER implementation
    template <typename T>
    template <auto OrderField, auto FirstField, auto... RestFields>
    QuerySet<T>& QuerySet<T>::group_concat_order(
            std::string_view alias, std::string_view separator, std::string_view fieldSeparator, bool distinct
    ) {
        return group_concat_with_order_impl<OrderField, FirstField, RestFields...>(
                alias, separator, fieldSeparator, distinct
        );
    }

    template <typename T>
    template <auto OrderField, auto FirstField, auto... RestFields>
    QuerySet<T>& QuerySet<T>::group_concat_with_order_impl(
            std::string_view alias, std::string_view separator, std::string_view fieldSeparator, bool distinct
    ) {
        static_assert(std::is_member_pointer_v<decltype(OrderField)>, "OrderField must be a member pointer");

        auto [actual_alias, field_expr] = prepare_group_concat<FirstField, RestFields...>(alias, fieldSeparator);

        auto orderDesc = make_field_desc<OrderField>();

        // Build GROUP_CONCAT function with ORDER BY
        std::string function_str = "GROUP_CONCAT(";

        if (distinct) {
            function_str += "DISTINCT ";
        }

        function_str += field_expr;
        function_str += std::format(" ORDER BY {}", orderDesc.full_name());
        function_str += std::format(", '{}' ) AS {}", separator, actual_alias);

        functions(AggregateSpec::custom_sql(function_str));
        return *this;
    }

    // AGGREGATE FUNCTIONS implementation
    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::max(std::string_view alias) {
        functionsSet.emplace_back(AggregateSpec::max<Field>(alias));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::min(std::string_view alias) {
        functionsSet.emplace_back(AggregateSpec::min<Field>(alias));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::avg(std::string_view alias) {
        functionsSet.emplace_back(AggregateSpec::avg<Field>(alias));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::count(std::string_view alias) {
        functionsSet.emplace_back(AggregateSpec::count<Field>(alias));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::sum(std::string_view alias) {
        functionsSet.emplace_back(AggregateSpec::sum<Field>(alias));
        return *this;
    }

    template <typename T>
    template <auto FirstField, auto... RestFields>
    QuerySet<T>& QuerySet<T>::group_concat(
            std::string_view alias, std::string_view separator, std::string_view fieldSeparator, bool distinct
    ) {
        // Note: group_concat_impl needs to be implemented - placeholder for now
        // TODO: Implement group_concat_impl method
        return *this;
    }

    // SELECT ONE implementation (returns single object)
    template <typename T> ExpectedT<T> QuerySet<T>::select_one() {
        return select_all()
            .and_then([](const auto& rows) -> ExpectedT<T> {
                if (rows.empty()) {
                    return std::unexpected("No results found for select_one query");
                }
                return rows[0];
            });
    }

    // SELECT ALL implementation
    template <typename T> ExpectedVectorT<T> QuerySet<T>::select_all() {
        // Construct SelectStatement directly: it builds ORDER/GROUP/FIELDS internally
        return SelectStatement<T>(
                       conn,
                       SelectOptions{
                               .joins           = std::move(join_clauses),
                               .distinct_fields = std::move(distinctFields),
                               .only_fields     = std::move(onlyFields),
                               .functions_set   = std::move(functionsSet),
                               .order_terms     = std::move(orderTerms),
                               .group_by_fields = std::move(groupByFields),
                               .limit           = _limit,
                               .offset          = _offset,
                               .where_clause    = std::move(_whereExpression),
                       }
        )
                .execute_objects();
    }

    // SELECT VALUES implementation (returns dictionary-like data)
    template <typename T> ExpectedValueVectorMap QuerySet<T>::select_values() {
        return SelectStatement<T>(
                       conn,
                       SelectOptions{
                               .joins           = std::move(join_clauses),
                               .distinct_fields = std::move(distinctFields),
                               .only_fields     = std::move(onlyFields),
                               .functions_set   = std::move(functionsSet),
                               .order_terms     = std::move(orderTerms),
                               .group_by_fields = std::move(groupByFields),
                               .limit           = _limit,
                               .offset          = _offset,
                               .where_clause    = std::move(_whereExpression),
                       }
        )
                .execute_values();
    }

    // C++23 optimized helper for executing aggregate queries
    template <typename T>
    template <typename ReturnType>
    std::expected<ReturnType, std::string>
    QuerySet<T>::execute_aggregate_query(std::string_view sql_function, std::string_view error_prefix) {
        if (sql_function.empty()) [[unlikely]] {
            return std::unexpected(std::format("{}: No aggregate function specified", error_prefix));
        }

        // Delegate to Spec-based overload using custom SQL wrapper
        return execute_aggregate_query<ReturnType>(AggregateSpec::custom_sql(sql_function), error_prefix);
    }

    template <typename T>
    template <typename ReturnType, typename Spec>
        requires requires(const Spec& s) { { s.to_sql() } -> std::convertible_to<std::string>; }
    std::expected<ReturnType, std::string>
    QuerySet<T>::execute_aggregate_query(const Spec& spec, std::string_view error_prefix) {
        static_assert(std::is_arithmetic_v<ReturnType> || std::same_as<ReturnType, std::string>,
                      "ReturnType must be arithmetic or string");

        // Normalize to AggregateSpec so we can feed SelectOptions
        AggregateSpec final_spec = [&]() -> AggregateSpec {
            if constexpr (std::is_same_v<std::decay_t<Spec>, AggregateSpec>) {
                return spec;
            } else {
                return AggregateSpec::custom_sql(spec.to_sql());
            }
        }();

        // Build SelectStatement with the aggregate function in functions_set
        SelectOptions opts{};
        opts.functions_set.emplace_back(std::move(final_spec));
        if (_whereExpression) {
            opts.where_clause = std::move(_whereExpression);
        }

        auto result = SelectStatement<T>(conn, std::move(opts)).execute_values();
        if (!result) [[unlikely]] {
            return std::unexpected(std::format("{}: {}", error_prefix, result.error()));
        }

        const auto& rows = result.value();
        if (rows.empty()) [[unlikely]] {
            return std::unexpected(std::format("{}: No results found", error_prefix));
        }

        const auto& first_row = rows[0];
        if (!first_row.empty()) [[likely]] {
            const auto& value = first_row.begin()->second;
            return std::visit(
                []<typename V>(const V& val) -> std::expected<ReturnType, std::string> {
                    if constexpr (std::same_as<ReturnType, V>) {
                        return val;
                    } else if constexpr (std::same_as<ReturnType, std::string> && std::convertible_to<V, std::string>) {
                        return std::format("{}", val);
                    } else if constexpr (std::is_arithmetic_v<ReturnType> && std::is_arithmetic_v<V>) {
                        return static_cast<ReturnType>(val);
                    } else {
                        return std::unexpected("Type conversion not supported");
                    }
                },
                value
            );
        }

        return std::unexpected(std::format("{}: Invalid result format", error_prefix));
    }

    // C++23 optimized aggregate functions with compile-time field validation
    template <typename T>
    template <auto Field>
        requires std::is_member_object_pointer_v<decltype(Field)>
    constexpr std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> 
    QuerySet<T>::max_value() noexcept {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(std::is_arithmetic_v<FieldType> || std::same_as<FieldType, std::string>, 
                     "MAX can only be used with arithmetic types or strings");

        return execute_aggregate_query<FieldType>(AggregateSpec::max<Field>(), "find maximum value");
    }

    template <typename T>
    template <auto Field>
        requires std::is_member_object_pointer_v<decltype(Field)>
    constexpr std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> 
    QuerySet<T>::min_value() noexcept {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(std::is_arithmetic_v<FieldType> || std::same_as<FieldType, std::string>, 
                     "MIN can only be used with arithmetic types or strings");

        return execute_aggregate_query<FieldType>(AggregateSpec::min<Field>(), "find minimum value");
    }

    template <typename T> 
    template <auto Field>
        requires std::is_member_object_pointer_v<decltype(Field)>
    constexpr std::expected<double, std::string> QuerySet<T>::avg_value() noexcept {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(std::is_arithmetic_v<FieldType>, "AVG can only be used with arithmetic types");

        return execute_aggregate_query<double>(AggregateSpec::avg<Field>(), "calculate average");
    }

    template <typename T> 
    template <auto Field>
        requires std::is_member_object_pointer_v<decltype(Field)>
    constexpr std::expected<long long, std::string> QuerySet<T>::count_value() noexcept {
        return execute_aggregate_query<long long>(AggregateSpec::count<Field>(), "count records");
    }

    template <typename T> 
    template <auto Field>
        requires std::is_member_object_pointer_v<decltype(Field)>
    constexpr std::expected<double, std::string> QuerySet<T>::sum_value() noexcept {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(std::is_arithmetic_v<FieldType>, "SUM can only be used with arithmetic types");

        return execute_aggregate_query<double>(AggregateSpec::sum<Field>(), "calculate sum");
    }

} // namespace storm
