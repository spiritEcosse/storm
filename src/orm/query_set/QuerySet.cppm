module;

// Module global fragment - third-party C headers (macros not exported by
// modules)
#include <sqlite3.h>

// Define the module
export module storm.query_set;

// Import required modules
import storm.core_types; // For SqlValue, Op, Collation
import storm.condition;  // For storm::Condition
import storm.function;
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
import storm.field_desc;
import storm.join_utils;
import storm.type_traits;

// Import standard header units in the global module fragment
import <string>;
import <utility>;
import <memory>;
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

export namespace storm {
    // Use the canonical SqlValue type from storm.core_types to avoid redundancy
    using ValueMap                              = std::map<std::string, SqlValue, std::less<>>;
    using ValueVectorMap                        = std::vector<ValueMap>;
    using ExpectedValueVectorMap                = std::expected<ValueVectorMap, std::string>;
    template <typename T> using ExpectedT       = std::expected<T, std::string>;
    template <typename T> using ExpectedVectorT = std::expected<std::vector<T>, std::string>;

    template <class T> class QuerySet {
      private:
        std::shared_ptr<Connection>   conn;
        std::optional<storm::Where>   _whereExpression;
        std::vector<std::string_view> join_clauses;
        std::vector<OrderTerm>        orderTerms;
        std::vector<FieldDescView>    distinctFields;
        std::vector<FieldDescView>    onlyFields;
        std::vector<Function>         functionsSet;
        std::vector<FieldDescView>    groupByFields;

        int _limit{};
        int _offset{};

        // Helper method to convert compile-time join info to runtime string
        template <typename JoinInfo> std::string join_info_to_string(const JoinInfo& info) {
            return JoinInfo::to_string();
        }

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
        template <auto Field> QuerySet<T>& order_by(Collation collation = Collation::NONE);

        // Modern compile-time version for multiple field-direction pairs
        template <auto Field, auto Direction, auto... Rest> QuerySet<T>& order_by();

        // Modern compile-time version with collation support
        template <auto Field, auto Direction, auto Coll, auto... Rest> QuerySet<T>& order_by_collate();

        // DISTINCT API (declarations)
        template <auto... Fields> QuerySet<T>& distinct();

        // ONLY API (declarations)
        template <auto... Fields> QuerySet<T>& only(const std::string& alias = "");

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
        std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> max_value();

        template <auto Field>
        std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> min_value();

        template <auto Field> std::expected<double, std::string> avg_value();

        template <auto Field> std::expected<int, std::string> count_value();

        template <auto Field> std::expected<double, std::string> sum_value();

        // FUNCTIONS API (declarations)
        template <typename... Args> QuerySet<T>& functions(Args&&... args);

        // JOIN API: append compile-time clause views and return self
        template <class U, auto MemberPtr> QuerySet<T>& join() {
            constexpr auto clause = decltype(make_join_clause<T, U, MemberPtr, JoinType::Inner>())::view();
            this->join_clauses.emplace_back(clause);
            return *this;
        }

        template <class U, auto MemberPtr> QuerySet<T>& left_join() {
            constexpr auto clause = decltype(make_join_clause<T, U, MemberPtr, JoinType::Left>())::view();
            this->join_clauses.emplace_back(clause);
            return *this;
        }

        // REMOVE API (declarations)
        std::expected<bool, std::string> remove(const T& obj);
        std::expected<bool, std::string> remove(const std::vector<T>& objs);

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

        // Helper to add an order field with direction and collation
        template <auto Field> QuerySet<T>& add_order_field(bool ascending, Collation collation) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");

            // Compile-time constructor - eliminates runtime string copies
            this->orderTerms.emplace_back(CtField<Field>::view(), ascending, collation);

            return *this;
        }

        // Helper to process field-direction pairs recursively
        template <auto Field, auto Direction, auto... Rest> void processOrderByPairs() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");

            // Add the current field-direction pair using the helper
            add_order_field<Field>(Direction, Collation::NONE);

            // Process remaining pairs if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByPairs<Rest...>();
            }
        }

        // Helper to process field-direction-collation triplets recursively
        template <auto Field, auto Direction, auto Coll, auto... Rest> void processOrderByCollationPairs() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");
            static_assert(std::is_same_v<decltype(Coll), Collation>, "Collation must be a Collation enum value");

            // Add the current field-direction-collation triplet as value term
            auto d = make_field_desc<Field>();
            this->orderTerms.emplace_back(d, Direction, Coll);

            // Process remaining triplets if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByCollationPairs<Rest...>();
            }
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
        // Common helper for executing aggregate queries
        template <typename ReturnType, typename SetupFunction, typename ValueExtractor = std::nullptr_t>
        std::expected<ReturnType, std::string> execute_aggregate_query(
                SetupFunction setup_function, std::string_view error_prefix, ValueExtractor value_extractor = nullptr
        );
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

    // DISTINCT implementation
    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::distinct() {
        static_assert((std::is_member_pointer_v<decltype(Fields)> && ...), "All fields must be member pointers");

        // Reserve capacity
        this->distinctFields.reserve(sizeof...(Fields));

        // Process each field using CtField for compile-time optimization
        auto addField = [this]<auto Field>() {
            using CtFieldType = CtField<Field>;
            this->distinctFields.emplace_back(CtFieldType::view()); // Direct view (no copies!)
        };

        (addField.template operator()<Fields>(), ...);

        return *this;
    }

    // ONLY (Field Selection) implementation
    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::only(const std::string& alias) {
        // Note: static_assert removed due to C++23 modules type_traits visibility
        // issues

        // Reserve capacity
        onlyFields.reserve(sizeof...(Fields));

        // Process each field using CtField for compile-time optimization
        auto addField = [this, &alias]<auto MemberPtr>() {
            using CtFieldType = CtField<MemberPtr>;
            // For only(), we need to override the alias from CtField with the runtime alias
            auto view = CtFieldType::view();
            // Create a new FieldDescView with the runtime alias
            onlyFields.emplace_back(view.table, view.field, alias); // Direct view (no copies!)
        };

        (addField.template operator()<Fields>(), ...);

        return *this;
    }

    // GROUP BY implementation
    template <typename T> template <auto... Fields> QuerySet<T>& QuerySet<T>::group_by() {
        // Reserve capacity
        groupByFields.reserve(sizeof...(Fields));

        // Process each field using CtField for compile-time optimization
        auto addField = [this]<auto MemberPtr>() {
            using CtFieldType = CtField<MemberPtr>;
            groupByFields.emplace_back(CtFieldType::view()); // Direct view (no copies!)
        };

        (addField.template operator()<Fields>(), ...);

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
    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::order_by(Collation collation) {
        return add_order_field<Field>(true, collation);
    }

    // Modern compile-time version for multiple field-direction pairs
    template <typename T> template <auto Field, auto Direction, auto... Rest> QuerySet<T>& QuerySet<T>::order_by() {
        // Ensure we have valid field-direction pairs
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");
        static_assert(sizeof...(Rest) % 2 == 0, "Must provide field-direction pairs (field, bool, field, bool, ...)");

        // Reserve capacity for all pairs
        this->orderTerms.reserve(this->orderTerms.size() + (sizeof...(Rest) / 2 + 1));

        // Add the first field-direction pair
        {
            // Compile-time constructor - eliminates runtime string copies
            this->orderTerms.emplace_back(CtField<Field>::view(), Direction, Collation::NONE);
        }

        // Process remaining pairs if any
        if constexpr (sizeof...(Rest) > 0) {
            processOrderByPairs<Rest...>();
        }

        return *this;
    }

    // Modern compile-time version with collation support
    template <typename T>
    template <auto Field, auto Direction, auto Coll, auto... Rest>
    QuerySet<T>& QuerySet<T>::order_by_collate() {
        // Ensure we have valid field-direction-collation triplets
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        static_assert(std::is_same_v<decltype(Direction), bool>, "Direction must be a boolean value");
        static_assert(std::is_same_v<decltype(Coll), Collation>, "Collation must be a Collation enum value");
        static_assert(
                sizeof...(Rest) % 3 == 0,
                "Must provide field-direction-collation triplets (field, bool, "
                "collation, ...)"
        );

        // Reserve capacity for all triplets
        this->orderTerms.reserve(this->orderTerms.size() + (sizeof...(Rest) / 3 + 1));

        // Add the first field-direction-collation triplet
        {
            auto d = make_field_desc<Field>();
            this->orderTerms.emplace_back(d, Direction, Coll);
        }

        // Process remaining triplets if any
        if constexpr (sizeof...(Rest) > 0) {
            processOrderByCollationPairs<Rest...>();
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

        functions(Function(function_str));
        return *this;
    }

    // AGGREGATE FUNCTIONS implementation
    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::max(std::string_view alias) {
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        auto        desc = make_field_desc<Field>();
        std::string actual_alias(alias);
        if (actual_alias.empty()) {
            actual_alias = std::format("max_{}", desc.field);
        }
        // We want to keep any existing onlyFields to allow selecting both fields and
        // aggregate functions
        functionsSet.emplace_back(Function(std::format("MAX({}) AS {}", desc.full_name(), actual_alias)));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::min(std::string_view alias) {
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        auto        desc = make_field_desc<Field>();
        std::string actual_alias(alias);
        if (actual_alias.empty()) {
            actual_alias = std::format("min_{}", desc.field);
        }
        // We want to keep any existing onlyFields to allow selecting both fields and
        // aggregate functions
        functionsSet.emplace_back(Function(std::format("MIN({}) AS {}", desc.full_name(), actual_alias)));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::avg(std::string_view alias) {
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        // Only numeric fields should be used with AVG
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(
                std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                "AVG can only be used with numeric fields"
        );
        auto        desc = make_field_desc<Field>();
        std::string actual_alias(alias);
        if (actual_alias.empty()) {
            actual_alias = std::format("avg_{}", desc.field);
        }
        // We want to keep any existing onlyFields to allow selecting both fields and
        // aggregate functions
        functionsSet.emplace_back(Function(std::format("AVG({}) AS {}", desc.full_name(), actual_alias)));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::count(std::string_view alias) {
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        auto        desc = make_field_desc<Field>();
        std::string actual_alias(alias);
        if (actual_alias.empty()) {
            actual_alias = std::format("count_{}", desc.field);
        }
        // We want to keep any existing onlyFields to allow selecting both fields and
        // aggregate functions
        functionsSet.emplace_back(Function(std::format("COUNT({}) AS {}", desc.full_name(), actual_alias)));
        return *this;
    }

    template <typename T> template <auto Field> QuerySet<T>& QuerySet<T>::sum(std::string_view alias) {
        static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        // Only numeric fields should be used with SUM
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;
        static_assert(
                std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>,
                "SUM can only be used with numeric fields"
        );
        auto        desc = make_field_desc<Field>();
        std::string actual_alias(alias);
        if (actual_alias.empty()) {
            actual_alias = std::format("sum_{}", desc.field);
        }
        functionsSet.emplace_back(Function(std::format("SUM({}) AS {}", desc.full_name(), actual_alias)));
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
        auto rows = select_all();
        if (!rows)
            return std::unexpected(rows.error());

        if (rows->empty()) {
            return std::unexpected("No results found for select_one query");
        }

        // Return the first object from the results
        return (*rows)[0];
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

    // Common helper for executing aggregate queries
    template <typename T>
    template <typename ReturnType, typename SetupFunction, typename ValueExtractor>
    std::expected<ReturnType, std::string> QuerySet<T>::execute_aggregate_query(
            SetupFunction setup_function, std::string_view error_prefix, ValueExtractor value_extractor
    ) {
        // // Create temporary QuerySet and clear existing state
        // auto tempQuerySet = *this;
        // tempQuerySet.onlyFields.clear();
        // tempQuerySet.distinctFields.clear();
        // tempQuerySet.functionsSet.clear();

        // // Apply the specific aggregate function
        // setup_function(tempQuerySet);

        // // Build and execute query - simplified version for now
        //     auto fieldsClause = tempQuerySet.buildFieldsClause();
        //     std::string sql = "SELECT " + fieldsClause + " FROM " +
        //     get_table_name<T>();

        //     // Add WHERE clause if present
        //     if (_whereExpression) {
        //         auto query_result = _whereExpression->to_query();
        //         sql += " WHERE " + query_result.sql;
        //     }

        //     // Create statement using Storm ORM API
        //     Statement stmt(conn, sql);

        //     // Bind WHERE parameters if present
        //     if (_whereExpression) {
        //         auto query_result = _whereExpression->to_query();
        //         // Bind parameters from the query result
        //         for (const auto& [param_name, param_value] :
        //         query_result.parameters()) {
        //             int param_index = stmt.get_parameter_index(param_name);
        //             if (param_index > 0) {
        //                 // Use switch on variant index to avoid std::visit
        //                 compilation issues switch (param_value.index()) {
        //                     case 0: // std::string
        //                         stmt.bind(param_index,
        //                         std::get<std::string>(param_value)); break;
        //                     case 1: // int
        //                         stmt.bind(param_index, std::get<int>(param_value));
        //                         break;
        //                     case 2: // long
        //                         stmt.bind(param_index, static_cast<long
        //                         long>(std::get<long>(param_value))); break;
        //                     case 3: // long long
        //                         stmt.bind(param_index, std::get<long
        //                         long>(param_value)); break;
        //                     case 4: // float
        //                         stmt.bind(param_index,
        //                         static_cast<double>(std::get<float>(param_value)));
        //                         break;
        //                     case 5: // double
        //                         stmt.bind(param_index,
        //                         std::get<double>(param_value)); break;
        //                     case 6: // bool
        //                         stmt.bind(param_index,
        //                         static_cast<int>(std::get<bool>(param_value)));
        //                         break;
        //                     case 7: // std::nullopt_t
        //                         stmt.bind_null(param_index);
        //                         break;
        //                 }
        //             }
        //         }
        //     }

        //     // Execute query and fetch results
        //     auto result = stmt.execute();
        //     if (!result) {
        //         return std::unexpected("Failed to execute aggregate query: " +
        //         result.error());
        //     }

        //     // For now, return a placeholder value based on ReturnType
        //     if constexpr (std::is_same_v<ReturnType, std::string>) {
        //         return std::string{};
        //     } else if constexpr (std::is_same_v<ReturnType, int>) {
        //         return 0;
        //     } else if constexpr (std::is_same_v<ReturnType, double>) {
        //         return 0.0;
        //     } else {
        //         return ReturnType{};
        //     }
    }

    // MAX aggregate function that returns the direct value instead of a QuerySet
    template <typename T>
    template <auto Field>
    std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> QuerySet<T>::max_value() {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;

        return execute_aggregate_query<FieldType>([](auto& qs) { qs.template max<Field>(); }, "find maximum value");
    }

    // MIN aggregate function that returns the direct value instead of a QuerySet
    template <typename T>
    template <auto Field>
    std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> QuerySet<T>::min_value() {
        using FieldType = typename member_pointer_traits<decltype(Field)>::type;

        return execute_aggregate_query<FieldType>([](auto& qs) { qs.template min<Field>(); }, "find minimum value");
    }

    // AVG aggregate function that returns the direct value instead of a QuerySet
    template <typename T> template <auto Field> std::expected<double, std::string> QuerySet<T>::avg_value() {
        return execute_aggregate_query<double>([](auto& qs) { qs.template avg<Field>(); }, "average");
    }

    // COUNT aggregate function that returns the direct value instead of a QuerySet
    template <typename T> template <auto Field> std::expected<int, std::string> QuerySet<T>::count_value() {
        return execute_aggregate_query<int>([](auto& qs) { qs.template count<Field>(); }, "count");
    }

    // SUM aggregate function that returns the direct value instead of a QuerySet
    template <typename T> template <auto Field> std::expected<double, std::string> QuerySet<T>::sum_value() {
        return execute_aggregate_query<double>([](auto& qs) { qs.template sum<Field>(); }, "sum");
    }

} // namespace storm
