#pragma once
#include <refl.hpp>
#include <string>
#include <string_view>
#include <type_traits>

// Utility functions that should be in a separate utils namespace/header
namespace utils {
    
    static std::string to_lower(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        return str;
    }
}

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

template<typename T>
class Reflect {
public:
    using reflected_type = T;
    
    // Type trait to check if a member is a field
    template <typename U>
    using is_field = refl::trait::is_field<U>;

    // Type trait to check if a member is a function
    template <typename U>
    using is_function = refl::trait::is_function<U>;

    // Helper to get member type
    template <typename U>
    using member_value_type = typename U::value_type;
    
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
    
    // Get struct name as lowercase string
    static std::string get_struct_name() {
        return utils::to_lower(std::string(get_reflected_type().name));
    }
    
    // Get number of members
    static constexpr std::size_t get_member_count() noexcept {
        return get_reflected_members().size;
    }
    
    template <typename Member>
    static constexpr std::string get_member_name(const Member& member) noexcept {
        return std::string(member.name.c_str());
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
    static auto find_field(FieldName&& name) {
        // Since we're not using the name parameter anymore, just return the first field
        // This is a temporary solution until we implement proper field name comparison
        constexpr auto members = get_reflected_members();
        return refl::util::find_one(members, [&](const auto& member) {
            return is_field<decltype(member)>::value;
        });
    }
    
    // Enum to represent the basic field types we support
    enum class FieldType {
        Integer,
        Double,
        Float,
        Boolean,
        String,
        Other
    };
    
    // Get the type of a field by its pointer
    template<auto MemberPtr>
    static constexpr FieldType get_field_type() {
        using FieldValueType = std::decay_t<decltype(std::declval<T>().*MemberPtr)>;
        
        if constexpr (std::is_same_v<FieldValueType, int> || 
                      std::is_same_v<FieldValueType, long> || 
                      std::is_same_v<FieldValueType, short> || 
                      std::is_same_v<FieldValueType, unsigned int> || 
                      std::is_same_v<FieldValueType, unsigned long> || 
                      std::is_same_v<FieldValueType, unsigned short>) {
            return FieldType::Integer;
        } else if constexpr (std::is_same_v<FieldValueType, double>) {
            return FieldType::Double;
        } else if constexpr (std::is_same_v<FieldValueType, float>) {
            return FieldType::Float;
        } else if constexpr (std::is_same_v<FieldValueType, bool>) {
            return FieldType::Boolean;
        } else if constexpr (std::is_same_v<FieldValueType, std::string> || 
                           std::is_same_v<FieldValueType, const char*> || 
                           std::is_same_v<FieldValueType, std::string_view>) {
            return FieldType::String;
        } else {
            return FieldType::Other;
        }
    }
    
    // Get the type of a field by its name (string version)
    static FieldType get_field_type(const std::string& name) {
        // For string-based field lookup, use SQLite's column type as fallback
        return FieldType::Other;
    }
};
