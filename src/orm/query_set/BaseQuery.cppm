module;

// Module global fragment - third-party C headers (macros not exported by modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.base_query;

// Import required modules
import storm.core_types; // For SqlValue, Op, Collation
import storm.condition;  // For storm::Condition
import storm.where;
import storm.field; // For Field class

// Storm modules
import storm.connection;
import storm.reflect;
import storm.utils;
import storm.type_traits;

// Import standard header units in the global module fragment
import <string>;
import <utility>;
import <memory>;
import <vector>;
import <expected>;
import <type_traits>;
import <optional>;
import <concepts>;
import <string_view>;
import <array>;
import <functional>;

export namespace storm {

    template <typename T>
    concept Sortable = requires(T a, T b) {
        { a < b } -> std::convertible_to<bool>;
    };

    template <class T> class BaseQuery {
      protected:
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

        std::vector<std::tuple<refl::FieldWrapper, bool, Collation>> orderTerms;

        // Helper to estimate result size for pre-allocation
        [[nodiscard]] constexpr std::size_t estimate_result_size() const noexcept {
            if (query_flags.limit > 0)
                return query_flags.limit;
            if (query_flags.has_aggregates)
                return 1;
            return 100; // Default estimate
        }

      public:
        // Constructors
        BaseQuery() = default;
        explicit BaseQuery(std::shared_ptr<Connection> connection) : conn(std::move(connection)) {}

        // Copy constructor - deep copy unique_ptr members where needed
        BaseQuery(const BaseQuery& other)
            : conn(other.conn)
            , _whereExpression(other._whereExpression)
            , query_flags(other.query_flags)
            , orderTerms(other.orderTerms) {
            // Value semantics: vectors copy directly
            distinctFields = other.distinctFields;
            groupByFields  = other.groupByFields;
        }

        // Move constructor
        BaseQuery(BaseQuery&& other) noexcept = default;

        // Copy assignment operator
        BaseQuery& operator=(const BaseQuery& other) {
            if (this != &other) {
                BaseQuery temp(other);   // Use copy constructor
                *this = std::move(temp); // Use move assignment
            }
            return *this;
        }

        // Move assignment operator
        BaseQuery& operator=(BaseQuery&& other) noexcept = default;

        // WHERE API (C++26 upgraded declarations)
        template <typename Self> constexpr auto&& where(this Self&& self, const storm::Where& where_clause);
        template <typename Self> constexpr auto&& where(this Self&& self, storm::Where&& where_clause);

        // WHERE with multiple conditions using C++26 fold expressions
        template <typename Self, typename... Conditions>
        constexpr auto&& where(this Self&& self, Conditions&&... conditions);

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

        // GROUP BY API (C++26 upgraded declarations with function parameter deduction)
        template <typename Self>
        constexpr auto&& group_by(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        template <typename Self> auto&& limit(this Self&& self, int limit_value);
        template <typename Self> auto&& offset(this Self&& self, int offset_value);

      protected:
        // Deducing this for perfect forwarding
        template <typename Self> static constexpr decltype(auto) self_cast(Self&& self) {
            return std::forward<Self>(self);
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
    };

    // C++26 WHERE with fold expressions and perfect forwarding for multiple conditions
    template <typename T>
    template <typename Self, typename... Conditions>
    constexpr auto&& BaseQuery<T>::where(this Self&& self, Conditions&&... conditions) {
        static_assert(sizeof...(conditions) > 0, "where with multiple conditions requires at least one condition");

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
    constexpr auto&& BaseQuery<T>::where(this Self&& self, const storm::Where& where_clause) {
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
    constexpr auto&& BaseQuery<T>::where(this Self&& self, storm::Where&& where_clause) {
        if (self._whereExpression) {
            // Combine with existing WHERE using AND
            self._whereExpression = storm::Where{*self._whereExpression && std::move(where_clause)};
        } else {
            self._whereExpression = std::move(where_clause);
        }
        return std::forward<Self>(self);
    }

    // C++26 DISTINCT implementation with function parameter deduction
    template <typename T>
    template <typename Self>
    constexpr auto&& BaseQuery<T>::distinct(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 10, "Too many DISTINCT fields (max 10 for performance)");

        // Optimize container capacity
        constexpr auto field_count = sizeof...(fields);
        self.distinctFields.reserve(self.distinctFields.size() + field_count);

        // C++26 fold expression with compile-time field wrapper creation
        (self.distinctFields.emplace_back(refl::FieldWrapper::create(fields)), ...);

        return std::forward<Self>(self);
    }

    // C++26 GROUP BY implementation with function parameter deduction
    template <typename T>
    template <typename Self>
    constexpr auto&& BaseQuery<T>::group_by(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 8, "Too many GROUP BY fields (max 8 for performance)");

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
    template <typename T> template <typename Self> auto&& BaseQuery<T>::limit(this Self&& self, int limit_value) {
        self.query_flags.limit = limit_value;
        return std::forward<Self>(self);
    }

    template <typename T> template <typename Self> auto&& BaseQuery<T>::offset(this Self&& self, int offset_value) {
        self.query_flags.offset = offset_value;
        return std::forward<Self>(self);
    }

    // C++26 compile-time field-direction pairs ordering
    template <typename T>
    template <typename Self>
    constexpr auto&& BaseQuery<T>::order_by(this Self&& self, auto field, auto direction, auto... rest)
        requires std::same_as<decltype(direction), bool>
    {
        // Validate variadic arguments come in field-direction pairs
        static_assert(sizeof...(rest) % 2 == 0, "Must provide field-direction pairs");

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
    constexpr auto&& BaseQuery<T>::order_by(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time field validation for all fields
        static_assert(sizeof...(fields) <= 16, "Too many ORDER BY fields (max 16 for performance)");

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
    constexpr auto&& BaseQuery<T>::order_by(this Self&& self, auto field, auto collation)
        requires std::same_as<decltype(collation), Collation>
    {
        // Validate field type is sortable
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        using FieldType =
                typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

        // Add order term with default ascending direction
        self.orderTerms.emplace_back(refl::FieldWrapper::create(field), true, collation);

        return std::forward<Self>(self);
    }

    // C++26 compile-time full ORDER BY with field-direction-collation triplets
    template <typename T>
    template <typename Self>
    constexpr auto&& BaseQuery<T>::order_by(this Self&& self, auto field, auto direction, auto collation, auto... rest)
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
        using FieldType =
                typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
        static_assert(Sortable<FieldType>, "Field type must be sortable for ORDER BY");

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

} // namespace storm