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
import <flat_map>;

export namespace storm {
    // Use the canonical SqlValue type from storm.core_types to avoid redundancy
    using ValueMap                              = std::flat_map<std::string, SqlValue, std::less<>>;
    using ValueVectorMap                        = std::vector<ValueMap>;
    using ExpectedValueVectorMap                = std::expected<ValueVectorMap, std::string>;
    template <typename T> using ExpectedT       = std::expected<T, std::string>;
    template <typename T> using ExpectedVectorT = std::expected<std::vector<T>, std::string>;

    // Compile-time aggregate result type deduction
    template <AggregateKind K, typename FieldType> struct aggregate_result_type {
        using type = std::conditional_t<
                K == AggregateKind::Count,
                std::int64_t,
                std::conditional_t<
                        K == AggregateKind::Avg || K == AggregateKind::Sum,
                        double,
                        FieldType // Max/Min preserve field type
                        >>;
    };

    template <AggregateKind K, typename FieldType>
    using aggregate_result_t = typename aggregate_result_type<K, FieldType>::type;

    template <typename T, std::size_t N> class compile_time_sql {
        std::array<char, N> buffer{};
        std::size_t         pos = 0;

      public:
        consteval compile_time_sql() = default;

        consteval auto& append(std::string_view str) {
            for (auto c : str) {
                if (pos < N)
                    buffer[pos++] = c;
            }
            return *this;
        }

        consteval auto get() const {
            return std::string_view(buffer.data(), pos);
        }
    };

    template <typename T>
    concept NumericType = std::is_arithmetic_v<T>;

    template <AggregateKind K, typename FieldType> struct aggregate_result {
        using type = std::conditional_t<
                K == AggregateKind::Count,
                std::int64_t,
                std::conditional_t<
                        (K == AggregateKind::Avg || K == AggregateKind::Sum),
                        std::conditional_t<NumericType<FieldType>, double, FieldType>,
                        FieldType>>;
    };

    template <auto MemberPtr> using field_type_t = typename member_pointer_traits<decltype(MemberPtr)>::type;

    template <auto MemberPtr> using class_type_t = typename member_pointer_traits<decltype(MemberPtr)>::class_type;

    // Concepts for better error messages
    template <typename T>
    concept Aggregatable = requires {
        // Use aggregate_result_t in a dependent context to validate availability
        typename std::type_identity<aggregate_result_t<AggregateKind::Sum, T>>::type;
    };

    template <typename T>
    concept Sortable = requires(T a, T b) {
        { a < b } -> std::convertible_to<bool>;
    };

    // Simplified conversion with better compile-time optimization
    template <typename To, typename From>
    constexpr auto convert_value(const From& from) -> std::expected<To, std::string> {
        if constexpr (std::same_as<To, From>) {
            return from;
        } else if constexpr (std::convertible_to<From, To>) {
            return static_cast<To>(from);
        } else if constexpr (std::same_as<To, std::string>) {
            return std::format("{}", from);
        } else {
            return std::unexpected("Type conversion not supported");
        }
    }

    template <class T> class QuerySet {
      private:
        // Optimize memory layout with [[no_unique_address]]
        [[no_unique_address]] std::shared_ptr<Connection> conn;
        [[no_unique_address]] std::optional<storm::Where> _whereExpression;

        // Use small vector optimization for common cases
        using SmallFieldVector = std::vector<refl::FieldWrapper>; // Could use small_vector
        SmallFieldVector distinctFields;
        SmallFieldVector groupByFields;

        // Pack smaller members together
        struct {
            int  limit : 31;
            int  offset : 31;
            bool has_aggregates : 1;
            bool is_distinct : 1;
        } query_flags{};

        // Helper to estimate result size for pre-allocation
        [[nodiscard]] constexpr std::size_t estimate_result_size() const noexcept {
            if (query_flags.limit > 0)
                return query_flags.limit;
            if (query_flags.has_aggregates)
                return 1;
            return 100; // Default estimate
        }

        std::vector<JoinWrapper>                                               join_clauses;
        std::vector<std::tuple<refl::FieldWrapper, bool, Collation>>           orderTerms;
        std::vector<std::pair<refl::FieldWrapper, std::optional<std::string>>> onlyFields;
        std::vector<AggregateSpec>                                             functionsSet;

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
        // WHERE with multiple conditions using fold expressions
        template <typename Self, typename... Conditions>
        constexpr auto&& where_all(Self&& self, Conditions&&... conditions);

        template <typename Self, auto MemberPtr, typename Value>
        constexpr auto&& where(Self&& self, Value&& value, storm::Op op = storm::Op::EQ);

        template <auto MemberPtr, typename Container> QuerySet<T>& where_in(const Container& values);

        template <auto MemberPtr, typename Value> QuerySet<T>& where_like(Value&& pattern);

        template <auto MemberPtr, typename T1, typename T2> QuerySet<T>& where_between(T1&& value1, T2&& value2);

        template <auto MemberPtr> QuerySet<T>& where_not_null();

        template <auto MemberPtr> QuerySet<T>& where_is_null();

        // ORDER BY API (declarations)
        // Single field
        template <auto Field, Collation CollationType = Collation::NONE> QuerySet<T>& order_by();

        // Multiple fields
        template <typename Self, auto... Fields> QuerySet<T>& order_by_multiple(Self&& self);

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

        // In class declaration:
        template <typename Self, bool Distinct = false, auto... Fields>
        constexpr auto&& group_concat(
                Self&&                  self,
                utils::fixed_string<32> alias     = utils::fixed_string<32>(std::string_view("")),
                utils::fixed_string<8>  separator = utils::fixed_string<8>(std::string_view(","))
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
        template <auto Field> QuerySet<T>& max(std::string_view alias = "") noexcept {
            functionsSet.emplace_back(AggregateSpec::max<Field>(alias));
            return *this;
        }

        template <auto Field> QuerySet<T>& min(std::string_view alias = "") noexcept {
            functionsSet.emplace_back(AggregateSpec::min<Field>(alias));
            return *this;
        }

        template <auto Field> QuerySet<T>& avg(std::string_view alias = "") noexcept {
            functionsSet.emplace_back(AggregateSpec::avg<Field>(alias));
            return *this;
        }

        template <auto Field> QuerySet<T>& count(std::string_view alias = "") noexcept {
            functionsSet.emplace_back(AggregateSpec::count<Field>(alias));
            return *this;
        }

        template <auto Field> QuerySet<T>& sum(std::string_view alias = "") noexcept {
            functionsSet.emplace_back(AggregateSpec::sum<Field>(alias));
            return *this;
        }

        // Aggregate value methods that return direct values instead of QuerySet
        template <auto Field> [[nodiscard]] constexpr auto max_value() noexcept {
            return execute_aggregate<Field, AggregateKind::Max>();
        }

        template <auto Field> [[nodiscard]] constexpr auto min_value() noexcept {
            return execute_aggregate<Field, AggregateKind::Min>();
        }

        template <auto Field> [[nodiscard]] constexpr auto avg_value() noexcept {
            return execute_aggregate<Field, AggregateKind::Avg>();
        }

        template <auto Field> [[nodiscard]] constexpr auto count_value() noexcept {
            return execute_aggregate<Field, AggregateKind::Count>();
        }

        template <auto Field> [[nodiscard]] constexpr auto sum_value() noexcept {
            return execute_aggregate<Field, AggregateKind::Sum>();
        }

        // Alternative: Generic aggregate with kind as template parameter
        template <auto Field, AggregateKind Kind> [[nodiscard]] constexpr auto aggregate_value() noexcept {
            return execute_aggregate<Field, Kind>();
        }

        // For custom SQL aggregates (GROUP_CONCAT, etc)
        template <typename ReturnType>
        [[nodiscard]] auto execute_custom_aggregate(std::string_view sql, std::string_view error_context)
                -> std::expected<ReturnType, std::string> {
            SelectOptions opts{
                    .functions_set = {AggregateSpec::custom_sql(sql)},
                    .where_clause  = std::move(_whereExpression),
            };

            auto result = SelectStatement<T>(conn, std::move(opts)).execute_values();
            if (!result) [[unlikely]]
                return std::unexpected(std::format("{}: {}", error_context, result.error()));

            if (result->empty() || result->front().empty()) [[unlikely]]
                return std::unexpected(std::format("{}: No results", error_context));

            return std::visit(
                    [](const auto& val) { return convert_value<ReturnType>(val); }, result->front().begin()->second
            );
        }

        template <auto Field, AggregateKind Kind>
        [[nodiscard]] constexpr auto execute_aggregate() const
                -> std::expected<aggregate_result_t<Kind, field_type_t<Field>>, std::string> {
            using ResultType = aggregate_result_t<Kind, field_type_t<Field>>;

            // Compile-time validation
            if constexpr (Kind == AggregateKind::Avg || Kind == AggregateKind::Sum) {
                static_assert(NumericType<field_type_t<Field>>, "AVG/SUM require numeric fields");
            }

            // Build spec at compile time with static string
            constexpr auto spec = []() {
                if constexpr (Kind == AggregateKind::Max)
                    return AggregateSpec::template max<Field>();
                else if constexpr (Kind == AggregateKind::Min)
                    return AggregateSpec::template min<Field>();
                else if constexpr (Kind == AggregateKind::Avg)
                    return AggregateSpec::template avg<Field>();
                else if constexpr (Kind == AggregateKind::Count)
                    return AggregateSpec::template count<Field>();
                else
                    return AggregateSpec::template sum<Field>();
            }();

            SelectOptions opts{
                    .functions_set = {spec},
                    .where_clause  = _whereExpression,
            };

            auto result = SelectStatement<T>(conn, std::move(opts)).execute_values();
            if (!result) [[unlikely]]
                return std::unexpected(result.error());

            if (result->empty()) [[unlikely]]
                return std::unexpected("No results");

            // Use ranges for cleaner access
            auto& first_row = result->front();
            if (auto it = std::ranges::begin(first_row); it != std::ranges::end(first_row)) {
                return std::visit(
                        []<typename V>(const V& val) -> std::expected<ResultType, std::string> {
                            if constexpr (std::convertible_to<V, ResultType>) {
                                return static_cast<ResultType>(val);
                            } else {
                                return std::unexpected("Type conversion failed");
                            }
                        },
                        it->second
                );
            }

            return std::unexpected("No data in result");
        }

        // FUNCTIONS API (declarations)
        template <typename... Args> QuerySet<T>& functions(Args&&... args);

        // JOIN API: append compile-time clause views and return self
        template <class U, auto MemberPtr> QuerySet<T>& join() {
            this->join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Inner>());
            return *this;
        }

        // Compile-time JOIN validation
        template <typename Self, class U, auto MemberPtr>
            requires requires {
                typename member_pointer_traits<decltype(MemberPtr)>::class_type;
                requires std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            }
        constexpr auto&& join(this Self&& self) {
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Inner>());
            return std::forward<Self>(self);
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
        // Generic contiguous range overload (e.g., vector, array, span)
        template <std::ranges::contiguous_range R>
            requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T>
        std::expected<std::vector<int>, std::string> insert(R&& objects);
        std::expected<std::vector<int>, std::string> insert(std::span<const T> objects);
        InsertStatement<T>                           stmt_insert(const T& obj);
        InsertStatement<T>                           stmt_insert(const std::vector<T>& objs);

      private:
        // Deducing this for perfect forwarding
        template <typename Self> static constexpr decltype(auto) self_cast(Self&& self) {
            return std::forward<Self>(self);
        }

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
    };

    template <typename T>
    template <typename Self, typename... Conditions>
    constexpr auto&& QuerySet<T>::where_all(Self&& self, Conditions&&... conditions) {
        storm::Where combined = (... && conditions);
        self._whereExpression =
                self._whereExpression ? storm::Where{*self._whereExpression && combined} : std::move(combined);
        return std::forward<Self>(self);
    }

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
    template <typename Self, auto MemberPtr, typename Value>
    constexpr auto&& QuerySet<T>::where(Self&& self, Value&& value, storm::Op op) {
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = self.create_condition(field_obj, std::forward<Value>(value), op);
        self._whereExpression =
                self._whereExpression.has_value() ? (self._whereExpression.value() && condition) : std::move(condition);
        return std::forward<Self>(self);
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

    // 6. Generic contiguous range - forwards to span<const T>
    template <typename T>
    template <std::ranges::contiguous_range R>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T>
    std::expected<std::vector<int>, std::string> QuerySet<T>::insert(R&& objects) {
        if (std::ranges::empty(objects)) {
            return std::vector<int>{};
        }
        return execute_insert(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
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

    template <typename T>
    template <typename Self, auto... Fields>
    QuerySet<T>& QuerySet<T>::order_by_multiple(Self&& self) {
        self.orderTerms.reserve(self.orderTerms.size() + sizeof...(Fields));
        [self]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((self.orderTerms.emplace_back(refl::FieldWrapper::create<Fields>(), true, Collation::NONE)), ...);
        }(std::make_index_sequence<sizeof...(Fields)>{});
        return std::forward<Self>(self);
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

    template <typename T>
    template <typename Self, bool Distinct, auto... Fields>
    constexpr auto&&
    QuerySet<T>::group_concat(Self&& self, utils::fixed_string<32> alias, utils::fixed_string<8> separator) {
        // Build complete SQL at compile time
        constexpr auto sql = []() {
            constexpr auto fields = []() {
                if constexpr (sizeof...(Fields) == 0) {
                    return utils::make_fixed_string("");
                }
                return utils::join(Fields..., ", ");
            }();

            if constexpr (Distinct) {
                return utils::concat_ct("GROUP_CONCAT(DISTINCT ", fields, ")");
            }
            return utils::concat_ct("GROUP_CONCAT(", fields, ")");
        }();

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql.view()));
        return std::forward<Self>(self);
    }

    // SELECT ONE implementation (returns single object)
    template <typename T> ExpectedT<T> QuerySet<T>::select_one() {
        return select_all().and_then([](const auto& rows) -> ExpectedT<T> {
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
    // Optimized value extraction with structured bindings
    template <typename T> [[nodiscard]] auto QuerySet<T>::select_values() -> ExpectedValueVectorMap {
        // Pre-calculate capacity hints
        const auto expected_size = estimate_result_size();

        SelectOptions opts{
                .joins           = std::move(join_clauses),
                .distinct_fields = std::move(distinctFields),
                .only_fields     = std::move(onlyFields),
                .functions_set   = std::move(functionsSet),
                .order_terms     = std::move(orderTerms),
                .group_by_fields = std::move(groupByFields),
                .limit           = _limit,
                .offset          = _offset,
                .where_clause    = std::move(_whereExpression),
        };

        auto stmt = SelectStatement<T>(conn, std::move(opts));
        stmt.reserve_hint(expected_size);
        return stmt.execute_values();
    }

} // namespace storm
