#pragma once

#include <string>
#include <type_traits>
#include "refl.hpp"

namespace storm {

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
template <auto MemberPtr>
std::string getFieldNameFromMemberPtr() {
    std::string result;
    using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;

    refl::util::for_each(refl::reflect<ClassType>().members, [&](auto member) {
        if constexpr (refl::descriptor::is_field(member)) {
            // Compare the actual member pointer values using std::is_same_v
            if constexpr (std::is_same_v<std::decay_t<decltype(member.pointer)>, 
                                       std::decay_t<decltype(MemberPtr)>>) {
                if constexpr (member.pointer == MemberPtr) {
                    result = std::string(member.name);
                }
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

} // namespace storm
