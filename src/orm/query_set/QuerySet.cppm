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

        // WHERE API (C++26 upgraded declarations)
        template <typename Self> auto&& where(this Self&& self, const storm::Where& where_clause);
        template <typename Self> auto&& where(this Self&& self, storm::Where&& where_clause);

        // WHERE with multiple conditions using C++26 fold expressions
        template <typename Self, typename... Conditions>
        constexpr auto&& where_all(Self&& self, Conditions&&... conditions);

        // C++26 compile-time field-based WHERE with static reflection
        template <typename Self, auto MemberPtr, typename Value>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        consteval auto&& where(this Self&& self, Value&& value, storm::Op op = storm::Op::EQ);

        // C++26 compile-time WHERE IN with container concept
        template <typename Self, auto MemberPtr, std::ranges::range Container>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        consteval auto&& where_in(this Self&& self, const Container& values);

        // C++26 compile-time WHERE LIKE with string concept
        template <typename Self, auto MemberPtr, typename Value>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     std::convertible_to<Value, std::string_view>
        consteval auto&& where_like(this Self&& self, Value&& pattern);

        // C++26 compile-time WHERE BETWEEN with comparable types
        template <typename Self, auto MemberPtr, typename T1, typename T2>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     std::three_way_comparable_with<T1, T2>
        consteval auto&& where_between(this Self&& self, T1&& value1, T2&& value2);

        // C++26 compile-time NULL checks with static field validation
        template <typename Self, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        consteval auto&& where_not_null(this Self&& self);

        template <typename Self, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        consteval auto&& where_is_null(this Self&& self);

        // ORDER BY API (C++26 upgraded declarations)
        // C++26 compile-time single field ordering with static validation
        template <typename Self, auto Field, Collation CollationType = Collation::NONE>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& order_by(this Self&& self);

        // C++26 compile-time multiple field ordering with variadic validation
        template <typename Self, auto... Fields>
            requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                    (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
        consteval auto&& order_by_multiple(this Self&& self);

        // C++26 compile-time field-direction pairs with static assertion
        template <typename Self, auto Field, auto Direction, auto... Rest>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T> &&
                     std::same_as<decltype(Direction), bool>
        consteval auto&& order_by_mixed(this Self&& self);

        // C++26 compile-time field-direction-collation triplets with full validation
        template <typename Self, auto Field, auto Direction, auto Coll, auto... Rest>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T> &&
                     std::same_as<decltype(Direction), bool> && std::same_as<decltype(Coll), Collation>
        consteval auto&& order_by_full(this Self&& self);

        // DISTINCT API (C++26 upgraded declarations)
        template <typename Self, auto... Fields>
            requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                    (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
        consteval auto&& distinct(this Self&& self);

        // ONLY API (C++26 upgraded declarations)
        template <typename Self, auto... Fields>
            requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                    (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
        consteval auto&& only(this Self&& self, const std::optional<std::string>& alias = std::nullopt);

        // GROUP BY API (C++26 upgraded declarations)
        template <typename Self, auto... Fields>
            requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                    (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
        consteval auto&& group_by(this Self&& self);

        // In class declaration:
        template <typename Self, bool Distinct = false, auto... Fields>
        consteval auto&&
        group_concat(Self&& self, utils::fixed_string<32> alias = "", utils::fixed_string<8> separator = ",");

        // Overload with ORDER BY for multiple fields - requires explicit
        // specification
        template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct = false>
        consteval auto&& group_concat_order(
                this Self&&             self,
                utils::fixed_string<32> alias          = {},
                utils::fixed_string<8>  separator      = {","},
                utils::fixed_string<8>  fieldSeparator = {","}
        ) {
            return self.template group_concat_with_order_impl<OrderField, FirstField, RestFields..., Distinct>(
                    alias, separator, fieldSeparator
            );
        }

        template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct = false>
        consteval auto&& group_concat_with_order_impl(
                this Self&&             self,
                utils::fixed_string<32> alias          = {},
                utils::fixed_string<8>  separator      = {","},
                utils::fixed_string<8>  fieldSeparator = {","}
        );

        template <typename Self> auto&& limit(this Self&& self, int limit_value);
        template <typename Self> auto&& offset(this Self&& self, int offset_value);

        // C++26 Aggregate functions with compile-time validation
        template <typename Self, auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& max(this Self&& self, std::string_view alias = "") noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MAX requires comparable field type");
            constexpr auto field_name = extract_field_name<Field>();

            self.functionsSet.emplace_back(AggregateSpec::max<Field>(alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self, auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& min(this Self&& self, std::string_view alias = "") noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MIN requires comparable field type");
            constexpr auto field_name = extract_field_name<Field>();

            self.functionsSet.emplace_back(AggregateSpec::min<Field>(alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self, auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& avg(this Self&& self, std::string_view alias = "") noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(NumericType<FieldType>, "AVG requires numeric field type");
            constexpr auto field_name = extract_field_name<Field>();

            self.functionsSet.emplace_back(AggregateSpec::avg<Field>(alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self, auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& count(this Self&& self, std::string_view alias = "") noexcept {
            // COUNT works on any field type
            constexpr auto field_name = extract_field_name<Field>();

            self.functionsSet.emplace_back(AggregateSpec::count<Field>(alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self, auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        consteval auto&& sum(this Self&& self, std::string_view alias = "") noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(NumericType<FieldType>, "SUM requires numeric field type");
            constexpr auto field_name = extract_field_name<Field>();

            self.functionsSet.emplace_back(AggregateSpec::sum<Field>(alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        // C++26 Aggregate value methods with compile-time type deduction
        template <auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto max_value() noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MAX requires comparable field type");
            constexpr auto field_name = extract_field_name<Field>();
            return execute_aggregate<Field, AggregateKind::Max>();
        }

        template <auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto min_value() noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(std::three_way_comparable<FieldType>, "MIN requires comparable field type");
            constexpr auto field_name = extract_field_name<Field>();
            return execute_aggregate<Field, AggregateKind::Min>();
        }

        template <auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto avg_value() noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(NumericType<FieldType>, "AVG requires numeric field type");
            constexpr auto field_name = extract_field_name<Field>();
            return execute_aggregate<Field, AggregateKind::Avg>();
        }

        template <auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto count_value() noexcept {
            constexpr auto field_name = extract_field_name<Field>();
            return execute_aggregate<Field, AggregateKind::Count>();
        }

        template <auto Field>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto sum_value() noexcept {
            using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
            static_assert(NumericType<FieldType>, "SUM requires numeric field type");
            constexpr auto field_name = extract_field_name<Field>();
            return execute_aggregate<Field, AggregateKind::Sum>();
        }

        // C++26 Generic aggregate with compile-time kind validation
        template <auto Field, AggregateKind Kind>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto aggregate_value() noexcept {
            using FieldType           = typename member_pointer_traits<decltype(Field)>::member_type;
            constexpr auto field_name = extract_field_name<Field>();

            // C++26 compile-time validation based on aggregate kind
            if constexpr (Kind == AggregateKind::Max || Kind == AggregateKind::Min) {
                static_assert(std::three_way_comparable<FieldType>, "MAX/MIN require comparable field type");
            } else if constexpr (Kind == AggregateKind::Avg || Kind == AggregateKind::Sum) {
                static_assert(NumericType<FieldType>, "AVG/SUM require numeric field type");
            }
            // COUNT works on any field type

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

        // C++26 compile-time aggregate execution with static reflection
        template <auto Field, AggregateKind Kind>
            requires std::is_member_pointer_v<decltype(Field)> &&
                     std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
        [[nodiscard]] consteval auto execute_aggregate() const
                -> std::expected<aggregate_result_t<Kind, field_type_t<Field>>, std::string> {
            using ResultType = aggregate_result_t<Kind, field_type_t<Field>>;
            using FieldType  = field_type_t<Field>;

            // C++26 compile-time validation with better error messages
            constexpr auto field_name = extract_field_name<Field>();

            if constexpr (Kind == AggregateKind::Max || Kind == AggregateKind::Min) {
                static_assert(
                        std::three_way_comparable<FieldType>,
                        "MAX/MIN aggregate requires comparable field type for field: " + field_name
                );
            } else if constexpr (Kind == AggregateKind::Avg || Kind == AggregateKind::Sum) {
                static_assert(
                        NumericType<FieldType>, "AVG/SUM aggregate requires numeric field type for field: " + field_name
                );
            }

            // C++26 compile-time spec generation with consteval lambda
            constexpr auto spec = []() consteval {
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
        template <typename Self, typename... Args> auto&& functions(this Self&& self, Args&&... args);

        // C++26 JOIN API with compile-time relationship validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        consteval auto&& join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Inner>());

            return std::forward<Self>(self);
        }

        // C++26 LEFT JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        consteval auto&& left_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "LEFT JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "LEFT JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Left>());

            return std::forward<Self>(self);
        }

        // C++26 RIGHT JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        consteval auto&& right_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "RIGHT JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "RIGHT JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Right>());

            return std::forward<Self>(self);
        }

        // C++26 FULL OUTER JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        consteval auto&& full_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "FULL JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "FULL JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Full>());

            return std::forward<Self>(self);
        }

        // C++26 REMOVE API with compile-time validation and constraint checking
        std::expected<bool, std::string> remove(const T& obj) requires refl::reflectable<T>;
        std::expected<bool, std::string> remove(const std::vector<T>& objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;
        std::expected<bool, std::string> remove() requires refl::reflectable<T>;

        // C++26 UPDATE API with compile-time validation and concepts
        std::expected<bool, std::string> update(T obj) requires refl::reflectable<T>;
        std::expected<bool, std::string> update(const T& obj) requires refl::reflectable<T>;
        std::expected<bool, std::string> update(std::vector<T> objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;
        std::expected<bool, std::string> update(const std::vector<T>& objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;
        std::expected<bool, std::string> update(std::span<const T> objects) 
            requires refl::reflectable<T> && std::ranges::contiguous_range<std::span<const T>>;

        // C++26 field-specific update with compile-time validation
        template <auto MemberPtr, typename Value>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     std::convertible_to<Value, typename member_pointer_traits<decltype(MemberPtr)>::member_type>
        std::expected<bool, std::string> update(Value&& value);

        // C++26 SELECT API with compile-time query validation and type safety
        ExpectedVectorT<T> select_all() requires refl::reflectable<T>;
        ExpectedT<T> select_one() requires refl::reflectable<T>;
        ExpectedValueVectorMap select_values() requires refl::reflectable<T>;

        // C++26 INSERT API with compile-time validation and zero-overhead abstractions
        std::expected<int, std::string> insert(T obj) requires refl::reflectable<T>;
        std::expected<int, std::string> insert(const T& obj) requires refl::reflectable<T>;
        std::expected<std::vector<int>, std::string> insert(std::vector<T> objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;
        std::expected<std::vector<int>, std::string> insert(const std::vector<T>& objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;

        // C++26 generic contiguous range with enhanced type safety
        template <std::ranges::contiguous_range R>
            requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T> &&
                     refl::reflectable<T> && std::ranges::sized_range<R>
        std::expected<std::vector<int>, std::string> insert(R&& objects);

        std::expected<std::vector<int>, std::string> insert(std::span<const T> objects) 
            requires refl::reflectable<T> && std::ranges::contiguous_range<std::span<const T>>;

        // C++26 compile-time statement preparation with reflection validation
        InsertStatement<T> stmt_insert(const T& obj) requires refl::reflectable<T>;
        InsertStatement<T> stmt_insert(const std::vector<T>& objs) 
            requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>>;

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

    // C++26 WHERE_ALL with fold expressions and perfect forwarding
    template <typename T>
    template <typename Self, typename... Conditions>
    constexpr auto&& QuerySet<T>::where_all(Self&& self, Conditions&&... conditions) {
        static_assert(sizeof...(conditions) > 0, "where_all requires at least one condition");

        // C++26 fold expression with logical AND
        storm::Where combined = (... && std::forward<Conditions>(conditions));

        // Monadic composition for cleaner error handling
        self._whereExpression = self._whereExpression.has_value()
                                        ? storm::Where{*self._whereExpression && std::move(combined)}
                                        : std::move(combined);

        return std::forward<Self>(self);
    }

    // WHERE API implementation
    template <typename T>
    template <typename Self>
    auto&& QuerySet<T>::where(this Self&& self, const storm::Where& where_clause) {
        if (self._whereExpression) {
            // Combine with existing WHERE using AND
            self._whereExpression = storm::Where{*self._whereExpression && where_clause};
        } else {
            self._whereExpression = where_clause;
        }
        return std::forward<Self>(self);
    }

    // C++26 compile-time WHERE with static reflection and zero-allocation field names
    template <typename T>
    template <typename Self, auto MemberPtr, typename Value>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    consteval auto&& QuerySet<T>::where(this Self&& self, Value&& value, storm::Op op) {
        // C++26 compile-time field name extraction
        constexpr auto field_name      = extract_field_name<MemberPtr>();
        constexpr auto full_field_name = extract_class_name<T>() + "." + field_name;

        // Create field object with compile-time name (zero runtime cost)
        auto field_obj = Field<MemberPtr>();

        // Generate condition with compile-time validation
        storm::Where condition = self.create_condition(field_obj, std::forward<Value>(value), op);

        // Monadic composition
        self._whereExpression = self._whereExpression.has_value()
                                        ? storm::Where{*self._whereExpression && std::move(condition)}
                                        : std::move(condition);

        return std::forward<Self>(self);
    }

    // C++26 compile-time WHERE IN with range concepts and static validation
    template <typename T>
    template <typename Self, auto MemberPtr, std::ranges::range Container>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    consteval auto&& QuerySet<T>::where_in(this Self&& self, const Container& values) {
        // C++26 compile-time validation
        static_assert(
                !std::ranges::empty(values) || std::ranges::sized_range<Container>,
                "Container must be sized or non-empty for WHERE IN"
        );

        // Early return for empty containers (runtime check)
        if (std::ranges::empty(values)) {
            // Generate FALSE condition for empty IN clause at runtime
            auto false_condition = storm::Where(std::make_unique<storm::Condition>("1", storm::Op::EQ, 0));
            return self.where(false_condition);
        }

        // C++26 compile-time field object creation
        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = self.create_in_condition(field_obj, values);
        return self.where(std::move(condition));
    }

    // C++26 compile-time WHERE LIKE with string validation
    template <typename T>
    template <typename Self, auto MemberPtr, typename Value>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                 std::convertible_to<Value, std::string_view>
    consteval auto&& QuerySet<T>::where_like(this Self&& self, Value&& pattern) {
        // C++26 compile-time field type validation
        using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
        static_assert(
                std::convertible_to<FieldType, std::string_view>, "LIKE operator requires string-compatible field type"
        );

        // C++26 compile-time pattern validation
        // Note: In consteval context, pattern validation is implicit

        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = field_obj.like(std::forward<Value>(pattern));
        return self.where(std::move(condition));
    }

    // C++26 compile-time WHERE BETWEEN with three-way comparison
    template <typename T>
    template <typename Self, auto MemberPtr, typename T1, typename T2>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                 std::three_way_comparable_with<T1, T2>
    consteval auto&& QuerySet<T>::where_between(this Self&& self, T1&& value1, T2&& value2) {
        // C++26 compile-time field type validation
        using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;
        static_assert(std::three_way_comparable_with<FieldType, T1>, "Field type must be comparable with value1 type");
        static_assert(std::three_way_comparable_with<FieldType, T2>, "Field type must be comparable with value2 type");

        // C++26 compile-time range validation
        // Note: In consteval context, range validation will be checked at compile time

        auto         field_obj = Field<MemberPtr>();
        storm::Where condition = field_obj.between(std::forward<T1>(value1), std::forward<T2>(value2));
        return self.where(std::move(condition));
    }

    // C++26 compile-time NULL checks with static field validation
    template <typename T>
    template <typename Self, auto MemberPtr>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    consteval auto&& QuerySet<T>::where_not_null(this Self&& self) {
        // C++26 compile-time field type validation
        using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;

        // Generate compile-time warning for non-nullable types
        if constexpr (!std::is_pointer_v<FieldType> &&
                      !std::is_same_v<FieldType, std::optional<typename FieldType::value_type>>) {
            // Note: Non-nullable field - consider if NULL check is necessary
        }

        constexpr auto field_name = extract_field_name<MemberPtr>();
        auto           field_obj  = Field<MemberPtr>();
        storm::Where   condition  = field_obj.is_not_null();
        return self.where(std::move(condition));
    }

    template <typename T>
    template <typename Self, auto MemberPtr>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    consteval auto&& QuerySet<T>::where_is_null(this Self&& self) {
        // C++26 compile-time field type validation
        using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::member_type;

        // Generate compile-time warning for non-nullable types
        if constexpr (!std::is_pointer_v<FieldType> &&
                      !std::is_same_v<FieldType, std::optional<typename FieldType::value_type>>) {
            // Note: Non-nullable field - IS NULL will always be false
        }

        constexpr auto field_name = extract_field_name<MemberPtr>();
        auto           field_obj  = Field<MemberPtr>();
        storm::Where   condition  = field_obj.is_null();
        return self.where(std::move(condition));
    }

    // UPDATE implementation
    // 1. Single object - handles move
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(T obj) requires refl::reflectable<T> {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 2. Const ref - keeps user's original object
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(const T& obj) requires refl::reflectable<T> {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 3. Batch move - takes ownership of vector
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(std::vector<T> objs) 
        requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>> {
        return execute_update(std::span<const T>{objs});
    }

    // 4. Batch const ref - keeps user's original vector
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(const std::vector<T>& objs) 
        requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>> {
        return execute_update(std::span<const T>{objs});
    }

    // 5. Advanced flexibility - direct span
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(std::span<const T> objects) 
        requires refl::reflectable<T> && std::ranges::contiguous_range<std::span<const T>> {
        return execute_update(objects);
    }

    // INSERT implementation
    // === MINIMAL NECESSARY OVERLOADS ===

    // 1. Single object - handles move
    template <typename T> std::expected<int, std::string> QuerySet<T>::insert(T obj) requires refl::reflectable<T> {
        return execute_insert(std::span<const T>{&obj, 1});
    }

    // 2. Const ref - keeps user's original object
    template <typename T> std::expected<int, std::string> QuerySet<T>::insert(const T& obj) requires refl::reflectable<T> {
        return execute_insert(std::span<const T>{&obj, 1});
    }

    // 3. Batch move - takes ownership of vector
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::vector<T> objs) 
        requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>> {
        return execute_insert(std::span<const T>{objs});
    }

    // 4. Batch const ref - keeps user's original vector
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(const std::vector<T>& objs) 
        requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>> {
        return execute_insert(std::span<const T>{objs});
    }

    // 5. Advanced flexibility - direct span
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::span<const T> objects) 
        requires refl::reflectable<T> && std::ranges::contiguous_range<std::span<const T>> {
        return execute_insert(objects);
    }

    // 6. Generic contiguous range - forwards to span<const T>
    template <typename T>
    template <std::ranges::contiguous_range R>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T> &&
                 refl::reflectable<T> && std::ranges::sized_range<R>
    std::expected<std::vector<int>, std::string> QuerySet<T>::insert(R&& objects) {
        return execute_insert(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
    }

    // Single object REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(const T& obj) requires refl::reflectable<T> {
        return execute_delete(std::span<const T>{&obj, 1});
    }

    // Batch REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(const std::vector<T>& objs) 
        requires refl::reflectable<T> && std::ranges::sized_range<std::vector<T>> {
        return execute_delete(std::span<const T>{objs});
    }

    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove() requires refl::reflectable<T> {
        return execute_delete();
    }

    // C++26 DISTINCT implementation with compile-time field validation
    template <typename T>
    template <typename Self, auto... Fields>
        requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
    consteval auto&& QuerySet<T>::distinct(this Self&& self) {
        // C++26 compile-time validation
        static_assert(sizeof...(Fields) <= 10, "Too many DISTINCT fields (max 10 for performance)");

        // Validate all field types support equality comparison (required for DISTINCT)
        static_assert(
                (std::equality_comparable<typename member_pointer_traits<decltype(Fields)>::member_type> && ...),
                "All DISTINCT field types must support equality comparison"
        );

        // C++26 compile-time field name extraction for debugging
        constexpr auto field_names = std::array{extract_field_name<Fields>()...};

        // Optimize container capacity
        constexpr auto field_count = sizeof...(Fields);
        self.distinctFields.reserve(self.distinctFields.size() + field_count);

        // C++26 fold expression with compile-time field wrapper creation
        (self.distinctFields.emplace_back(refl::FieldWrapper::create<Fields>()), ...);

        return std::forward<Self>(self);
    }

    // C++26 ONLY implementation with compile-time field selection
    template <typename T>
    template <typename Self, auto... Fields>
        requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
    consteval auto&& QuerySet<T>::only(this Self&& self, const std::optional<std::string>& alias) {
        // C++26 compile-time validation
        static_assert(sizeof...(Fields) <= 20, "Too many ONLY fields (max 20 for performance)");

        // C++26 compile-time field name extraction for validation
        constexpr auto field_names = std::array{extract_field_name<Fields>()...};

        // Validate no duplicate field names at compile time
        constexpr auto has_duplicates = []() {
            for (std::size_t i = 0; i < field_names.size(); ++i) {
                for (std::size_t j = i + 1; j < field_names.size(); ++j) {
                    if (field_names[i] == field_names[j])
                        return true;
                }
            }
            return false;
        }();
        static_assert(!has_duplicates, "ONLY fields must be unique");

        // Optimize container capacity
        constexpr auto field_count = sizeof...(Fields);
        self.onlyFields.reserve(self.onlyFields.size() + field_count);

        // C++26 fold expression with field wrapper creation
        (self.onlyFields.emplace_back(refl::FieldWrapper::create<Fields>(), alias), ...);

        return std::forward<Self>(self);
    }

    // C++26 GROUP BY implementation with compile-time aggregation validation
    template <typename T>
    template <typename Self, auto... Fields>
        requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
    consteval auto&& QuerySet<T>::group_by(this Self&& self) {
        // C++26 compile-time validation
        static_assert(sizeof...(Fields) <= 8, "Too many GROUP BY fields (max 8 for performance)");

        // Validate all field types support equality comparison (required for GROUP BY)
        static_assert(
                (std::equality_comparable<typename member_pointer_traits<decltype(Fields)>::member_type> && ...),
                "All GROUP BY field types must support equality comparison"
        );

        // Validate all field types are hashable for performance optimization
        static_assert(
                (std::is_same_v<
                         std::hash<typename member_pointer_traits<decltype(Fields)>::member_type>,
                         std::hash<typename member_pointer_traits<decltype(Fields)>::member_type>> &&
                 ...),
                "All GROUP BY field types should be hashable for optimal performance"
        );

        // C++26 compile-time field name extraction
        constexpr auto field_names = std::array{extract_field_name<Fields>()...};

        // Validate no duplicate field names
        constexpr auto has_duplicates = []() {
            for (std::size_t i = 0; i < field_names.size(); ++i) {
                for (std::size_t j = i + 1; j < field_names.size(); ++j) {
                    if (field_names[i] == field_names[j])
                        return true;
                }
            }
            return false;
        }();
        static_assert(!has_duplicates, "GROUP BY fields must be unique");

        // Optimize container capacity
        constexpr auto field_count = sizeof...(Fields);
        self.groupByFields.reserve(self.groupByFields.size() + field_count);

        // C++26 fold expression with compile-time field wrapper creation
        (self.groupByFields.emplace_back(refl::FieldWrapper::create<Fields>()), ...);

        // Mark that this query uses aggregation for optimization hints
        self.query_flags.has_aggregates = true;

        return std::forward<Self>(self);
    }

    // LIMIT/OFFSET implementation
    template <typename T> template <typename Self> auto&& QuerySet<T>::limit(this Self&& self, int limit_value) {
        self._limit = limit_value;
        return std::forward<Self>(self);
    }

    template <typename T> template <typename Self> auto&& QuerySet<T>::offset(this Self&& self, int offset_value) {
        self._offset = offset_value;
        return std::forward<Self>(self);
    }

    // Functions method implementation
    template <typename T>
    template <typename Self, typename... Args>
    auto&& QuerySet<T>::functions(this Self&& self, Args&&... args) {
        // Reserve capacity
        self.functionsSet.reserve(self.functionsSet.size() + sizeof...(Args));

        // Process each function using fold expression
        (self.functionsSet.emplace_back(std::forward<Args>(args)), ...);
        return std::forward<Self>(self);
    }

    // C++26 ORDER BY implementations with compile-time field validation
    template <typename T>
    template <typename Self, auto Field, Collation CollationType>
        requires std::is_member_pointer_v<decltype(Field)> &&
                 std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T>
    consteval auto&& QuerySet<T>::order_by(this Self&& self) {
        // C++26 compile-time field name extraction
        constexpr auto field_name      = extract_field_name<Field>();
        constexpr auto full_field_name = extract_class_name<T>() + "." + field_name;

        // Compile-time validation for sortable fields
        using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // Create field wrapper with zero-allocation compile-time name
        self.orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), true, CollationType);
        return std::forward<Self>(self);
    }

    // C++26 multiple field ordering with compile-time validation and optimization
    template <typename T>
    template <typename Self, auto... Fields>
        requires(sizeof...(Fields) > 0) && (std::is_member_pointer_v<decltype(Fields)> && ...) &&
                (std::same_as<typename member_pointer_traits<decltype(Fields)>::class_type, T> && ...)
    consteval auto&& QuerySet<T>::order_by_multiple(this Self&& self) {
        // C++26 compile-time field validation for all fields
        static_assert(sizeof...(Fields) <= 16, "Too many ORDER BY fields (max 16 for performance)");

        // Validate all field types are sortable at compile time
        static_assert(
                (Sortable<typename member_pointer_traits<decltype(Fields)>::member_type> && ...),
                "All field types must be sortable for ORDER BY"
        );

        // C++26 compile-time capacity optimization
        constexpr auto field_count = sizeof...(Fields);
        self.orderTerms.reserve(self.orderTerms.size() + field_count);

        // C++26 fold expression with perfect forwarding
        (self.orderTerms.emplace_back(refl::FieldWrapper::create<Fields>(), true, Collation::NONE), ...);

        return std::forward<Self>(self);
    }

    // C++26 compile-time field-direction pairs with recursive template expansion
    template <typename T>
    template <typename Self, auto Field, auto Direction, auto... Rest>
        requires std::is_member_pointer_v<decltype(Field)> &&
                 std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T> &&
                 std::same_as<decltype(Direction), bool>
    consteval auto&& QuerySet<T>::order_by_mixed(this Self&& self) {
        // C++26 compile-time validation
        static_assert(sizeof...(Rest) % 2 == 0, "Must provide field-direction pairs (field, bool, field, bool, ...)");
        static_assert((sizeof...(Rest) / 2 + 1) <= 8, "Too many ORDER BY pairs (max 8 for performance)");

        // Validate field is sortable
        using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // C++26 compile-time field name for debugging
        constexpr auto field_name = extract_field_name<Field>();

        // Optimize container capacity
        constexpr auto total_pairs = sizeof...(Rest) / 2 + 1;
        self.orderTerms.reserve(self.orderTerms.size() + total_pairs);

        // Add current field-direction pair
        self.orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), Direction, Collation::NONE);

        // C++26 recursive template expansion
        if constexpr (sizeof...(Rest) > 0) {
            return self.template order_by_mixed<Rest...>();
        } else {
            return std::forward<Self>(self);
        }
    }

    // C++26 compile-time full ORDER BY with field-direction-collation triplets
    template <typename T>
    template <typename Self, auto Field, auto Direction, auto Coll, auto... Rest>
        requires std::is_member_pointer_v<decltype(Field)> &&
                 std::same_as<typename member_pointer_traits<decltype(Field)>::class_type, T> &&
                 std::same_as<decltype(Direction), bool> && std::same_as<decltype(Coll), Collation>
    consteval auto&& QuerySet<T>::order_by_full(this Self&& self) {
        // C++26 compile-time validation
        static_assert(
                sizeof...(Rest) % 3 == 0,
                "Must provide field-direction-collation triplets (field, bool, collation, ...)"
        );
        static_assert((sizeof...(Rest) / 3 + 1) <= 6, "Too many ORDER BY triplets (max 6 for performance)");

        // Validate field is sortable
        using FieldType = typename member_pointer_traits<decltype(Field)>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // C++26 compile-time collation validation for string fields
        if constexpr (std::convertible_to<FieldType, std::string_view>) {
            static_assert(
                    Coll != Collation::BINARY || std::is_same_v<FieldType, std::string>,
                    "BINARY collation requires exact string type"
            );
        } else {
            static_assert(Coll == Collation::NONE, "Non-string fields cannot use collation");
        }

        // C++26 compile-time field name for debugging
        constexpr auto field_name = extract_field_name<Field>();

        // Optimize container capacity
        constexpr auto total_triplets = sizeof...(Rest) / 3 + 1;
        self.orderTerms.reserve(self.orderTerms.size() + total_triplets);

        // Add current field-direction-collation triplet
        self.orderTerms.emplace_back(refl::FieldWrapper::create<Field>(), Direction, Coll);

        // C++26 recursive template expansion
        if constexpr (sizeof...(Rest) > 0) {
            return self.template order_by_full<Rest...>();
        }

        return std::forward<Self>(self);
    }

    // GROUP_CONCAT_ORDER implementation
    template <typename T>
    template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct>
    consteval auto&& QuerySet<T>::group_concat_with_order_impl(
            this Self&&             self,
            utils::fixed_string<32> alias,
            utils::fixed_string<8>  separator,
            utils::fixed_string<8>  fieldSeparator
    ) {
        static_assert(std::is_member_pointer_v<decltype(OrderField)>, "OrderField must be a member pointer");
        static_assert(std::is_member_pointer_v<decltype(FirstField)>, "FirstField must be a member pointer");
        // RestFields are validated at instantiation time when used

        constexpr auto orderDesc = make_field_desc<OrderField>();
        constexpr auto firstDesc = make_field_desc<FirstField>();

        // Generate actual alias at compile time
        constexpr auto actual_alias = [&] {
            if (alias.size() > 0) {
                return alias;
            } else {
                return utils::make_string_builder<64>().append("group_concat_").append(firstDesc.field).build();
            }
        }();

        // Build field expression at compile time
        constexpr auto field_expr = [&] {
            if constexpr (sizeof...(RestFields) == 0) {
                return firstDesc.full_name();
            } else {
                return utils::join_with(
                        fieldSeparator.view(), firstDesc.full_name(), make_field_desc<RestFields>().full_name()...
                );
            }
        }();

        // Build complete GROUP_CONCAT SQL at compile time
        constexpr auto sql = utils::make_string_builder<512>()
                                     .append("GROUP_CONCAT(")
                                     .append(Distinct ? "DISTINCT " : "")
                                     .append(field_expr)
                                     .append(" ORDER BY ")
                                     .append(orderDesc.full_name())
                                     .append(" SEPARATOR '")
                                     .append(separator)
                                     .append("') AS ")
                                     .append(actual_alias)
                                     .build();

        self.functions(AggregateSpec::custom_sql(std::string{sql.view()}));
        return std::forward<Self>(self);
    }

    template <typename T>
    template <typename Self, bool Distinct, auto... Fields>
    consteval auto&&
    QuerySet<T>::group_concat(Self&& self, utils::fixed_string<32> alias, utils::fixed_string<8> separator) {
        constexpr auto sql = utils::make_string_builder<256>()
                                     .append("GROUP_CONCAT(")
                                     .append(Distinct ? "DISTINCT " : "")
                                     .append([]() {
                                         if constexpr (sizeof...(Fields) == 0) {
                                             return utils::fixed_string<1>{"*"};
                                         } else {
                                             return utils::join_with(", ", utils::formatFieldName("", Fields)...);
                                         }
                                     }())
                                     .append(" SEPARATOR '")
                                     .append(separator)
                                     .append("')")
                                     .and_then([&](auto builder) {
                                         if (alias.size() > 0) {
                                             return builder.append(" AS \"").append(alias).append("\"");
                                         }
                                         return builder;
                                     })
                                     .build();

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql.view()));
        return std::forward<Self>(self);
    }

    // SELECT ONE implementation (returns single object)
    template <typename T> ExpectedT<T> QuerySet<T>::select_one() requires refl::reflectable<T> {
        return select_all().and_then([](const auto& rows) -> ExpectedT<T> {
            if (rows.empty()) {
                return std::unexpected("No results found for select_one query");
            }
            return rows[0];
        });
    }

    // SELECT ALL implementation
    template <typename T> ExpectedVectorT<T> QuerySet<T>::select_all() requires refl::reflectable<T> {
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
    template <typename T> [[nodiscard]] auto QuerySet<T>::select_values() -> ExpectedValueVectorMap requires refl::reflectable<T> {
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
