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

// ============================================================================
// CONCEPTS FOR STATEMENT BINDING
// ============================================================================
export template <typename Statement>
concept bindable_statement = requires(Statement& stmt, int idx, int value) {
    { stmt.bind(idx, value) } -> std::same_as<std::expected<void, std::string>>;
};

// ============================================================================
// STATEMENT BINDING
// ============================================================================
template <bindable_statement Statement>
auto bind_variant(Statement& stmt, int param_index, const storm::SqlValue& value) -> std::expected<void, std::string> {
    return std::visit([&](const auto& v) { return stmt.bind(param_index, v); }, value);
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

        // Generic member iteration
        template <typename F, typename... Args> static consteval void iterate_members(F&& func, Args&&... args) {
            []<size_t... Is>(std::index_sequence<Is...>, auto&& f, auto&&... a) {
                (f(std::get<Is>(members_tuple{}), std::forward<decltype(a)>(a)...), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func), std::forward<Args>(args)...);
        }

        // Simplified visit methods using generic iteration
        template <typename F> static consteval void for_each_member(F&& func) {
            iterate_members([&](auto member) { func(member); });
        }

        template <typename F> static consteval void for_each_member_with_name(F&& func) {
            iterate_members([&](auto member) { func(member, member.get_name()); });
        }

        template <typename Obj, typename F> static consteval void visit_members(Obj&& obj, F&& func) {
            iterate_members([&](auto member) { func(member.get_name(), member.get(std::forward<Obj>(obj))); });
        }

        template <typename Obj, typename F> static consteval void visit_members_with_descriptor(Obj&& obj, F&& func) {
            iterate_members([&](auto member) { func(member, member.get(std::forward<Obj>(obj))); });
        }

        static consteval auto find_member_index(std::string_view name) -> std::optional<size_t> {
            auto names = get_member_names();
            auto it    = std::ranges::find(names, name);
            return it != names.end() ? std::optional{std::distance(names.begin(), it)} : std::nullopt;
        }
    };

} // namespace refl::meta

// ============================================================================
// C++26-READY STATIC REFLECTION (TRANSITION)
// ============================================================================
namespace std::meta {
    struct info {
        const char* name;
        consteval info(const char* n) : name(n) {}
    };

    template <auto MemberPtr> consteval std::string_view extract_field_name_impl() {
        constexpr std::string_view full_name = __PRETTY_FUNCTION__;

        constexpr auto find_member_name = [](std::string_view str) constexpr {
            // Find last :: and extract member name
            auto pos = str.rfind("::");
            if (pos != std::string_view::npos) {
                auto start = pos + 2;
                auto end   = str.find_first_of("]> )", start);
                return str.substr(start, end - start);
            }
            return std::string_view{"unknown_field"};
        };

        return find_member_name(full_name);
    }

    consteval std::string_view name_of(info reflected) {
        return std::string_view(reflected.name);
    }

    template <auto MemberPtr> consteval info reflect(auto) {
        return info{extract_field_name_impl<MemberPtr>()};
    }

    template <typename T> consteval info reflect_type() {
        return info{__PRETTY_FUNCTION__};
    }
} // namespace std::meta

// Helper functions
template <auto MemberPtr> consteval auto make_reflection() {
    return std::meta::reflect<MemberPtr>(MemberPtr);
}

template <auto MemberPtr> consteval std::string_view extract_field_name() {
    return std::meta::name_of(make_reflection<MemberPtr>());
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

        template <size_t I> static consteval auto get_member() {
            return descriptor::template get_member<I>();
        }

        // Member iteration delegates
        template <typename F> static consteval void for_each_member(F&& func) {
            descriptor::for_each_member(std::forward<F>(func));
        }

        template <typename F> static consteval void for_each_member_with_name(F&& func) {
            descriptor::for_each_member_with_name(std::forward<F>(func));
        }

        // Visit delegates - unified for const and non-const
        template <typename Obj, typename F> static consteval void visit_members(Obj&& obj, F&& func) {
            descriptor::visit_members(std::forward<Obj>(obj), std::forward<F>(func));
        }

        template <typename Obj, typename F> static consteval void visit_members_with_descriptor(Obj&& obj, F&& func) {
            descriptor::visit_members_with_descriptor(std::forward<Obj>(obj), std::forward<F>(func));
        }

        // Runtime utilities

        static bool is_primary_key(std::string_view field_name) {
            return field_name == "id";
        }
    };

    // Field member utilities
    template <auto MemberPtr> consteval std::string_view get_field_name() {
        return std::meta::name_of(make_reflection<MemberPtr>());
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

        template <auto MemberPtr>
            requires std::is_member_pointer_v<decltype(MemberPtr)>
        static FieldWrapper create() {
            static constexpr auto fs = FieldMember<MemberPtr>::get_full_field_name();
            return {.full_name = fs.view(), .field_member = FieldMember<MemberPtr>{}};
        }

        std::string_view view() const noexcept {
            return full_name;
        }
    };

} // namespace refl