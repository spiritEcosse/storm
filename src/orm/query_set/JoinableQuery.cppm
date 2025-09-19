module;

// Module global fragment - third-party C headers (macros not exported by modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.joinable_query;

// Import required modules
import storm.aggregate_query;
import storm.join_utils;

// Import standard header units
import <string>;
import <utility>;
import <vector>;
import <type_traits>;
import <concepts>;

export namespace storm {

    template <class T> class JoinableQuery : public AggregateQuery<T> {
      protected:
        std::vector<JoinWrapper> join_clauses;

      public:
        // Inherit constructors
        using AggregateQuery<T>::AggregateQuery;

        // Copy constructor
        JoinableQuery(const JoinableQuery& other)
            : AggregateQuery<T>(other)
            , join_clauses(other.join_clauses) {}

        // Move constructor
        JoinableQuery(JoinableQuery&& other) noexcept = default;

        // Copy assignment operator
        JoinableQuery& operator=(const JoinableQuery& other) {
            if (this != &other) {
                AggregateQuery<T>::operator=(other);
                join_clauses = other.join_clauses;
            }
            return *this;
        }

        // Move assignment operator
        JoinableQuery& operator=(JoinableQuery&& other) noexcept = default;

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

      protected:
        // Override AggregateQuery's build_select_options to include join clauses
        [[nodiscard]] SelectOptions build_select_options() const {
            return SelectOptions{
                .joins           = join_clauses,
                .distinct_fields = this->distinctFields,
                .only_fields     = this->onlyFields,
                .functions_set   = this->functionsSet,
                .order_terms     = this->orderTerms,
                .group_by_fields = this->groupByFields,
                .limit           = this->_limit,
                .offset          = this->_offset,
                .where_clause    = this->_whereExpression,
            };
        }
    };

} // namespace storm