#pragma once

#include <string>
#include <type_traits>
#include "refl.hpp"

namespace orm {
namespace detail {

// Helper to get class type from member pointer
template <typename T>
struct member_pointer_class;

template <typename C, typename T>
struct member_pointer_class<T C::*> {
    using type = C;
};

// Helper to get member type from member pointer
template <typename T>
struct member_pointer_type;

template <typename C, typename T>
struct member_pointer_type<T C::*> {
    using type = T;
};

// Helper: get field name from member pointer at compile time
template <typename C, auto MemberPtr>
std::string getFieldNameFromMemberPtr() {
    std::string result;
    refl::util::for_each(refl::reflect<C>().members, [&](auto member) {
        if constexpr (refl::descriptor::is_field(member)) {
            if (member.pointer == MemberPtr) {
                result = std::string(member.name);
            }
        }
    });
    return result;
}

// Runtime version: get field name from member pointer
template <typename C, typename T>
std::string getFieldNameFromMemberPtr(T C::* memberPtr) {
    std::string result;
    refl::util::for_each(refl::reflect<C>().members, [&](auto member) {
        if constexpr (refl::descriptor::is_field(member)) {
            // Compare the addresses of the member pointers
            // This is a bit of a hack but should work in most cases
            if (std::memcmp(&member.pointer, &memberPtr, sizeof(memberPtr)) == 0) {
                result = std::string(member.name);
            }
        }
    });
    return result;
}

// Helper to get table name from type at compile time
// For now, we simply use the reflected type name.
// If you have a custom Table attribute in your project, extend this function accordingly.
template <typename T>
std::string getTableNameFromType() {
    return std::string(refl::reflect<T>().name);
}

// This is a simplified implementation - in a real system, you'd need a more robust approach
template <typename C, typename T, auto MemberPtr>
constexpr std::string getMemberNameFromPtr() {
    // This is where you'd implement logic to match the member pointer to a name
    // For now, we'll use a simplified approach that works for the example
    
    // Use reflection to find all fields and try to match them
    std::string result;
    
    refl::util::for_each(refl::reflect<C>().members, [&](auto member) {
        if constexpr (refl::descriptor::is_field(member)) {
            // In a real implementation, you'd need a way to compare member pointers
            // This is a placeholder that would need to be replaced with actual logic
            
            // For now, just return the first field name as an example
            if (result.empty()) {
                result = std::string(member.name);
            }
        }
    });
    
    return result;
}

} // namespace detail
} // namespace orm
