#pragma once
#include <refl.hpp>
#include <string>
#include <string_view>
#include <type_traits>

// Type trait to check if a member is a field
template <typename T>
using is_field = refl::trait::is_field<T>;

// Type trait to check if a member is a function
template <typename T>
using is_function = refl::trait::is_function<T>;

// Helper to get member type
template <typename T>
using member_value_type = typename T::value_type;

// Helper to get member name - now returns string_view for better performance
template <typename Member>
constexpr std::string_view get_member_name(const Member& member) noexcept {
    return member.name;
}

// Helper to convert string_view to string when needed
template <typename Member>
std::string get_member_name_str(const Member& member) {
    return std::string(member.name);
}

// Utility functions that should be in a separate utils namespace/header
namespace utils {
    std::string to_lower(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        return str;
    }
}

template<typename T>
class Reflect {
public:
    using reflected_type = T;
    
    Reflect() = default;
    virtual ~Reflect() = default;
    
    // Prevent copying
    Reflect(const Reflect&) = delete;
    Reflect& operator=(const Reflect&) = delete;
    
    // Allow moving
    Reflect(Reflect&&) noexcept = default;
    Reflect& operator=(Reflect&&) noexcept = default;

    template <typename Members, typename Fn>
    static constexpr void for_each_member(Members&& members, Fn&& fn) {
        refl::util::for_each(std::forward<Members>(members), std::forward<Fn>(fn));
    }

    // Helper to get reflected type information for T
    static constexpr auto get_reflected_type() noexcept {
        return refl::reflect<T>();
    }

    // Function to get the reflected members of the type
    static constexpr auto get_reflected_members() noexcept {
        return get_reflected_type().members;
    }
    
    // Get struct name as string_view for better performance
    static constexpr std::string_view get_struct_name_view() noexcept {
        return get_reflected_type().name;
    }
    
    // Get struct name as lowercase string
    static std::string get_struct_name() {
        return utils::to_lower(std::string(get_reflected_type().name));
    }
    
    // Get number of members
    static constexpr std::size_t get_member_count() noexcept {
        return get_reflected_members().size;
    }
    
    // Check if type has any fields
    static constexpr bool has_fields() noexcept {
        bool has_field = false;
        for_each_member(get_reflected_members(), [&](const auto& member) {
            if constexpr (is_field<decltype(member)>::value) {
                has_field = true;
            }
        });
        return has_field;
    }
    
    // Get field count
    static constexpr std::size_t get_field_count() noexcept {
        std::size_t count = 0;
        for_each_member(get_reflected_members(), [&](const auto& member) {
            if constexpr (is_field<decltype(member)>::value) {
                ++count;
            }
        });
        return count;
    }
    
    // Template method to apply a function to all fields of an instance
    template<typename Fn>
    static void for_each_field(T& instance, Fn&& fn) {
        for_each_member(get_reflected_members(), [&](const auto& member) {
            if constexpr (is_field<decltype(member)>::value) {
                fn(member, member(instance));
            }
        });
    }
    
    // Const version
    template<typename Fn>
    static void for_each_field(const T& instance, Fn&& fn) {
        for_each_member(get_reflected_members(), [&](const auto& member) {
            if constexpr (is_field<decltype(member)>::value) {
                fn(member, member(instance));
            }
        });
    }
    
    // Find a field by name (returns optional-like result)
    template<typename FieldName>
    static constexpr auto find_field(FieldName&& name) {
        constexpr auto members = get_reflected_members();
        return refl::util::find_one(members, [&](const auto& member) {
            return is_field<decltype(member)>::value && 
                   member.name == std::forward<FieldName>(name);
        });
    }
};
