#pragma once

// Legacy header placeholder; use C++23 modules (import storm.field).
import <string>;
import <memory>;
import <type_traits>;
import <concepts>;
import <span>;
import <format>;
import <utility>;
import <ranges>;
import <optional>;
import storm.reflect;
import storm.utils;
import storm.expression;
import storm.condition;
import storm.logical_expression;
import storm.parameter_binder;

namespace storm {

    // Forward declaration of Where class
    class Where;

    // Field proxy for fluent syntax - Compile-time only version (C++23)
    template <auto MemberPtr>
        requires std::is_member_object_pointer_v<decltype(MemberPtr)>
    class Field {
      private:
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        using FieldType = typename member_pointer_type<decltype(MemberPtr)>::type;

        // All compile-time constants
        static constexpr auto type_name   = refl::reflect<ClassType>::name();
        static constexpr auto member_name = []() constexpr {
            std::string_view result;
            refl::reflect<ClassType>::for_each_member([&]<size_t I>(auto member_desc) {
                if constexpr (member_desc.member_ptr == MemberPtr) {
                    result = member_desc.get_name();
                }
            });
            return result;
        }();

        // Pre-computed field name at compile time
        static constexpr auto field_name = []() constexpr {
            return utils::formatFieldName(std::string{type_name}, std::string{member_name});
        }();

        Collation collation_ = Collation::NONE;

      public:
        // C++23: Default constructor
        constexpr explicit Field(Collation collation = Collation::NONE) noexcept : collation_(collation) {}

        // Constructor that takes a member pointer for deduction
        template <auto MP>
            requires std::is_same_v<decltype(MP), decltype(MemberPtr)>
        constexpr explicit Field(decltype(MP), Collation collation = Collation::NONE) noexcept
            : collation_(collation) {}

        // Static accessors for compile-time information
        static consteval std::string_view get_member_name() noexcept {
            return member_name;
        }
        static consteval std::string_view get_field_name() noexcept {
            return field_name;
        }

        static consteval std::string_view get_type_name() noexcept {
            return type_name;
        }

        // Comparison operators - now using compile-time field name
        template <typename T> constexpr Where operator==(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::EQ, std::forward<T>(value), collation_)
            );
        }

        template <typename T> constexpr Where operator!=(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::NE, std::forward<T>(value), collation_)
            );
        }

        template <typename T> constexpr Where operator>(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::GT, std::forward<T>(value), collation_)
            );
        }

        template <typename T> constexpr Where operator<(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::LT, std::forward<T>(value), collation_)
            );
        }

        template <typename T> constexpr Where operator>=(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::GE, std::forward<T>(value), collation_)
            );
        }

        template <typename T> constexpr Where operator<=(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::LE, std::forward<T>(value), collation_)
            );
        }

        // Special operators
        template <typename T> constexpr Where like(T&& pattern) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::LIKE, std::forward<T>(pattern), collation_)
            );
        }

        constexpr Where is_null() const {
            return Where(std::make_unique<Condition>(std::string{field_name}, Op::IS, std::nullopt, collation_));
        }

        constexpr Where is_not_null() const {
            return Where(std::make_unique<Condition>(std::string{field_name}, Op::IS_NOT, std::nullopt, collation_));
        }

        template <typename T> constexpr Where is(T&& value) const {
            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::IS, std::forward<T>(value), collation_)
            );
        }

        template <typename T1, typename T2> constexpr Where between(T1&& value1, T2&& value2) const {
            return Where(
                    std::make_unique<Condition>(
                            std::string{field_name}, std::forward<T1>(value1), std::forward<T2>(value2), collation_
                    )
            );
        }

        // IN operator for multiple values - C++23 enhanced
        template <typename Container> constexpr Where in(const Container& values) const {
            if consteval {
                // C++23: Compile-time branch for empty containers
                if (std::ranges::empty(values)) {
                    return Where(
                            std::make_unique<Condition>(
                                    std::string{field_name}, Op::EQ, "__IMPOSSIBLE_VALUE__", collation_
                            )
                    );
                }
            }

            auto  it     = std::ranges::begin(values);
            Where result = Field<MemberPtr>(collation_) == *it;
            ++it;

            for (auto end = std::ranges::end(values); it != end; ++it) {
                Where next_condition = Field<MemberPtr>(collation_) == *it;
                result               = result or next_condition; // NOSONAR
            }

            return result;
        }

        // IN operator for initializer lists - C++23 enhanced
        template <typename T> constexpr Where in(std::initializer_list<T> values) const {
            return in(std::span<const T>(values));
        }

        // C++23: Concept for string view convertible types
        template <typename T>
        concept StringViewConvertible = requires { std::string_view{std::declval<T>()}; };

        // String pattern matching helpers - C++23 enhanced
        template <typename T>
        constexpr Where startswith(T&& prefix) const
            requires StringViewConvertible<T>
        {
            // C++23: Use constexpr string formatting
            // This will be evaluated at compile-time when possible
            // and fall back to runtime evaluation when necessary
            auto pattern = std::format("{}%", std::string_view{std::forward<T>(prefix)});

            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::LIKE, std::move(pattern), collation_)
            );
        }

        template <typename T>
        constexpr Where endswith(T&& suffix) const
            requires StringViewConvertible<T>
        {
            // C++23: Use constexpr string formatting
            // This will be evaluated at compile-time when possible
            // and fall back to runtime evaluation when necessary
            auto pattern = std::format("%{}", std::string_view{std::forward<T>(suffix)});

            return Where(
                    std::make_unique<Condition>(std::string{field_name}, Op::LIKE, std::move(pattern), collation_)
            );
        }

        // Collation methods - C++23 enhanced with constexpr
        constexpr Field<MemberPtr> collate_binary() const noexcept {
            return Field<MemberPtr>(Collation::BINARY);
        }

        constexpr Field<MemberPtr> collate_nocase() const noexcept {
            return Field<MemberPtr>(Collation::NOCASE);
        }

        constexpr Field<MemberPtr> collate_rtrim() const noexcept {
            return Field<MemberPtr>(Collation::RTRIM);
        }

        // C++23: Deducing this for perfect forwarding (if needed)
        template <typename Self> constexpr auto with_collation(this Self&& self, Collation collation) noexcept {
            return Field<MemberPtr>(collation);
        }
    };

    // C++23: Template deduction guide for member pointer constructor
    template <auto MemberPtr> Field(decltype(MemberPtr), Collation) -> Field<MemberPtr>;

    // Original deduction guide (kept for backward compatibility)
    template <auto MemberPtr> Field(Collation) -> Field<MemberPtr>;

    // C++23: Concept for field validation
    template <typename T>
    concept ValidFieldType = requires {
        typename T::ClassType;
        typename T::FieldType;
        T::get_field_name();
        T::get_member_name();
        T::get_type_name();
    };

} // namespace storm
