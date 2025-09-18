module;

// Module global fragment - third-party C headers (macros not exported by
// modules)
#include <sqlite3.h>
#include <storm/macros.h>

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
        constexpr compile_time_sql() = default;

        constexpr auto& append(std::string_view str) {
            for (auto c : str) {
                if (pos < N)
                    buffer[pos++] = c;
            }
            return *this;
        }

        constexpr auto get() const {
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

    template <auto MemberPtr>
    using field_type_t = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;

    template <auto MemberPtr>
    using class_type_t = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type;

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
    // TODO: replace with C++26 pattern matching
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

        std::vector<JoinWrapper>                                            join_clauses;
        std::vector<std::tuple<refl::FieldWrapper, bool, Collation>>        orderTerms;
        std::vector<std::pair<refl::FieldWrapper, utils::fixed_string<32>>> onlyFields;
        std::vector<AggregateSpec>                                          functionsSet;

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
        template <typename Self> constexpr auto&& where(this Self&& self, const storm::Where& where_clause);
        template <typename Self> constexpr auto&& where(this Self&& self, storm::Where&& where_clause);

        // WHERE with multiple conditions using C++26 fold expressions
        template <typename Self, typename... Conditions>
        constexpr auto&& where_all(this Self&& self, Conditions&&... conditions);

        // Macro-based WHERE with compile-time field resolution - Implementation functions
        // These are called by the where() macro and should not be used directly

        // Basic equality and operator - where(field, value) / where(field, value, op)
        template <auto MemberPtr, typename Self, typename Value>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        constexpr auto&& where_impl(this Self&& self, Value&& value, storm::Op op = storm::Op::EQ);

        // BETWEEN - where(field, value1, value2)
        template <auto MemberPtr, typename Self, typename T1, typename T2>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     std::three_way_comparable_with<T1, T2> && (!std::same_as<T2, storm::Op>)
        constexpr auto&& where_impl(this Self&& self, T1&& value1, T2&& value2);

        // NULL checks - where(field, Op::IS) / where(field, Op::IS_NOT)
        template <auto MemberPtr, typename Self>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>
        constexpr auto&& where_impl(this Self&& self, storm::Op null_op);

        // IN clause - where(field, container)
        template <auto MemberPtr, typename Self, std::ranges::range Container>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     (!std::same_as<Container, storm::Op>)
        constexpr auto&& where_impl(this Self&& self, const Container& values);

        // ORDER BY API (C++26 upgraded declarations)
        // C++26 compile-time multiple field ordering with variadic validation
        template <typename Self>
        constexpr auto&& order_by(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        // C++26 compile-time field-direction pairs with static assertion
        template <typename Self>
        constexpr auto&& order_by(this Self&& self, auto field, auto direction, auto... rest)
            requires std::same_as<decltype(direction), bool>;
        // C++26 compile-time field-collation pairs with validation
        template <typename Self>
        constexpr auto&& order_by(this Self&& self, auto field, auto collation)
            requires std::same_as<decltype(collation), Collation>;

        // C++26 compile-time field-direction-collation triplets with full validation
        template <typename Self>
        constexpr auto&& order_by(this Self&& self, auto field, auto direction, auto collation, auto... rest)
            requires std::same_as<decltype(direction), bool> && std::same_as<decltype(collation), Collation>;

        // DISTINCT API (C++26 upgraded declarations with function parameter deduction)
        template <typename Self>
        constexpr auto&& distinct(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        // ONLY API (C++26 upgraded declarations with function parameter deduction)
        // Simple version: .only(field1, field2, ...)
        template <typename Self>
        constexpr auto&& only(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        // Alias version: .only(field, alias, field, alias, ...)
        template <typename Self>
        constexpr auto&& only_with_aliases(this Self&& self, auto... field_alias_pairs)
            requires(sizeof...(field_alias_pairs) > 0) && (sizeof...(field_alias_pairs) % 2 == 0);

        // GROUP BY API (C++26 upgraded declarations with function parameter deduction)
        template <typename Self>
        constexpr auto&& group_by(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        // In class declaration:
        template <typename Self>
        auto&&
        group_concat(this Self&& self, auto field, utils::fixed_string<32> alias = "", utils::fixed_string<8> separator = ",", bool distinct = false);

        // Overload with ORDER BY for multiple fields - requires explicit
        // specification
        template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct = false>
        constexpr auto&& group_concat_order(
                this Self&&             self,
                utils::fixed_string<32> alias          = {},
                utils::fixed_string<8>  separator      = {","},
                utils::fixed_string<8>  fieldSeparator = {","}
        ) {
            return self.template group_concat_with_order_impl<OrderField, FirstField, RestFields..., Distinct>(
                    alias, separator, fieldSeparator
            );
        }

        // Parameter-based overload for group_concat_order with bool literal support
        template <typename Self, bool Distinct>
        constexpr auto&& group_concat_order(
                this Self&&             self,
                auto                    orderField,
                auto                    firstField,
                utils::fixed_string<32> alias,
                utils::fixed_string<8>  separator,
                utils::fixed_string<8>  fieldSeparator,
                std::integral_constant<bool, Distinct>
        ) {
            return self.template group_concat_with_order_impl<orderField, firstField, Distinct>(
                    alias, separator, fieldSeparator
            );
        }

        // Parameter-based overload for group_concat_order with runtime bool
        template <typename Self>
        constexpr auto&& group_concat_order(
                this Self&&             self,
                auto                    orderField,
                auto                    firstField,
                utils::fixed_string<32> alias,
                utils::fixed_string<8>  separator,
                utils::fixed_string<8>  fieldSeparator,
                bool                    distinct
        ) {
            if (distinct) {
                return self.template group_concat_with_order_impl<orderField, firstField, true>(
                        alias, separator, fieldSeparator
                );
            } else {
                return self.template group_concat_with_order_impl<orderField, firstField, false>(
                        alias, separator, fieldSeparator
                );
            }
        }

        template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct = false>
        constexpr auto&& group_concat_with_order_impl(
                this Self&&             self,
                utils::fixed_string<32> alias          = {},
                utils::fixed_string<8>  separator      = {","},
                utils::fixed_string<8>  fieldSeparator = {","}
        );

        template <typename Self> auto&& limit(this Self&& self, int limit_value);
        template <typename Self> auto&& offset(this Self&& self, int offset_value);

        // C++26 Aggregate functions with compile-time validation
        template <typename Self>
        constexpr auto&& max(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::max(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& min(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::min(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& avg(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::avg(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& count(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::count(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& sum(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::sum(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        // C++26 Aggregate value methods with field() syntax - direct execution
        template <typename Self> [[nodiscard]] constexpr auto max_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Max>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto min_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Min>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto avg_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Avg>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto count_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Count>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto sum_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Sum>(field);
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

        // C++26 compile-time aggregate execution with field parameter
        template <AggregateKind Kind> [[nodiscard]] constexpr auto execute_aggregate(auto field) const {
            constexpr auto MemberPtr = decltype(field)::member_ptr;
            using FieldType =
                    typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
            using ResultType = aggregate_result_t<Kind, FieldType>;

            // C++26 compile-time spec generation with built-in validation
            AggregateSpec spec;
            if constexpr (Kind == AggregateKind::Max)
                spec = AggregateSpec::max(field);
            else if constexpr (Kind == AggregateKind::Min)
                spec = AggregateSpec::min(field);
            else if constexpr (Kind == AggregateKind::Avg)
                spec = AggregateSpec::avg(field);
            else if constexpr (Kind == AggregateKind::Count)
                spec = AggregateSpec::count(field);
            else
                spec = AggregateSpec::sum(field);

            SelectOptions opts{
                    .functions_set = {spec},
                    .where_clause  = _whereExpression,
            };

            auto result = SelectStatement<T>(conn, std::move(opts)).execute_values();
            if (!result) [[unlikely]]
                return std::expected<ResultType, std::string>(std::unexpected(result.error()));

            if (result->empty()) [[unlikely]]
                return std::expected<ResultType, std::string>(std::unexpected(std::string("No results")));

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

            return std::expected<ResultType, std::string>(std::unexpected(std::string("No data in result")));
        }

        // FUNCTIONS API (declarations)
        template <typename Self, typename... Args> auto&& functions(this Self&& self, Args&&... args);

        // C++26 JOIN API with compile-time relationship validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        constexpr auto&& join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Inner>());

            return std::forward<Self>(self);
        }

        // Helper to extract table name from field name (e.g., "author_id" -> "Author")
        template <auto MemberPtr> static consteval auto extract_table_name_from_field() {
            constexpr auto field_name = extract_field_name<MemberPtr>();

            // Find "_id" suffix and extract base name
            static_assert(field_name.ends_with("_id"), "Foreign key field must end with '_id'");

            constexpr auto base_name = field_name.substr(0, field_name.size() - 3); // Remove "_id"

            // Capitalize first letter to get table name
            constexpr auto table_name = [&]() {
                auto result = base_name;
                if (!result.empty()) {
                    result[0] = static_cast<char>(std::toupper(result[0]));
                }
                return result;
            }();

            return table_name;
        }

        // C++26 JOIN API with field() syntax - using string table name
        template <typename Self, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)>
        constexpr auto&& join(this Self&& self, Field<MemberPtr>) {
            constexpr auto field_name = extract_field_name<MemberPtr>();

            // Extract table name: "author_id" -> "author"
            static_assert(field_name.ends_with("_id"), "Foreign key field must end with '_id'");
            constexpr auto base_name = field_name.substr(0, field_name.size() - 3);

            // Capitalize first letter to get proper table name: "author" -> "Author"
            auto table_name = [&]() {
                std::string result{base_name}; // Convert to mutable string
                if (!result.empty()) {
                    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
                }
                return result;
            }();

            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "JOIN field must support equality comparison");

            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create_with_string<T, MemberPtr, JoinType::Inner>(table_name));

            return std::forward<Self>(self);
        }

        // C++26 LEFT JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        constexpr auto&& left_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "LEFT JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "LEFT JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Left>());

            return std::forward<Self>(self);
        }

        // C++26 RIGHT JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        constexpr auto&& right_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "RIGHT JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "RIGHT JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Right>());

            return std::forward<Self>(self);
        }

        // C++26 FULL OUTER JOIN with compile-time validation
        template <typename Self, class U, auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     (std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> ||
                      std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>) &&
                     refl::reflectable<T> && refl::reflectable<U>
        constexpr auto&& full_join(this Self&& self) {
            // C++26 compile-time field validation
            constexpr auto field_name  = extract_field_name<MemberPtr>();
            constexpr auto left_table  = extract_class_name<T>();
            constexpr auto right_table = extract_class_name<U>();

            // Validate join field types are compatible
            using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
            static_assert(std::equality_comparable<FieldType>, "FULL JOIN field must support equality comparison");

            // C++26 compile-time join validation
            constexpr bool is_left_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>;
            constexpr bool is_right_field =
                    std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, U>;
            static_assert(is_left_field || is_right_field, "FULL JOIN field must belong to one of the joined tables");

            // Reserve capacity for join clauses
            self.join_clauses.reserve(self.join_clauses.size() + 1);
            self.join_clauses.emplace_back(JoinWrapper::create<T, U, MemberPtr, JoinType::Full>());

            return std::forward<Self>(self);
        }

        // C++26 REMOVE API with compile-time validation and constraint checking
        std::expected<bool, std::string> remove(const T& obj)
            requires refl::reflectable<T>;
        std::expected<bool, std::string> remove(std::span<const T> objects)
            requires refl::reflectable<T>;
        std::expected<bool, std::string> remove()
            requires refl::reflectable<T>;

        // C++26 UPDATE API with compile-time validation and concepts
        std::expected<bool, std::string> update(const T& obj)
            requires refl::reflectable<T>;
        std::expected<bool, std::string> update(std::span<const T> objects)
            requires refl::reflectable<T>;

        // C++26 field-specific update with compile-time validation
        template <auto MemberPtr, typename Value>
            requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                     std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                     std::convertible_to<
                             Value,
                             typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type>
        std::expected<bool, std::string> update(Value&& value);

        // C++26 SELECT API with compile-time query validation and type safety
        ExpectedVectorT<T> select_all()
            requires refl::reflectable<T>;
        ExpectedT<T> select_one()
            requires refl::reflectable<T>;
        ExpectedValueVectorMap select_values()
            requires refl::reflectable<T>;

        // C++26 INSERT API with compile-time validation and zero-overhead abstractions
        std::expected<int, std::string> insert(const T& obj)
            requires refl::reflectable<T>;
        std::expected<std::vector<int>, std::string> insert(std::span<const T> objects)
            requires refl::reflectable<T>;

        // C++26 generic contiguous range with enhanced type safety
        template <std::ranges::contiguous_range R>
            requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T> && refl::reflectable<T> &&
                     std::ranges::sized_range<R>
        std::expected<std::vector<int>, std::string> insert(R&& objects);

        // C++26 compile-time statement preparation with reflection validation
        InsertStatement<T> stmt_insert(const T& obj)
            requires refl::reflectable<T>;
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

        template <typename U> [[nodiscard]] constexpr std::string_view get_table_name() {
            return refl::reflect<U>::name();
        }

        template <auto FirstField, auto... RestFields>
        std::pair<std::string, std::string>
        prepare_group_concat(utils::fixed_string<32> alias, utils::fixed_string<8> fieldSeparator) {
            // Validate all member pointers
            static_assert(std::is_member_pointer_v<decltype(FirstField)>, "FirstField must be a member pointer");
            (check_member_pointer<RestFields>(), ...);

            auto firstDesc = make_field_desc<FirstField>();

            // Generate alias at compile time
            const auto actual_alias = [&]() {
                if (alias.c_str()[0] != '\0') {
                    return alias;
                } else {
                    return utils::make_string_builder<64>().append("group_concat_").append(firstDesc.field).build();
                }
            }();

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

        template <auto Field> constexpr void check_member_pointer() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
        }
    };

    // C++26 WHERE_ALL with fold expressions and perfect forwarding
    template <typename T>
    template <typename Self, typename... Conditions>
    constexpr auto&& QuerySet<T>::where_all(this Self&& self, Conditions&&... conditions) {
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
    constexpr auto&& QuerySet<T>::where(this Self&& self, const storm::Where& where_clause) {
        if (self._whereExpression) {
            // Combine with existing WHERE using AND
            self._whereExpression = storm::Where{*self._whereExpression && where_clause};
        } else {
            self._whereExpression = where_clause;
        }
        return std::forward<Self>(self);
    }

    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::where(this Self&& self, storm::Where&& where_clause) {
        if (self._whereExpression) {
            // Combine with existing WHERE using AND
            self._whereExpression = storm::Where{*self._whereExpression && std::move(where_clause)};
        } else {
            self._whereExpression = std::move(where_clause);
        }
        return std::forward<Self>(self);
    }

    // Macro-based WHERE implementations with compile-time field resolution

    // Basic equality and operator - where(field, value) / where(field, value, op)
    template <typename T>
    template <auto MemberPtr, typename Self, typename Value>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    constexpr auto&& QuerySet<T>::where_impl(this Self&& self, Value&& value, storm::Op op) {
        // ✅ COMPILE-TIME: Field name resolution using template parameter
        constexpr auto field_name = extract_field_name<MemberPtr>();

        // Runtime: Create condition with compile-time resolved field name
        storm::Where condition = storm::Where(
                std::make_unique<storm::Condition>(std::string(field_name), op, std::forward<Value>(value))
        );

        // Runtime: Combine with existing WHERE clause
        self._whereExpression = self._whereExpression.has_value()
                                        ? storm::Where{*self._whereExpression && std::move(condition)}
                                        : std::move(condition);

        return std::forward<Self>(self);
    }

    // BETWEEN - where(field, value1, value2)
    template <typename T>
    template <auto MemberPtr, typename Self, typename T1, typename T2>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                 std::three_way_comparable_with<T1, T2> && (!std::same_as<T2, storm::Op>)
    constexpr auto&& QuerySet<T>::where_impl(this Self&& self, T1&& value1, T2&& value2) {
        // ✅ COMPILE-TIME: Field validation and name resolution
        using FieldType = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
        static_assert(std::three_way_comparable_with<FieldType, T1>, "Field type must be comparable with value1 type");
        static_assert(std::three_way_comparable_with<FieldType, T2>, "Field type must be comparable with value2 type");
        constexpr auto field_name = extract_field_name<MemberPtr>();

        // Runtime: Create BETWEEN condition
        storm::Where condition = storm::Where(
                std::make_unique<storm::Condition>(
                        std::string(field_name), std::forward<T1>(value1), std::forward<T2>(value2)
                )
        );

        self._whereExpression = self._whereExpression.has_value()
                                        ? storm::Where{*self._whereExpression && std::move(condition)}
                                        : std::move(condition);

        return std::forward<Self>(self);
    }

    // NULL checks - where(field, Op::IS) / where(field, Op::IS_NOT)
    template <typename T>
    template <auto MemberPtr, typename Self>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T>
    constexpr auto&& QuerySet<T>::where_impl(this Self&& self, storm::Op null_op) {
        // ✅ COMPILE-TIME: Field validation and name resolution
        using FieldType           = typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type;
        constexpr auto field_name = extract_field_name<MemberPtr>();

        // Compile-time warning for non-nullable types
        if constexpr (!std::is_pointer_v<FieldType> &&
                      !std::is_same_v<FieldType, std::optional<typename FieldType::value_type>>) {
            // Note: Non-nullable field - NULL check may always be false/true
        }

        // Runtime: Create NULL condition
        storm::Where condition =
                storm::Where(std::make_unique<storm::Condition>(std::string(field_name), null_op, std::nullopt));

        self._whereExpression = self._whereExpression.has_value()
                                        ? storm::Where{*self._whereExpression && std::move(condition)}
                                        : std::move(condition);

        return std::forward<Self>(self);
    }

    // IN clause - where(field, container)
    template <typename T>
    template <auto MemberPtr, typename Self, std::ranges::range Container>
        requires std::is_member_pointer_v<decltype(MemberPtr)> &&
                 std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
                 (!std::same_as<Container, storm::Op>)
    constexpr auto&& QuerySet<T>::where_impl(this Self&& self, const Container& values) {
        // ✅ COMPILE-TIME: Field name resolution
        constexpr auto field_name = extract_field_name<MemberPtr>();

        // Runtime: Early return for empty containers
        if (std::ranges::empty(values)) {
            auto false_condition = storm::Where(std::make_unique<storm::Condition>("1", storm::Op::EQ, 0));
            return self.where(false_condition);
        }

        // Runtime: Create IN condition as OR chain
        auto         it = std::ranges::begin(values);
        storm::Where condition =
                storm::Where(std::make_unique<storm::Condition>(std::string(field_name), storm::Op::EQ, *it));

        ++it;
        for (; it != std::ranges::end(values); ++it) {
            auto next_condition =
                    storm::Where(std::make_unique<storm::Condition>(std::string(field_name), storm::Op::EQ, *it));
            condition = condition || std::move(next_condition);
        }

        return self.where(std::move(condition));
    }

    // UPDATE implementation
    // 1. Single object - handles move
    template <typename T>
    std::expected<bool, std::string> QuerySet<T>::update(const T& obj)
        requires refl::reflectable<T>
    {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 2. Batch update - modern span-based API
    template <typename T>
    std::expected<bool, std::string> QuerySet<T>::update(std::span<const T> objects)
        requires refl::reflectable<T>
    {
        return execute_update(objects);
    }

    // INSERT implementation
    // === MINIMAL NECESSARY OVERLOADS ===

    // 1. Single object - handles move
    template <typename T>
    std::expected<int, std::string> QuerySet<T>::insert(const T& obj)
        requires refl::reflectable<T>
    {
        return execute_insert(std::span<const T>{&obj, 1});
    }

    // 2. Batch insert - modern span-based API
    template <typename T>
    std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::span<const T> objects)
        requires refl::reflectable<T>
    {
        return execute_insert(objects);
    }

    // 3. Generic contiguous range - forwards to span<const T>
    template <typename T>
    template <std::ranges::contiguous_range R>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T> && refl::reflectable<T> &&
                 std::ranges::sized_range<R>
    std::expected<std::vector<int>, std::string> QuerySet<T>::insert(R&& objects) {
        return execute_insert(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
    }

    // Single object REMOVE implementation
    template <typename T>
    std::expected<bool, std::string> QuerySet<T>::remove(const T& obj)
        requires refl::reflectable<T>
    {
        return execute_delete(std::span<const T>{&obj, 1});
    }

    // Batch REMOVE implementation
    template <typename T>
    std::expected<bool, std::string> QuerySet<T>::remove(std::span<const T> objects)
        requires refl::reflectable<T>
    {
        return execute_delete(objects);
    }

    template <typename T>
    std::expected<bool, std::string> QuerySet<T>::remove()
        requires refl::reflectable<T>
    {
        return execute_delete();
    }

    // C++26 DISTINCT implementation with function parameter deduction
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::distinct(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 10, "Too many DISTINCT fields (max 10 for performance)");

        // Field validation will be handled by FieldWrapper::create

        // Note: Field name extraction not needed for runtime validation

        // Optimize container capacity
        constexpr auto field_count = sizeof...(fields);
        self.distinctFields.reserve(self.distinctFields.size() + field_count);

        // C++26 fold expression with compile-time field wrapper creation
        (self.distinctFields.emplace_back(refl::FieldWrapper::create(fields)), ...);

        return std::forward<Self>(self);
    }

    // C++26 ONLY implementation - Simple version: .only(field1, field2, ...)
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::only(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 20, "Too many ONLY fields (max 20 for performance)");

        // Note: Field name extraction not needed for runtime validation
        // Note: Duplicate field validation would require compile-time field name comparison

        // Optimize container capacity
        constexpr auto field_count = sizeof...(fields);
        self.onlyFields.reserve(self.onlyFields.size() + field_count);

        // C++26 fold expression with field wrapper creation (no aliases)
        (self.onlyFields.emplace_back(refl::FieldWrapper::create(fields), utils::fixed_string<32>{}), ...);

        return std::forward<Self>(self);
    }

    // C++26 ONLY implementation - Alias version: .only(field, alias, field, alias, ...)
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::only_with_aliases(this Self&& self, auto... field_alias_pairs)
        requires(sizeof...(field_alias_pairs) > 0) && (sizeof...(field_alias_pairs) % 2 == 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(field_alias_pairs) <= 40, "Too many field-alias pairs (max 20 pairs for performance)");
        static_assert(sizeof...(field_alias_pairs) % 2 == 0, "Must provide field-alias pairs");

        // Extract field-alias pairs at compile time
        constexpr auto pairs = std::make_tuple(field_alias_pairs...);

        // Process pairs using index sequence
        return [&]<std::size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
            constexpr auto field_count = sizeof...(field_alias_pairs) / 2;
            self.onlyFields.reserve(self.onlyFields.size() + field_count);

            // Validate fields are member pointers and add them
            (([&self]<std::size_t Idx>() {
                 constexpr auto field = std::get<Idx * 2>(pairs);
                 constexpr auto alias = std::get<Idx * 2 + 1>(pairs);

                 static_assert(
                         std::is_member_pointer_v<decltype(field)>, "Even-indexed arguments must be member pointers"
                 );
                 static_assert(
                         std::same_as<typename refl::meta::member_pointer_traits<decltype(field)>::class_type, T>,
                         "Field must belong to the correct class"
                 );

                 self.onlyFields.emplace_back(refl::FieldWrapper::create(field), alias);
             }.template operator()<I>()),
             ...);

            return std::forward<Self>(self);
        }(std::make_index_sequence<sizeof...(field_alias_pairs) / 2>{});
    }

    // C++26 GROUP BY implementation with function parameter deduction
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::group_by(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 8, "Too many GROUP BY fields (max 8 for performance)");

        // Field validation will be handled by FieldWrapper::create

        // Note: Field name extraction not needed for runtime validation
        // Note: Duplicate field validation would require compile-time field name comparison

        // Optimize container capacity
        constexpr auto field_count = sizeof...(fields);
        self.groupByFields.reserve(self.groupByFields.size() + field_count);

        // C++26 fold expression with compile-time field wrapper creation
        (self.groupByFields.emplace_back(refl::FieldWrapper::create(fields)), ...);

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

    // C++26 compile-time field-direction pairs ordering
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::order_by(this Self&& self, auto field, auto direction, auto... rest)
        requires std::same_as<decltype(direction), bool>
    {
        // Validate variadic arguments come in field-direction pairs
        static_assert(sizeof...(rest) % 2 == 0, "Must provide field-direction pairs");

        // Field validation will be handled by FieldWrapper::create

        // Optimize container capacity
        constexpr auto total_pairs = sizeof...(rest) / 2 + 1;
        self.orderTerms.reserve(self.orderTerms.size() + total_pairs);

        // Process all field-direction pairs with parameter pack expansion
        auto pack = std::make_tuple(field, direction, rest...);
        [&pack, &self]<std::size_t... I>(std::index_sequence<I...>) {
            (self.orderTerms.emplace_back(
                     refl::FieldWrapper::create(std::get<I * 2>(pack)), std::get<I * 2 + 1>(pack), Collation::NONE
             ),
             ...);
        }(std::make_index_sequence<(sizeof...(rest) + 2) / 2>{});

        return std::forward<Self>(self);
    }

    // C++26 multiple field ordering with compile-time validation and optimization
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::order_by(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time field validation for all fields
        static_assert(sizeof...(fields) <= 16, "Too many ORDER BY fields (max 16 for performance)");

        // Field validation will be handled by FieldWrapper::create

        // C++26 compile-time capacity optimization
        constexpr auto field_count = sizeof...(fields);
        self.orderTerms.reserve(self.orderTerms.size() + field_count);

        // C++26 fold expression with perfect forwarding
        (self.orderTerms.emplace_back(refl::FieldWrapper::create(fields), true, Collation::NONE), ...);

        return std::forward<Self>(self);
    }

    // C++26 compile-time field-collation pairs implementation
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::order_by(this Self&& self, auto field, auto collation)
        requires std::same_as<decltype(collation), Collation>
    {
        // Validate field type is sortable
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        using FieldType = typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // Runtime collation validation - will be handled at execution time by SQL engine

        // Add order term with default ascending direction
        self.orderTerms.emplace_back(refl::FieldWrapper::create(field), true, collation);

        return std::forward<Self>(self);
    }

    // C++26 compile-time full ORDER BY with field-direction-collation triplets
    template <typename T>
    template <typename Self>
    constexpr auto&& QuerySet<T>::order_by(this Self&& self, auto field, auto direction, auto collation, auto... rest)
        requires std::same_as<decltype(direction), bool> && std::same_as<decltype(collation), Collation>
    {
        // C++26 compile-time validation
        static_assert(
                sizeof...(rest) % 3 == 0,
                "Must provide field-direction-collation triplets (field, bool, collation, ...)"
        );
        static_assert((sizeof...(rest) / 3 + 1) <= 6, "Too many ORDER BY triplets (max 6 for performance)");

        // Validate field is sortable
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        using FieldType = typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // Runtime collation validation - will be handled at execution time by SQL engine

        // Optimize container capacity
        constexpr auto total_triplets = sizeof...(rest) / 3 + 1;
        self.orderTerms.reserve(self.orderTerms.size() + total_triplets);

        // Process all field-direction-collation triplets with fold expression
        auto process_triplets = [&self]<typename... Args>(Args&&... args) {
            auto process_triplet = [&self](auto&& f, auto&& d, auto&& c) {
                self.orderTerms.emplace_back(refl::FieldWrapper::create(f), d, c);
            };

            // Process triplets using parameter pack expansion
            auto pack = std::make_tuple(std::forward<Args>(args)...);
            [&pack, &process_triplet]<std::size_t... I>(std::index_sequence<I...>) {
                (process_triplet(std::get<I * 3>(pack), std::get<I * 3 + 1>(pack), std::get<I * 3 + 2>(pack)), ...);
            }(std::make_index_sequence<sizeof...(Args) / 3>{});
        };

        process_triplets(field, direction, collation, rest...);
        return std::forward<Self>(self);
    }

    // GROUP_CONCAT_ORDER implementation
    template <typename T>
    template <typename Self, auto OrderField, auto FirstField, auto... RestFields, bool Distinct>
    constexpr auto&& QuerySet<T>::group_concat_with_order_impl(
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
    template <typename Self>
    auto&&
    QuerySet<T>::group_concat(this Self&& self, auto field, utils::fixed_string<32> alias, utils::fixed_string<8> separator, bool distinct) {
        // Extract field information at compile time
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        constexpr auto field_name = extract_field_name<MemberPtr>();
        constexpr auto table_name = extract_class_name<T>();

        // Build SQL with runtime parameters
        std::string sql = "GROUP_CONCAT(";
        if (distinct) {
            sql += "DISTINCT ";
        }
        sql += std::string{table_name};
        sql += ".";
        sql += std::string{field_name};
        sql += " SEPARATOR '";
        sql += separator.c_str();
        sql += "')";

        if (alias.c_str()[0] != '\0') {
            sql += " AS \"";
            sql += alias.c_str();
            sql += "\"";
        }

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql));
        return std::forward<Self>(self);
    }

    // SELECT ONE implementation (returns single object with LIMIT 1)
    template <typename T>
    ExpectedT<T> QuerySet<T>::select_one()
        requires refl::reflectable<T>
    {
        return std::move(*this).limit(1).select_all().and_then([](const auto& rows) -> ExpectedT<T> {
            if (rows.empty()) {
                return std::unexpected("No results found for select_one query");
            }
            return rows[0];
        });
    }

    // SELECT ALL implementation
    template <typename T>
    ExpectedVectorT<T> QuerySet<T>::select_all()
        requires refl::reflectable<T>
    {
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
    template <typename T>
    [[nodiscard]] auto QuerySet<T>::select_values() -> ExpectedValueVectorMap
        requires refl::reflectable<T>
    {
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
