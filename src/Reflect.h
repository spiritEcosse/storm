int main() {
    print_type_info.operator()<Person>();
    print_type_info.operator()<Author>();
    
    // Example of bind_object_values using new API
    auto bind_object_values = []<typename T>(auto& stmt, T const& obj, int& param_index) {
        auto const field_names = refl::reflect<T>::field_names();
        
        for (auto const& field_name : field_names) {
            if (field_name == "id") continue; // Skip auto-generated id
            
            // Note: In a real implementation, you'd need a way to get the actual field value
            // This is a limitation of the simple reflection - we only store names/types, not accessors
            std::cout << "Would bind field: " << field_name << " at param_index: " << param_index << '\n';
            param_index++;
        }
    };
    
    // Usage example
    Person person{"John", 30, 50000.0};
    int param_index = 1;
    // bind_object_values(some_statement, person, param_index);
    
    return 0;
}

/*
Note: The simple reflection library doesn't store field accessors, only names and types.
For actual field value access, you'd need one of these approaches:

1. Store member pointers in the reflection data
2. Use a more complex macro that generates accessor functions  
3. Use a different reflection approach that supports field access

Example with member pointers:
struct FieldAccessor {
    std::string name;
    std::string type;
    std::function<void(void const*, Statement&, int&)> bind_func;
};
*/#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <concepts>
#include <type_traits>
#include <unordered_map>
#include <iostream>
#include <ranges>
#include <algorithm>
#include <cctype>
#include <functional>
#include <any>

namespace refl {

// Field information with accessor function
template<typename T>
struct Field {
    std::string name;
    std::string type;
    std::function<std::any(T const&)> get_value; // Field accessor
};

// Type information - just name and fields
template<typename T>
struct Type {
    std::string name;
    std::vector<Field<T>> fields;
};

// Get clean type name using C++23 features
template<typename T>
consteval std::string_view type_name() {
    std::string_view name = __PRETTY_FUNCTION__;
    auto start = name.find("T = ") + 4;
    auto end = name.find_last_of(']');
    return name.substr(start, end - start);
}

// Type registry
template<typename T>
class Registry {
    static inline std::unordered_map<std::string, Type<T>> types_{};

public:
    static void register_type(Type<T>&& type) {
        types_[type.name] = std::move(type);
    }

    static auto get_type() -> Type<T> const* {
        auto name = std::string{type_name<T>()};
        if (auto it = types_.find(name); it != types_.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

// Reflectable concept
template<typename T>
concept Reflectable = requires {
    typename T::_reflect_tag;
};

// Utility function using C++23 ranges
inline auto to_lower(std::string str) -> std::string {
    std::ranges::transform(str, str.begin(),
                          [](unsigned char c) { return std::tolower(c); });
    return str;
}

// Main reflection interface
template<typename T>
struct reflect {
    static auto name() -> std::string {
        return std::string{type_name<T>()};
    }

    static auto get_struct_name() -> std::string {
        return to_lower(name());
    }

    static auto fields() -> std::vector<Field<T>> {
        if (auto const* type = Registry<T>::get_type()) {
            return type->fields;
        }
        return {};
    }

    static auto field_names() -> std::vector<std::string> {
        auto result = std::vector<std::string>{};
        for (auto const& field : fields()) {
            result.push_back(field.name);
        }
        return result;
    }

    static auto field_types() -> std::vector<std::string> {
        auto result = std::vector<std::string>{};
        for (auto const& field : fields()) {
            result.push_back(field.type);
        }
        return result;
    }

    // For each field functionality
    template<typename Func>
    static void for_each_field(Func&& func) {
        auto const field_list = fields();
        for (auto const& field : field_list) {
            func(field);
        }
    }

    // Check if field exists
    static auto has_field(std::string_view field_name) -> bool {
        auto const field_list = fields();
        return std::ranges::any_of(field_list, [field_name](auto const& field) {
            return field.name == field_name;
        });
    }

    // Get field value by name
    static auto get_field_value(T const& obj, std::string_view field_name) -> std::any {
        auto const field_list = fields();
        for (auto const& field : field_list) {
            if (field.name == field_name) {
                return field.get_value(obj);
            }
        }
        return {};
    }

    // Get field name from member pointer (new functionality)
    template<auto MemberPtr>
    requires std::is_member_object_pointer_v<decltype(MemberPtr)>
    static auto get_field_name_from_member_ptr() -> std::string {
        // Extract field name from member pointer using compile-time reflection
        constexpr auto member_name = extract_member_name<MemberPtr>();
        return std::string{member_name};
    }

private:
    // Helper to extract member name from pointer at compile time
    template<auto MemberPtr>
    static consteval auto extract_member_name() -> std::string_view {
        std::string_view func_name = __PRETTY_FUNCTION__;
        // Parse the member pointer from function signature
        // This is compiler-specific and needs adjustment for your compiler
        auto start = func_name.find("::") + 2;
        auto end = func_name.find("]", start);
        return func_name.substr(start, end - start);
    }

public:

    // Bind field value helper (equivalent to your bind_field_value)
    template<typename Statement>
    static void bind_field_value(Statement& stmt, T const& obj, std::string_view field_name, int& param_index) {
        if (!has_field(field_name)) {
            return; // Field not found
        }
        
        auto value = get_field_value(obj, field_name);
        // bind_sql_value(stmt, param_index, value); // Your binding function
        param_index++;
    }

    // Bind all object values (equivalent to your bind_object_values)  
    template<typename Statement>
    static void bind_object_values(Statement& stmt, T const& obj, int& param_index) {
        for_each_field([&](auto const& field) {
            if (field.name == "id") return; // Skip auto-generated id
            
            auto value = field.get_value(obj);
            // bind_sql_value(stmt, param_index, value); // Your binding function
            param_index++;
        });
    }
};

} // namespace refl

// Simple registration macro
#define REFLECT(TypeName, ...) \
    namespace { \
        struct TypeName##_registrar { \
            TypeName##_registrar() { \
                refl::Type type_info; \
                type_info.name = #TypeName; \
                register_fields(type_info, __VA_ARGS__); \
                refl::Registry::register_type(std::move(type_info)); \
            } \
        private: \
            template<typename... Fields> \
            void register_fields(refl::Type& type, Fields const&... fields) { \
                (add_field(type, fields), ...); \
            } \
            template<typename FieldType> \
            void add_field(refl::Type& type, std::pair<std::string_view, FieldType*> const& field_info) { \
                type.fields.push_back({std::string{field_info.first}, std::string{refl::type_name<FieldType>()}}); \
            } \
        }; \
        [[maybe_unused]] static TypeName##_registrar TypeName##_registrar_instance; \
    } \
    template<> \
    struct TypeName { \
        using _reflect_tag = void; \
    }

// Helper macro for fields
#define FIELD(name, type) std::make_pair(std::string_view{#name}, static_cast<type*>(nullptr))

// Even simpler approach - just list field names and types
#define SIMPLE_REFLECT(TypeName, ...) \
    namespace { \
        struct TypeName##_reflect_init { \
            TypeName##_reflect_init() { \
                refl::Type type_info; \
                type_info.name = #TypeName; \
                add_fields(type_info, __VA_ARGS__); \
                refl::Registry::register_type(std::move(type_info)); \
            } \
        private: \
            template<typename... Args> \
            void add_fields(refl::Type& type, Args const&... args) { \
                (process_field_pair(type, args), ...); \
            } \
            void process_field_pair(refl::Type& type, std::pair<std::string, std::string> const& field) { \
                type.fields.push_back({field.first, field.second}); \
            } \
        }; \
        [[maybe_unused]] static TypeName##_reflect_init TypeName##_init; \
    }

// Helper for field pairs
#define F(name, type) std::make_pair(std::string{#name}, std::string{#type})

// Usage examples
struct Person {
    std::string name;
    int age;
    double salary;
};

struct Author {
    std::string name;
    int id;
    std::string email;
};

// Register types
SIMPLE_REFLECT(Person,
    F(name, std::string),
    F(age, int),
    F(salary, double)
);

SIMPLE_REFLECT(Author,
    F(name, std::string),
    F(id, int),
    F(email, std::string)
);

// Usage
auto print_type_info = []<typename T>() {
    std::cout << "Type: " << refl::reflect<T>::name() << '\n';
    std::cout << "Struct name (lowercase): " << refl::reflect<T>::get_struct_name() << '\n';
    
    auto field_names = refl::reflect<T>::field_names();
    auto field_types = refl::reflect<T>::field_types();
    
    std::cout << "Fields:\n";
    for (size_t i = 0; i < field_names.size(); ++i) {
        std::cout << "  " << field_names[i] << ": " << field_types[i] << '\n';
    }
    std::cout << '\n';
};

int main() {
    print_type_info.operator()<Person>();
    print_type_info.operator()<Author>();
    
    return 0;
}

/*
Expected Output:
Type: Person
Struct name (lowercase): person
Fields:
  name: std::string
  age: int
  salary: double

Type: Author
Struct name (lowercase): author
Fields:
  name: std::string
  id: int
  email: std::string
*/