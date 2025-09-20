module;

// Define the module
export module storm.reflect;
import storm.utils;
import storm.core_types;

// Import standard header units (C++26 ready)
import <vector>;
import <string>;
import <string_view>;
import <concepts>;
import <type_traits>;
import <typeinfo>;
import <unordered_map>;
import <iostream>;
import <ranges>;
import <algorithm>;
import <cctype>;
import <functional>;
import <any>;
import <expected>;
import <print>;
import <span>;
import <array>;
import <optional>;
import <format>;
import <variant>;

// Forward declarations
namespace storm {
    template <auto MemberPtr> struct Field;
}

// ============================================================================
// COMPILE-TIME REFLECTION METADATA
// ============================================================================
export namespace refl::meta {

    // Helper to extract class and member types from member pointer
    template <typename T> struct member_pointer_traits;

    template <typename Class, typename Member> struct member_pointer_traits<Member Class::*> {
        using class_type  = Class;
        using member_type = Member;
    };

    // Compile-time member descriptor
    template <auto MemberPtr, auto Name> struct member_descriptor {
        static constexpr auto name       = Name;
        static constexpr auto member_ptr = MemberPtr;

        using traits      = member_pointer_traits<decltype(MemberPtr)>;
        using class_type  = typename traits::class_type;
        using member_type = typename traits::member_type;

        static consteval auto get_name() {
            return name;
        }
        static consteval const char* type_name() {
            return typeid(member_type).name();
        }

        // Generic getter/setter
        template <typename Obj> static consteval auto& get(Obj&& obj) {
            return std::forward<Obj>(obj).*member_ptr;
        }

        template <typename U>
        static consteval auto set(class_type& obj, U&& value) -> std::expected<void, std::string> {
            if constexpr (std::is_assignable_v<member_type&, U>) {
                obj.*member_ptr = std::forward<U>(value);
                return {};
            } else {
                return std::unexpected{std::format("Cannot assign value to member '{}'", get_name())};
            }
        }
    };

    // Compile-time type descriptor
    template <typename T, auto TypeName, typename... Members> struct type_descriptor {
        using type                         = T;
        static constexpr auto name         = TypeName;
        static constexpr auto member_count = sizeof...(Members);
        using members_tuple                = std::tuple<Members...>;

        static consteval auto get_name() {
            return name;
        }
        static consteval size_t get_member_count() {
            return member_count;
        }

        template <size_t I> static consteval auto get_member() {
            static_assert(I < member_count, "Member index out of bounds");
            return std::get<I>(members_tuple{});
        }

        static consteval auto get_member_names() {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array<std::string_view, member_count>{std::get<Is>(members_tuple{}).get_name()...};
            }(std::make_index_sequence<member_count>{});
        }

        // Essential member iteration methods
        template <typename F> static constexpr void for_each_member_with_index(F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, auto&& f) {
                (f.template operator()<Is>(std::get<Is>(members_tuple{})), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
        }

        // Alias for backwards compatibility
        template <typename F> static consteval void iterate_members(F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, auto&& f) {
                (f(std::get<Is>(members_tuple{})), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
        }

        template <typename Obj, typename F> static constexpr void visit_members(Obj&& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, auto&& o, auto&& f) {
                (f(std::get<Is>(members_tuple{}).get_name(),
                   std::get<Is>(members_tuple{}).get(std::forward<decltype(o)>(o))),
                 ...);
            }(std::make_index_sequence<member_count>{}, std::forward<Obj>(obj), std::forward<F>(func));
        }
    };

} // namespace refl::meta

// ============================================================================
// SIMPLE FIELD NAME EXTRACTION
// ============================================================================
export template <auto MemberPtr> consteval std::string_view extract_field_name() {
    constexpr std::string_view full_name = __PRETTY_FUNCTION__;

    // Find last :: and extract member name
    auto pos = full_name.rfind("::");
    if (pos != std::string_view::npos) {
        auto start = pos + 2;
        auto end   = full_name.find_first_of("]> )", start);
        return full_name.substr(start, end - start);
    }
    return std::string_view{"unknown_field"};
}

export template <typename T> consteval std::string_view extract_class_name() {
    constexpr std::string_view full_name = __PRETTY_FUNCTION__;

    // Find the class name in the pretty function signature
    auto pos = full_name.find("T = ");
    if (pos != std::string_view::npos) {
        auto start = pos + 4;
        auto end   = full_name.find_first_of("]>;", start);
        return full_name.substr(start, end - start);
    }
    return std::string_view{"unknown_class"};
}

// ============================================================================
// REFLECTION REGISTRY
// ============================================================================
export namespace refl {

    // Specialization point for types
    template <typename T> struct type_info {
        static constexpr bool has_reflection = false;
    };

    // Concept for reflectable types
    template <typename T>
    concept reflectable = type_info<T>::has_reflection;

    // Main reflection interface
    template <reflectable T> struct reflect {
        using descriptor = typename type_info<T>::descriptor;
        using type       = T;

        // Basic metadata
        static consteval auto name() {
            return descriptor::get_name();
        }
        static consteval size_t member_count() {
            return descriptor::get_member_count();
        }
        static consteval auto member_names() {
            return descriptor::get_member_names();
        }

        template <typename Predicate>
        static consteval auto get_member_names_if(Predicate&& predicate) {
            return []<size_t... Is>(std::index_sequence<Is...>, auto&& pred) consteval {
                // Create fixed-size array with conditional inclusion
                // Empty string_view for filtered-out members
                // Use tuple element type directly instead of creating temporary
                using tuple_t = typename descriptor::members_tuple;
                return std::array<std::string_view, sizeof...(Is)>{
                    (pred(std::tuple_element_t<Is, tuple_t>{})
                        ? std::tuple_element_t<Is, tuple_t>::name
                        : std::string_view{})...
                };
            }(std::make_index_sequence<descriptor::member_count>{},
              std::forward<Predicate>(predicate));
        }

        template <size_t I> static consteval auto get_member() {
            return descriptor::template get_member<I>();
        }

        // Essential member iteration delegates
        template <typename F> static constexpr void for_each_member_with_index(F&& func) {
            descriptor::for_each_member_with_index(std::forward<F>(func));
        }

        template <typename Obj, typename F> static constexpr void visit_members(Obj&& obj, F&& func) {
            descriptor::visit_members(std::forward<Obj>(obj), std::forward<F>(func));
        }

        // Runtime utilities

        static consteval bool is_primary_key(std::string_view field_name) {
            return field_name == "id";
        }
    };

    // Field member utilities
    template <auto MemberPtr> consteval std::string_view get_field_name() {
        return extract_field_name<MemberPtr>();
    }

    template <auto MemberPtr> consteval auto get_full_field_name() {
        using ClassType = typename meta::member_pointer_traits<decltype(MemberPtr)>::class_type;
        return storm::utils::formatFieldName(reflect<ClassType>::name(), get_field_name<MemberPtr>());
    }

    // FieldMember for type-safe member pointer storage
    template <auto MemberPtr> struct FieldMember {
        static constexpr auto member_ptr = MemberPtr;

        using member_ptr_type = decltype(MemberPtr);
        using traits          = meta::member_pointer_traits<member_ptr_type>;
        using member_type     = typename traits::member_type;
        using class_type      = typename traits::class_type;

        static consteval auto get_full_field_name() {
            return refl::get_full_field_name<MemberPtr>();
        }

        static consteval auto get_field_name() {
            return refl::get_field_name<MemberPtr>();
        }
    };

    // Type-erased field wrapper
    struct FieldWrapper {
        std::string_view full_name{};
        std::any         field_member;

        // Overload for storm::Field objects
        template <typename FieldType>
        static FieldWrapper create(FieldType field)
            requires requires { FieldType::member_ptr; }
        {
            // Extract the field name at compile time but store at runtime
            constexpr auto        member_ptr = FieldType::member_ptr;
            static constexpr auto fs         = FieldMember<member_ptr>::get_full_field_name();
            return {.full_name = fs.view(), .field_member = FieldMember<member_ptr>{}};
        }

        std::string_view view() const noexcept {
            return full_name;
        }
    };

} // namespace refl