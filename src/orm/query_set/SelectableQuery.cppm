module;

// Module global fragment - third-party C headers (macros not exported by modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.selectable_query;

// Import required modules
import storm.base_query;
import storm.core_types; // For SqlValue, ValueMap, etc.
import storm.statement.select;
import storm.utils;
import storm.reflect; // For FieldWrapper

// Import standard header units
import <string>;
import <utility>;
import <vector>;
import <expected>;
import <type_traits>;
import <utility>;
import <ranges>;
import <flat_map>;
import <functional>;

export namespace storm {
    // Use the canonical SqlValue type from storm.core_types to avoid redundancy
    using ValueMap                              = std::flat_map<std::string, SqlValue, std::less<>>;
    using ValueVectorMap                        = std::vector<ValueMap>;
    using ExpectedValueVectorMap                = std::expected<ValueVectorMap, std::string>;
    template <typename T> using ExpectedT       = std::expected<T, std::string>;
    template <typename T> using ExpectedVectorT = std::expected<std::vector<T>, std::string>;

    template <class T> class SelectableQuery : public BaseQuery<T> {
      protected:
        std::vector<std::pair<refl::FieldWrapper, utils::fixed_string<32>>> onlyFields;

      public:
        // Inherit constructors
        using BaseQuery<T>::BaseQuery;

        // Copy constructor
        SelectableQuery(const SelectableQuery& other) : BaseQuery<T>(other), onlyFields(other.onlyFields) {}

        // Move constructor
        SelectableQuery(SelectableQuery&& other) noexcept = default;

        // Copy assignment operator
        SelectableQuery& operator=(const SelectableQuery& other) {
            if (this != &other) {
                BaseQuery<T>::operator=(other);
                onlyFields = other.onlyFields;
            }
            return *this;
        }

        // Move assignment operator
        SelectableQuery& operator=(SelectableQuery&& other) noexcept = default;

        // ONLY API (C++26 upgraded declarations with function parameter deduction)
        // Simple version: .only(field1, field2, ...)
        template <typename Self>
        constexpr auto&& only(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0);

        // Overloaded version for field-alias pairs: .only(field, alias, field, alias, ...)
        template <typename Self>
        constexpr auto&& only(this Self&& self, auto first_field, auto first_alias, auto... rest)
            requires(sizeof...(rest) % 2 == 0) && std::is_member_pointer_v<decltype(first_field)> &&
                    (!std::is_member_pointer_v<decltype(first_alias)>);

        // C++26 SELECT API with compile-time query validation and type safety
        ExpectedVectorT<T> select_all()
            requires refl::reflectable<T>;
        ExpectedT<T> select_one()
            requires refl::reflectable<T>;
        ExpectedValueVectorMap select_values()
            requires refl::reflectable<T>;

      protected:
        // Helper method to build SelectOptions
        [[nodiscard]] SelectOptions build_select_options() const {
            return SelectOptions{
                    .distinct_fields = this->distinctFields,
                    .only_fields     = onlyFields,
                    .order_terms     = this->orderTerms,
                    .group_by_fields = this->groupByFields,
                    .limit           = this->_limit,
                    .offset          = this->_offset,
                    .where_clause    = this->_whereExpression,
            };
        }
    };

    // C++26 ONLY implementation - Simple version: .only(field1, field2, ...)
    template <typename T>
    template <typename Self>
    constexpr auto&& SelectableQuery<T>::only(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0)
    {
        // C++26 compile-time validation
        static_assert(sizeof...(fields) <= 20, "Too many ONLY fields (max 20 for performance)");

        // Optimize container capacity
        constexpr auto field_count = sizeof...(fields);
        self.onlyFields.reserve(self.onlyFields.size() + field_count);

        // C++26 fold expression with field wrapper creation (no aliases)
        (self.onlyFields.emplace_back(refl::FieldWrapper::create(fields), utils::fixed_string<32>{}), ...);

        return std::forward<Self>(self);
    }

    // C++26 ONLY implementation - Overloaded version for field-alias pairs: .only(field, alias, field, alias, ...)
    template <typename T>
    template <typename Self>
    constexpr auto&& SelectableQuery<T>::only(this Self&& self, auto first_field, auto first_alias, auto... rest)
        requires(sizeof...(rest) % 2 == 0) && std::is_member_pointer_v<decltype(first_field)> &&
                (!std::is_member_pointer_v<decltype(first_alias)>)
    {
        // C++26 compile-time validation
        constexpr auto total_pairs = (sizeof...(rest) + 2) / 2;
        static_assert(total_pairs <= 20, "Too many field-alias pairs (max 20 pairs for performance)");
        static_assert(sizeof...(rest) % 2 == 0, "Must provide field-alias pairs");

        // Extract field-alias pairs at compile time
        constexpr auto pairs = std::make_tuple(first_field, first_alias, rest...);

        // Process pairs using index sequence
        return [&]<std::size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
            constexpr auto field_count = (sizeof...(rest) + 2) / 2;
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
        }(std::make_index_sequence<(sizeof...(rest) + 2) / 2>{});
    }

    // SELECT ONE implementation (returns single object with LIMIT 1)
    template <typename T>
    ExpectedT<T> SelectableQuery<T>::select_one()
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
    ExpectedVectorT<T> SelectableQuery<T>::select_all()
        requires refl::reflectable<T>
    {
        auto opts = build_select_options();
        return SelectStatement<T>(this->conn, std::move(opts)).execute_objects();
    }

    // SELECT VALUES implementation (returns dictionary-like data)
    template <typename T>
    [[nodiscard]] auto SelectableQuery<T>::select_values() -> ExpectedValueVectorMap
        requires refl::reflectable<T>
    {
        // Pre-calculate capacity hints
        const auto expected_size = this->estimate_result_size();

        auto opts = build_select_options();
        auto stmt = SelectStatement<T>(this->conn, std::move(opts));
        stmt.reserve_hint(expected_size);
        return stmt.execute_values();
    }

} // namespace storm