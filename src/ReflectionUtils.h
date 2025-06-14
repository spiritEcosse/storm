// ReflectionUtils.h
#pragma once
#include <refl.hpp>

namespace storm::reflect {

// Type trait to check if a member is a field
template <typename T>
using is_field = refl::trait::is_field<T>;

// Wrapper for refl::util::for_each
template <typename Members, typename Fn>
constexpr void for_each_member(Members&& members, Fn&& fn) {
    refl::util::for_each(std::forward<Members>(members), std::forward<Fn>(fn));
}

// Helper to get member type
template <typename T>
using member_value_type = typename T::value_type;

// Helper to get member name
inline std::string get_member_name(const auto& member) {
    return std::string(member.name.c_str());
}

template <typename T>
static constexpr auto getType() {
    return refl::reflect<T>();
}

} // namespace storm::reflect
