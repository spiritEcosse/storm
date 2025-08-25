#pragma once
// Legacy header placeholder; use C++23 modules (import storm.reflect).
import <string>;
import <vector>;
import <string_view>;
import <concepts>;
import <type_traits>;
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
import storm.utils;

// ============================================================================
// COMPILE-TIME STRING UTILITIES
// ============================================================================
namespace refl::detail {

    template <size_t N> struct fixed_string {
        constexpr fixed_string(const char (&str)[N]) {
            std::copy_n(str, N, value);
        }

        constexpr fixed_string(std::string_view sv) {
            if (sv.size() >= N) {
                std::copy_n(sv.data(), N - 1, value);
                value[N - 1] = '\0';
            } else {
                std::copy_n(sv.data(), sv.size(), value);
                value[sv.size()] = '\0';
            }
        }

        char value[N];

        constexpr operator std::string_view() const {
            return std::string_view(value);
        }

        constexpr const char* c_str() const {
            return value;
        }
        constexpr size_t size() const {
            return N - 1;
        }

        constexpr bool operator==(const fixed_string& other) const {
            return std::string_view(*this) == std::string_view(other);
        }
    };

    template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace refl::detail

// ============================================================================
// COMPILE-TIME REFLECTION METADATA
// ============================================================================
namespace refl::meta {

    // Compile-time member descriptor
    template <auto MemberPtr, refl::detail::fixed_string Name> struct member_descriptor {
        static constexpr auto name       = Name;
        static constexpr auto member_ptr = MemberPtr;

        using class_type = typename std::remove_cv_t<
                std::remove_reference_t<decltype(std::declval<std::decay_t<decltype(MemberPtr)>>())>>;

        using member_type = std::decay_t<decltype(std::declval<class_type>().*MemberPtr)>;

        static constexpr std::string_view get_name() {
            return std::string_view(name);
        }

        static constexpr auto get(const class_type& obj) -> const member_type& {
            return obj.*member_ptr;
        }

        static constexpr auto get(class_type& obj) -> member_type& {
            return obj.*member_ptr;
        }

        template <typename U>
        static constexpr auto set(class_type& obj, U&& value) -> std::expected<void, std::string> {
            if constexpr (std::is_assignable_v<member_type&, U>) {
                obj.*member_ptr = std::forward<U>(value);
                return {};
            } else {
                return std::unexpected{std::format("Cannot assign value to member '{}'", get_name())};
            }
        }

        static constexpr const char* type_name() {
            return typeid(member_type).name();
        }
    };

    // Compile-time type descriptor
    template <typename T, refl::detail::fixed_string TypeName, typename... Members> struct type_descriptor {
        using type                         = T;
        static constexpr auto name         = TypeName;
        static constexpr auto member_count = sizeof...(Members);
        using members_tuple                = std::tuple<Members...>;

        static constexpr std::string_view get_name() {
            return std::string_view(name);
        }

        static constexpr size_t get_member_count() {
            return member_count;
        }

        template <size_t I> static constexpr auto get_member() {
            static_assert(I < member_count, "Member index out of bounds");
            return std::get<I>(members_tuple{});
        }

        static constexpr auto get_member_names() {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array<std::string_view, member_count>{std::get<Is>(members_tuple{}).get_name()...};
            }(std::make_index_sequence<member_count>{});
        }

        template <typename F> static constexpr void for_each_member(F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, F&& f) {
                (f.template operator()<Is>(std::get<Is>(members_tuple{})), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
        }

        template <typename F> static constexpr void visit_members(const T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, const T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members).get_name(), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        template <typename F> static constexpr void visit_members(T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members).get_name(), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        static constexpr auto find_member_index(std::string_view name) -> std::optional<size_t> {
            auto names = get_member_names();
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i] == name) {
                    return i;
                }
            }
            return std::nullopt;
        }
    };

} // namespace refl::meta

// ============================================================================
// REFLECTION REGISTRY (COMPILE-TIME)
// ============================================================================
namespace refl {

    // Specialization point for types - users specialize this
    template <typename T> struct type_info {
        // Default: no reflection info
        static constexpr bool has_reflection = false;
    };

    // Concept for reflectable types
    template <typename T>
    concept reflectable = type_info<T>::has_reflection;

    // Main reflection interface
    template <reflectable T> struct reflect {
        using descriptor = typename type_info<T>::descriptor;

        static constexpr std::string_view name() {
            return descriptor::get_name();
        }

        static constexpr size_t member_count() {
            return descriptor::get_member_count();
        }

        static constexpr auto member_names() {
            return descriptor::get_member_names();
        }

        template <size_t I> static constexpr auto get_member() {
            return descriptor::template get_member<I>();
        }

        template <typename F> static constexpr void for_each_member(F&& func) {
            descriptor::for_each_member(std::forward<F>(func));
        }

        template <typename F> static constexpr void visit_members(const T& obj, F&& func) {
            descriptor::visit_members(obj, std::forward<F>(func));
        }

        template <typename F> static constexpr void visit_members(T& obj, F&& func) {
            descriptor::visit_members(obj, std::forward<F>(func));
        }

        static auto get_member_value(const T& obj, std::string_view name) -> std::expected<std::any, std::string> {
            std::expected<std::any, std::string> result = std::unexpected{"Member not found"};

            descriptor::visit_members(obj, [&](std::string_view member_name, const auto& value) {
                if (member_name == name) {
                    result = std::make_any<std::decay_t<decltype(value)>>(value);
                }
            });

            return result;
        }

        // Bulk optimized creation (when all fields are selected)
        template <typename Row>
        static auto create_objects_bulk_optimized(std::span<const Row> rows)
                -> std::expected<std::vector<T>, std::string> {
            return create_objects_from_rows(rows);
        }

        // Get field names as vector
        static auto get_field_names() -> std::vector<std::string_view> {
            auto names = descriptor::get_member_names();
            return std::vector<std::string_view>(names.begin(), names.end());
        }

        // Validate field names against type members
        static auto validate_field_names(std::span<const std::string> field_names) -> std::expected<void, std::string> {
            auto                     valid_names = descriptor::get_member_names();
            std::vector<std::string> invalid_fields;

            for (const auto& field_name : field_names) {
                bool found = std::ranges::any_of(valid_names, [&](std::string_view valid_name) {
                    return valid_name == field_name;
                });

                if (!found) {
                    invalid_fields.push_back(field_name);
                }
            }

            if (!invalid_fields.empty()) {
                return std::unexpected{std::format(
                        "Invalid field names: {}. Valid fields: {}",
                        storm::utils::join(invalid_fields, ", "),
                        storm::utils::join(valid_names, ", ")
                )};
            }

            return {};
        }

        // Convert object to key-value map
        static auto to_map(const T& obj) -> std::expected<std::unordered_map<std::string, std::any>, std::string> {
            std::unordered_map<std::string, std::any> result;

            descriptor::visit_members(obj, [&](std::string_view member_name, const auto& value) {
                result[std::string{member_name}] = std::make_any<std::decay_t<decltype(value)>>(value);
            });

            return result;
        }

        // Create object from key-value map
        static auto from_map(const std::unordered_map<std::string, std::any>& value_map)
                -> std::expected<T, std::string> {
            T                        obj{};
            std::vector<std::string> errors;

            descriptor::for_each_member([&]<size_t I>(auto member) {
                auto member_name = std::string{member.get_name()};

                if (auto it = value_map.find(member_name); it != value_map.end()) {
                    using MemberType = typename decltype(member)::member_type;

                    if (auto ptr = std::any_cast<MemberType>(&it->second); ptr) {
                        if (auto result = member.set(obj, *ptr); !result) {
                            errors.push_back(std::format("Failed to set member '{}': {}", member_name, result.error()));
                        }
                    } else {
                        errors.push_back(
                                std::format("Type mismatch for member '{}': cannot cast from any", member_name)
                        );
                    }
                }
                // Missing members are left as default-initialized
            });

            if (!errors.empty()) {
                return std::unexpected{storm::utils::join(errors, "; ")};
            }

            return obj;
        }

        // Serialize to JSON-like string (basic implementation)
        static auto to_json_string(const T& obj) -> std::expected<std::string, std::string> {
            std::string result = "{";
            bool        first  = true;

            descriptor::visit_members(obj, [&](std::string_view member_name, const auto& value) {
                if (!first)
                    result += ",";
                result += std::format("\"{}\":", member_name);

                using ValueType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<ValueType, std::string>) {
                    result += std::format("\"{}\"", value);
                } else if constexpr (std::is_same_v<ValueType, bool>) {
                    result += value ? "true" : "false";
                } else if constexpr (std::is_arithmetic_v<ValueType>) {
                    result += std::format("{}", value);
                } else {
                    result += "null";
                }
                first = false;
            });

            result += "}";
            return result;
        }

        // Deep copy an object (member by member)
        static auto deep_copy(const T& source) -> std::expected<T, std::string> {
            T                        result{};
            std::vector<std::string> errors;

            descriptor::for_each_member([&]<size_t I>(auto member) {
                const auto& source_value = member.get(source);

                if (auto set_result = member.set(result, source_value); !set_result) {
                    errors.push_back(
                            std::format("Failed to copy member '{}': {}", member.get_name(), set_result.error())
                    );
                }
            });

            if (!errors.empty()) {
                return std::unexpected{storm::utils::join(errors, "; ")};
            }

            return result;
        }

        // Check if object has default values
        static auto is_default_initialized(const T& obj) -> std::expected<bool, std::string> {
            if (auto default_obj = make(); !default_obj) {
                return std::unexpected{std::format("Cannot create default object: {}", default_obj.error())};
            } else {
                return equals(obj, *default_obj);
            }
        }

        // Get member count at runtime
        static constexpr size_t runtime_member_count() {
            return descriptor::get_member_count();
        }

        // Check if a member exists by name
        static auto has_member(std::string_view name) -> bool {
            auto names = descriptor::get_member_names();
            return std::ranges::any_of(names, [name](std::string_view member_name) { return member_name == name; });
        }

        // Get member type information
        template <size_t I> static auto get_member_type_info() -> std::string {
            static_assert(I < member_count(), "Member index out of bounds");
            auto member = descriptor::template get_member<I>();
            return std::string{member.type_name()};
        }

        // Visit members with index
        template <typename F> static constexpr void visit_members_with_index(const T& obj, F&& func) {
            descriptor::for_each_member([&]<size_t I>(auto member) { func(I, member.get_name(), member.get(obj)); });
        }

        template <typename F> static constexpr void visit_members_with_index(T& obj, F&& func) {
            descriptor::for_each_member([&]<size_t I>(auto member) { func(I, member.get_name(), member.get(obj)); });
        }
    };

} // namespace refl

// ============================================================================
// REGISTRATION MACRO FOR COMPILE-TIME REFLECTION
// ============================================================================
#define REFL_DEFINE_TYPE(Type, TypeNameStr, ...)                                                                       \
    namespace refl {                                                                                                   \
        template <> struct type_info<Type> {                                                                           \
            static constexpr bool has_reflection = true;                                                               \
            using descriptor =                                                                                         \
                    ::refl::meta::type_descriptor<Type, ::refl::detail::fixed_string{TypeNameStr}, __VA_ARGS__>;       \
        };                                                                                                             \
    }

#define REFL_MEMBER(Type, member) ::refl::meta::member_descriptor<&Type::member, ::refl::detail::fixed_string{#member}>

// ============================================================================
// EXAMPLE USAGE
// ============================================================================
// struct Person {
//     std::string name;
//     int age;
//     double salary;
//     bool is_active;
// };

// struct Author {
//     std::string name;
//     int id;
//     std::string email;
//     bool verified;
// };

// // Register types with compile-time reflection
// REFL_DEFINE_TYPE(Person, "Person",
//     REFL_MEMBER(Person, name),
//     REFL_MEMBER(Person, age),
//     REFL_MEMBER(Person, salary),
//     REFL_MEMBER(Person, is_active)
// );

// REFL_DEFINE_TYPE(Author, "Author",
//     REFL_MEMBER(Author, name),
//     REFL_MEMBER(Author, id),
//     REFL_MEMBER(Author, email),
//     REFL_MEMBER(Author, verified)
// );

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
// template<refl::reflectable T>
// auto print_type_info() -> void {
//     std::println("=== Type Information ===");
//     std::println("Type: {}", refl::reflect<T>::name());
//     std::println("Member count: {}", refl::reflect<T>::member_count());

//     auto names = refl::reflect<T>::member_names();
//     std::println("Members:");
//     for (const auto& name : names) {
//         std::println("  - {}", name);
//     }
//     std::println("");
// }

// template<refl::reflectable T>
// auto demo_reflection(T& obj) -> void {
//     std::println("=== Reflection Demo for {} ===", refl::reflect<T>::name());

//     // Print object state
//     if (auto str_result = refl::reflect<T>::to_string(obj); str_result) {
//         std::println("Object state: {}", *str_result);
//     } else {
//         std::println("Failed to convert to string: {}", str_result.error());
//     }

//     // Visit all members
//     std::println("Visiting members:");
//     refl::reflect<T>::visit_members(obj, [](std::string_view name, const auto& value) {
//         std::println("  {}: [value accessible]", name);
//     });

//     // Try to get a member value
//     if (auto result = refl::reflect<T>::get_member_value(obj, "name"); result) {
//         std::println("Successfully retrieved 'name' member");
//     } else {
//         std::println("Failed to get 'name': {}", result.error());
//     }

//     std::println("");
// }

// ============================================================================
// DATABASE INTEGRATION EXAMPLE
// ============================================================================

// Mock database row for demonstration
// struct MockRow {
//     std::vector<std::string> text_data;
//     std::vector<int> int_data;
//     std::vector<double> double_data;

//     MockRow(std::initializer_list<std::string> texts,
//             std::initializer_list<int> ints,
//             std::initializer_list<double> doubles)
//         : text_data(texts), int_data(ints), double_data(doubles) {}

//     int get_column_count() const {
//         return static_cast<int>(std::max({text_data.size(), int_data.size(), double_data.size()}));
//     }

//     std::string get_text(int col) const {
//         return col < static_cast<int>(text_data.size()) ? text_data[col] : "";
//     }

//     int get_int(int col) const {
//         return col < static_cast<int>(int_data.size()) ? int_data[col] : 0;
//     }

//     double get_double(int col) const {
//         return col < static_cast<int>(double_data.size()) ? double_data[col] : 0.0;
//     }
// };

// auto demo_database_integration() -> void {
//     std::println("\n=== Database Integration Demo ===");

//     // Create mock database rows
//     std::vector<MockRow> rows = {
//         MockRow({"Alice Johnson", "", "", "true"}, {0, 28, 0, 1}, {0.0, 0.0, 75000.0, 0.0}),
//         MockRow({"Bob Smith", "", "", "false"}, {0, 35, 0, 0}, {0.0, 0.0, 82000.0, 0.0}),
//         MockRow({"Carol Davis", "", "", "true"}, {0, 31, 0, 1}, {0.0, 0.0, 68000.0, 0.0})
//     };

//     // Test create_objects_from_rows (like select_all)
//     std::println("Creating objects from rows...");
//     if (auto persons = refl::reflect<Person>::create_objects_from_rows(std::span{rows}); persons) {
//         for (const auto& person : *persons) {
//             if (auto str_result = refl::reflect<Person>::to_string(person); str_result) {
//                 std::println("  {}", *str_result);
//             }
//         }
//     } else {
//         std::println("Failed to create objects: {}", persons.error());
//     }

//     // Test create_value_map_from_row (like select_value)
//     std::println("\nCreating value map from first row...");
//     if (auto value_map = refl::reflect<Person>::create_value_map_from_row(rows[0]); value_map) {
//         std::println("Value map contents:");
//         for (const auto& [key, value] : *value_map) {
//             std::print("  {}: ", key);
//             if (auto str_val = std::any_cast<std::string>(&value); str_val) {
//                 std::println("\"{}\"", *str_val);
//             } else if (auto int_val = std::any_cast<int>(&value); int_val) {
//                 std::println("{}", *int_val);
//             } else if (auto double_val = std::any_cast<double>(&value); double_val) {
//                 std::println("{}", *double_val);
//             } else {
//                 std::println("[unknown type]");
//             }
//         }
//     } else {
//         std::println("Failed to create value map: {}", value_map.error());
//     }

//     // Test create_value_maps_from_rows (like select_values)
//     std::println("\nCreating value maps from all rows...");
//     if (auto value_maps = refl::reflect<Person>::create_value_maps_from_rows(std::span{rows}); value_maps) {
//         std::println("Created {} value maps", value_maps->size());
//     } else {
//         std::println("Failed to create value maps: {}", value_maps.error());
//     }

//     // Test populate_selected_fields
//     std::println("\nTesting populate_selected_fields...");
//     Person partial_person{};
//     std::vector<std::string> selected_fields = {"name", "age"};

//     if (auto result = refl::reflect<Person>::populate_selected_fields(
//         partial_person, rows[0], std::span{selected_fields}); result) {
//         if (auto str_result = refl::reflect<Person>::to_string(partial_person); str_result) {
//             std::println("Partially populated person: {}", *str_result);
//         }
//     } else {
//         std::println("Failed to populate selected fields: {}", result.error());
//     }

//     // Test populate_distinct_fields
//     std::println("\nTesting populate_distinct_fields...");
//     Person distinct_person{};
//     std::vector<std::pair<size_t, std::string>> field_mappings = {
//         {0, "name"},    // Column 0 -> name field
//         {1, "age"}      // Column 1 -> age field
//     };

//     if (auto result = refl::reflect<Person>::populate_distinct_fields(
//         distinct_person, rows[0], field_mappings); result) {
//         if (auto str_result = refl::reflect<Person>::to_string(distinct_person); str_result) {
//             std::println("Distinct populated person: {}", *str_result);
//         }
//     } else {
//         std::println("Failed to populate distinct fields: {}", result.error());
//     }

//     // Test advanced features
//     std::println("\n=== Advanced Features Demo ===");

//     // Test to_map and from_map
//     Person test_person{"John Doe", 30, 60000.0, true};
//     if (auto map_result = refl::reflect<Person>::to_map(test_person); map_result) {
//         std::println("Converted person to map successfully");

//         if (auto person_result = refl::reflect<Person>::from_map(*map_result); person_result) {
//             if (auto str_result = refl::reflect<Person>::to_string(*person_result); str_result) {
//                 std::println("Recreated person from map: {}", *str_result);
//             }
//         }
//     }

//     // Test JSON serialization
//     if (auto json_result = refl::reflect<Person>::to_json_string(test_person); json_result) {
//         std::println("JSON representation: {}", *json_result);
//     }

//     // Test deep copy
//     if (auto copy_result = refl::reflect<Person>::deep_copy(test_person); copy_result) {
//         if (auto equals_result = refl::reflect<Person>::equals(test_person, *copy_result); equals_result) {
//             std::println("Deep copy successful, objects are equal: {}", *equals_result);
//         }
//     }

//     // Test default initialization check
//     Person default_person{};
//     if (auto is_default_result = refl::reflect<Person>::is_default_initialized(default_person); is_default_result) {
//         std::println("Default person is default initialized: {}", *is_default_result);
//     }
//     if (auto is_default_result = refl::reflect<Person>::is_default_initialized(test_person); is_default_result) {
//         std::println("Test person is default initialized: {}", *is_default_result);
//     }
// }

// ============================================================================
// MAIN DEMO
// ============================================================================
// int main() {
//     std::println("=== C++26 Style Exception-Free Reflection Demo ===\n");

//     // Create test objects
//     Person person{"Alice Johnson", 28, 75000.0, true};
//     Author author{"Bob Writer", 42, "bob@example.com", true};

//     // Print type information
//     print_type_info<Person>();
//     print_type_info<Author>();

//     // Demo reflection capabilities
//     demo_reflection(person);
//     demo_reflection(author);

//     // Test member access
//     std::println("=== Member Access Test ===");
//     if (auto result = refl::reflect<Person>::set_member_value(person, "age", 29); result) {
//         std::println("Successfully updated person's age");
//         if (auto str_result = refl::reflect<Person>::to_string(person); str_result) {
//             std::println("Updated person: {}", *str_result);
//         }
//     } else {
//         std::println("Failed to update age: {}", result.error());
//     }

//     if (auto result = refl::reflect<Person>::get_member_value(person, "age"); result) {
//         std::println("Retrieved age value successfully");
//     }

//     // Test equality comparison
//     std::println("\n=== Equality Test ===");
//     Person person2{"Alice Johnson", 29, 75000.0, true};
//     Person person3{"Bob Smith", 29, 75000.0, true};

//     if (auto eq1 = refl::reflect<Person>::equals(person, person2); eq1) {
//         std::println("person == person2: {}", *eq1);
//     }
//     if (auto eq2 = refl::reflect<Person>::equals(person, person3); eq2) {
//         std::println("person == person3: {}", *eq2);
//     }

//     // Test object creation
//     std::println("\n=== Object Creation Test ===");
//     if (auto created_person = refl::reflect<Person>::make("Charlie Brown", 35, 85000.0, false); created_person) {
//         if (auto str_result = refl::reflect<Person>::to_string(*created_person); str_result) {
//             std::println("Created person: {}", *str_result);
//         }
//     } else {
//         std::println("Failed to create person: {}", created_person.error());
//     }

//     // Compile-time member iteration
//     std::println("\n=== Compile-Time Member Info ===");
//     refl::reflect<Person>::for_each_member([]<size_t I>(auto member) {
//         std::println("Member {}: {} (type: {})", I, member.get_name(), member.type_name());
//     });

//     // Database integration demo
//     demo_database_integration();

//     return 0;
// }

/*
Expected Output:
=== C++26 Style Exception-Free Reflection Demo ===

=== Type Information ===
Type: Person
Member count: 4
Members:
  - name
  - age
  - salary
  - is_active

=== Type Information ===
Type: Author
Member count: 4
Members:
  - name
  - id
  - email
  - verified

=== Reflection Demo for Person ===
Object state: Person{name: "Alice Johnson", age: 28, salary: 75000, is_active: true}
Visiting members:
  name: [value accessible]
  age: [value accessible]
  salary: [value accessible]
  is_active: [value accessible]
Successfully retrieved 'name' member

=== Reflection Demo for Author ===
Object state: Author{name: "Bob Writer", id: 42, email: "bob@example.com", verified: true}
Visiting members:
  name: [value accessible]
  id: [value accessible]
  email: [value accessible]
  verified: [value accessible]
Successfully retrieved 'name' member

=== Member Access Test ===
Successfully updated person's age
Updated person: Person{name: "Alice Johnson", age: 29, salary: 75000, is_active: true}
Retrieved age value successfully

=== Equality Test ===
person == person2: true
person == person3: false

=== Object Creation Test ===
Created person: Person{name: "Charlie Brown", age: 35, salary: 85000, is_active: false}

=== Compile-Time Member Info ===
Member 0: name (type: NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE)
Member 1: age (type: i)
Member 2: salary (type: d)
Member 3: is_active (type: b)

=== Database Integration Demo ===
Creating objects from rows...
  Person{name: "Alice Johnson", age: 28, salary: 75000, is_active: true}
  Person{name: "Bob Smith", age: 35, salary: 82000, is_active: false}
  Person{name: "Carol Davis", age: 31, salary: 68000, is_active: true}

Creating value map from first row...
Value map contents:
  name: "Alice Johnson"
  age: 28
  salary: 75000
  is_active: 1

Created 3 value maps

Partially populated person: Person{name: "Alice Johnson", age: 28, salary: 0, is_active: false}
Distinct populated person: Person{name: "Alice Johnson", age: 28, salary: 0, is_active: false}

=== Advanced Features Demo ===
Converted person to map successfully
Recreated person from map: Person{name: "John Doe", age: 30, salary: 60000, is_active: true}
JSON representation: {"name":"John Doe","age":30,"salary":60000,"is_active":true}
Deep copy successful, objects are equal: true
Default person is default initialized: true
Test person is default initialized: false
*/

// template<typename U>
// static auto set_member_value(T& obj, std::string_view name, U&& value) -> std::expected<void, std::string> {
//     std::expected<void, std::string> result = std::unexpected{"Member not found"};

//     descriptor::for_each_member([&]<size_t I>(auto member) {
//         if (member.get_name() == name) {
//             if constexpr (std::is_same_v<std::decay_t<U>, std::any>) {
//                 using MemberType = typename decltype(member)::member_type;
//                 if (auto ptr = std::any_cast<MemberType>(&value); ptr) {
//                     result = member.set(obj, *ptr);
//                 } else {
//                     result = std::unexpected{std::format("Type mismatch for member '{}': cannot cast from any",
//                     name)};
//                 }
//             } else {
//                 result = member.set(obj, std::forward<U>(value));
//             }
//         }
//     });

//     return result;
// }

// static auto to_string(const T& obj) -> std::expected<std::string, std::string> {
//     std::string result = std::format("{}{{", name());
//     bool first = true;

//     visit_members(obj, [&](std::string_view member_name, const auto& value) {
//         if (!first) result += ", ";
//         result += std::format("{}: ", member_name);

//         using ValueType = std::decay_t<decltype(value)>;
//         if constexpr (std::is_same_v<ValueType, std::string>) {
//             result += std::format("\"{}\"", value);
//         } else if constexpr (std::is_arithmetic_v<ValueType>) {
//             if constexpr (std::is_same_v<ValueType, bool>) {
//                 result += value ? "true" : "false";
//             } else {
//                 result += std::format("{}", value);
//             }
//         } else {
//             result += "[complex type]";
//         }
//         first = false;
//     });

//     result += "}";
//     return result;
// }

// template<typename... Args>
// static constexpr auto make(Args&&... args) -> std::expected<T, std::string> {
//     if constexpr (sizeof...(Args) == member_count()) {
//         if constexpr (std::is_constructible_v<T, Args...>) {
//             return T{std::forward<Args>(args)...};
//         } else {
//             return std::unexpected{"Type is not constructible with provided arguments"};
//         }
//     } else {
//         return std::unexpected{std::format("Argument count mismatch: expected {}, got {}",
//                                          member_count(), sizeof...(Args))};
//     }
// }

// static constexpr auto equals(const T& lhs, const T& rhs) -> std::expected<bool, std::string> {
//     bool result = true;
//     std::string error_msg;

//     descriptor::visit_members(lhs, [&](std::string_view name, const auto& lhs_value) {
//         if (!result || !error_msg.empty()) return;

//         descriptor::visit_members(rhs, [&](std::string_view rhs_name, const auto& rhs_value) {
//             if (name == rhs_name) {
//                 using ValueType = std::decay_t<decltype(lhs_value)>;
//                 if constexpr (std::is_same_v<ValueType, std::decay_t<decltype(rhs_value)>>) {
//                     if constexpr (requires { lhs_value == rhs_value; }) {
//                         result = result && (lhs_value == rhs_value);
//                     } else {
//                         error_msg = std::format("Member '{}' is not equality comparable", name);
//                     }
//                 } else {
//                     error_msg = std::format("Type mismatch for member '{}'", name);
//                 }
//             }
//         });
//     });

//     if (!error_msg.empty()) {
//         return std::unexpected{error_msg};
//     }

//     return result;
// }

// // ============================================================================
// // DATABASE INTEGRATION METHODS
// // ============================================================================

// // Safe type conversion helpers
// template<typename MemberType>
// static auto safe_convert_from_row(const auto& row, int column_idx) -> std::expected<MemberType, std::string> {
//     if (column_idx >= row.get_column_count()) {
//         return std::unexpected{std::format("Column index {} out of bounds (max: {})",
//                                          column_idx, row.get_column_count() - 1)};
//     }

//     if constexpr (std::is_same_v<MemberType, std::string>) {
//         // Try to get as text
//         if (auto text_result = safe_get_text(row, column_idx); text_result) {
//             return *text_result;
//         } else {
//             return std::unexpected{text_result.error()};
//         }
//     } else if constexpr (std::is_integral_v<MemberType>) {
//         if constexpr (std::is_same_v<MemberType, bool>) {
//             // Smart boolean conversion
//             if (auto int_result = safe_get_int(row, column_idx); int_result) {
//                 return *int_result != 0;
//             } else {
//                 // Try text-based boolean conversion
//                 if (auto text_result = safe_get_text(row, column_idx); text_result) {
//                     std::string str_val = *text_result;
//                     std::transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
//                     return str_val == "true" || str_val == "1" || str_val == "yes" || str_val == "on";
//                 } else {
//                     return std::unexpected{std::format("Cannot convert column {} to bool: {}",
//                                                      column_idx, text_result.error())};
//                 }
//             }
//         } else {
//             if (auto int_result = safe_get_int(row, column_idx); int_result) {
//                 if constexpr (std::is_same_v<MemberType, int>) {
//                     return *int_result;
//                 } else {
//                     // Check for overflow when converting to other integral types
//                     if (*int_result >= std::numeric_limits<MemberType>::min() &&
//                         *int_result <= std::numeric_limits<MemberType>::max()) {
//                         return static_cast<MemberType>(*int_result);
//                     } else {
//                         return std::unexpected{std::format("Integer overflow when converting {} to target type",
//                                                          *int_result)};
//                     }
//                 }
//             } else {
//                 return std::unexpected{int_result.error()};
//             }
//         }
//     } else if constexpr (std::is_floating_point_v<MemberType>) {
//         if (auto double_result = safe_get_double(row, column_idx); double_result) {
//             return static_cast<MemberType>(*double_result);
//         } else {
//             return std::unexpected{double_result.error()};
//         }
//     } else if constexpr (std::is_enum_v<MemberType>) {
//         // Handle enums
//         if (auto int_result = safe_get_int(row, column_idx); int_result) {
//             return static_cast<MemberType>(*int_result);
//         } else {
//             return std::unexpected{std::format("Cannot convert column {} to enum: {}",
//                                              column_idx, int_result.error())};
//         }
//     } else {
//         // For custom types, try string conversion
//         if constexpr (std::is_constructible_v<MemberType, std::string>) {
//             if (auto text_result = safe_get_text(row, column_idx); text_result) {
//                 return MemberType{*text_result};
//             } else {
//                 return std::unexpected{text_result.error()};
//             }
//         } else {
//             return std::unexpected{std::format("Type '{}' is not supported for database conversion",
//                                              typeid(MemberType).name())};
//         }
//     }
// }

// // Safe database row access helpers
// static auto safe_get_text(const auto& row, int column_idx) -> std::expected<std::string, std::string> {
//     if (column_idx < 0 || column_idx >= row.get_column_count()) {
//         return std::unexpected{std::format("Column index {} out of bounds", column_idx)};
//     }
//     // Assume get_text is noexcept or handle internally
//     return row.get_text(column_idx);
// }

// static auto safe_get_int(const auto& row, int column_idx) -> std::expected<int, std::string> {
//     if (column_idx < 0 || column_idx >= row.get_column_count()) {
//         return std::unexpected{std::format("Column index {} out of bounds", column_idx)};
//     }
//     // If the row's get_int can fail, we'd need to handle that
//     // For now, assume it returns a valid int or has internal error handling
//     return row.get_int(column_idx);
// }

// static auto safe_get_double(const auto& row, int column_idx) -> std::expected<double, std::string> {
//     if (column_idx < 0 || column_idx >= row.get_column_count()) {
//         return std::unexpected{std::format("Column index {} out of bounds", column_idx)};
//     }
//     return row.get_double(column_idx);
// }

// // Create value map from database row - compile-time version
// template<typename Row>
// static auto create_value_map_from_row(const Row& row) -> std::expected<std::unordered_map<std::string, std::any>,
// std::string> {
//     std::unordered_map<std::string, std::any> value_map;
//     std::vector<std::string> errors;

//     descriptor::for_each_member([&]<size_t I>(auto member) {
//         if (I < static_cast<size_t>(row.get_column_count())) {
//             auto name = std::string{member.get_name()};

//             // Try different types in order of preference
//             if (auto text_result = safe_get_text(row, static_cast<int>(I)); text_result) {
//                 value_map[name] = std::make_any<std::string>(*text_result);
//             } else if (auto int_result = safe_get_int(row, static_cast<int>(I)); int_result) {
//                 value_map[name] = std::make_any<int>(*int_result);
//             } else if (auto double_result = safe_get_double(row, static_cast<int>(I)); double_result) {
//                 value_map[name] = std::make_any<double>(*double_result);
//             } else {
//                 errors.push_back(std::format("Column {}: all conversions failed", I));
//                 value_map[name] = std::any{};
//             }
//         }
//     });

//     if (!errors.empty()) {
//         return std::unexpected{std::format("Conversion errors: {}", storm::utils::join(errors, "; "))};
//     }

//     return value_map;
// }

// // Create multiple value maps from multiple rows
// template<typename Row>
// static auto create_value_maps_from_rows(std::span<const Row> rows)
//     -> std::expected<std::vector<std::unordered_map<std::string, std::any>>, std::string> {

//     std::vector<std::unordered_map<std::string, std::any>> result;
//     result.reserve(rows.size());

//     for (size_t i = 0; i < rows.size(); ++i) {
//         if (auto map_result = create_value_map_from_row(rows[i]); map_result) {
//             result.emplace_back(std::move(*map_result));
//         } else {
//             return std::unexpected{std::format("Failed to create value map for row {}: {}",
//                                              i, map_result.error())};
//         }
//     }

//     return result;
// }

// template<typename Member, typename Row>
// static auto populate_member_from_column(T& obj, Member member, const Row& row, int column_idx)
//     -> std::expected<void, std::string> {

//     using MemberType = typename Member::member_type;

//     if (auto converted_value = safe_convert_from_row<MemberType>(row, column_idx); converted_value) {
//         return member.set(obj, std::move(*converted_value));
//     } else {
//         return std::unexpected{std::format("Failed to populate member '{}' from column {}: {}",
//                                          member.get_name(), column_idx, converted_value.error())};
//     }
// }

// // Populate object from database row - compile-time version
// template<typename Row>
// static auto populate_from_row(T& obj, const Row& row, int start_column = 0)
//     -> std::expected<void, std::string> {

//     std::vector<std::string> errors;
//     int successful_fields = 0;

//     descriptor::for_each_member([&]<size_t I>(auto member) {
//         int column_idx = start_column + static_cast<int>(I);

//         if (column_idx >= row.get_column_count()) {
//             return; // Stop if we run out of columns
//         }

//         if (auto result = populate_member_from_column(obj, member, row, column_idx); result) {
//             successful_fields++;
//         } else {
//             errors.push_back(std::format("Field '{}' at column {}: {}",
//                                        member.get_name(), column_idx, result.error()));
//         }
//     });

//     // Return error only if ALL fields failed
//     if (successful_fields == 0 && !errors.empty()) {
//         return std::unexpected{storm::utils::join(errors, "; ")};
//     }

//     return {};
// }

// template<typename QueryBuilder>
// static auto create_objects_from_query(std::span<const typename QueryBuilder::Row> rows, const QueryBuilder&
// query_builder)
//     -> std::expected<std::vector<T>, std::string> {

//     if (rows.empty()) {
//         return std::vector<T>{};
//     }

//     // Let reflection system intelligently handle the query context
//     if (query_builder.should_use_bulk_population()) {
//         // All fields - use optimized bulk creation
//         return create_objects_bulk_optimized(rows);
//     } else {
//         // Partial fields - use context-aware population
//         return create_objects_with_query_context(rows, query_builder);
//     }
// }

// template<typename QueryBuilder>
// static auto create_object_from_query(const typename QueryBuilder::Row& row, const QueryBuilder& query_builder)
//     -> std::expected<T, std::string> {

//     T obj{};

//     // Delegate population strategy to reflection system
//     auto populate_result = [&]() -> std::expected<void, std::string> {
//         if (!query_builder.distinctFields.empty()) {
//             auto field_mappings = extract_distinct_mappings(query_builder.distinctFields, row);
//             return populate_distinct_fields(obj, row, field_mappings);
//         } else if (!query_builder.onlyFields.empty()) {
//             auto selected_fields = extract_only_field_names(query_builder.onlyFields);
//             return populate_selected_fields(obj, row, std::span{selected_fields});
//         } else {
//             return populate_from_row(obj, row);
//         }
//     }();

//     if (populate_result) {
//         return obj;
//     } else {
//         return std::unexpected{populate_result.error()};
//     }
// }

// // Helper methods for the reflection system
// template<typename DistinctFields>
// static auto extract_distinct_mappings(const DistinctFields& distinct_fields, const auto& row)
//     -> std::expected<std::vector<std::pair<size_t, std::string>>, std::string> {

//     std::vector<std::pair<size_t, std::string>> result;

//     for (size_t idx = 0; idx < distinct_fields.size(); ++idx) {
//         const auto& fieldAlias = distinct_fields[idx];
//         if (fieldAlias != nullptr && idx < static_cast<size_t>(row.get_column_count())) {
//             result.emplace_back(idx, fieldAlias->getFieldName());
//         }
//     }

//     return result;
// }

// template<typename OnlyFields>
// static auto extract_only_field_names(const OnlyFields& only_fields)
//     -> std::expected<std::vector<std::string>, std::string> {

//     std::vector<std::string> result;

//     for (const auto& field : only_fields) {
//         if (field != nullptr) {
//             result.push_back(field->getFieldName());
//         }
//     }

//     return result;
// }

// template<typename QueryBuilder>
// static auto create_objects_with_query_context(std::span<const typename QueryBuilder::Row> rows,
//                                              const QueryBuilder& query_builder)
//     -> std::expected<std::vector<T>, std::string> {

//     std::vector<T> result;
//     result.reserve(rows.size());

//     for (size_t i = 0; i < rows.size(); ++i) {
//         if (auto obj_result = create_object_from_query(rows[i], query_builder); obj_result) {
//             result.emplace_back(std::move(*obj_result));
//         } else {
//             return std::unexpected{std::format("Failed to create object at row {}: {}",
//                                              i, obj_result.error())};
//         }
//     }

//     return result;
// }

// // Populate selected fields only
// template<typename Row>
// static auto populate_selected_fields(T& obj, const Row& row, std::span<const std::string> selected_fields)
//     -> std::expected<void, std::string> {

//     if (selected_fields.size() > static_cast<size_t>(row.get_column_count())) {
//         return std::unexpected{std::format("Selected fields count ({}) exceeds row columns ({})",
//                                          selected_fields.size(), row.get_column_count())};
//     }

//     std::vector<std::string> missing_fields;
//     std::vector<std::string> errors;

//     for (size_t field_idx = 0; field_idx < selected_fields.size(); ++field_idx) {
//         const auto& field_name = selected_fields[field_idx];

//         bool found = false;
//         descriptor::for_each_member([&]<size_t I>(auto member) {
//             if (!found && member.get_name() == field_name) {
//                 found = true;
//                 if (auto result = populate_member_from_column(obj, member, row, static_cast<int>(field_idx));
//                 !result) {
//                     errors.push_back(std::format("Failed to populate field '{}': {}",
//                                                field_name, result.error()));
//                 }
//             }
//         });

//         if (!found) {
//             missing_fields.push_back(field_name);
//         }
//     }

//     if (!missing_fields.empty()) {
//         return std::unexpected{std::format("Fields not found in type: {}",
//                                          storm::utils::join(missing_fields, ", "))};
//     }

//     if (!errors.empty()) {
//         return std::unexpected{storm::utils::join(errors, "; ")};
//     }

//     return {};
// }

// // Populate distinct fields with column mappings
// template<typename Row>
// static auto populate_distinct_fields(T& obj, const Row& row,
//                                    const std::vector<std::pair<size_t, std::string>>& field_mappings)
//     -> std::expected<void, std::string> {

//     std::vector<std::string> errors;

//     for (const auto& [column_idx, field_name] : field_mappings) {
//         bool found = false;

//         descriptor::for_each_member([&]<size_t I>(auto member) {
//             if (!found && member.get_name() == field_name) {
//                 found = true;

//                 if (static_cast<int>(column_idx) >= row.get_column_count()) {
//                     errors.push_back(std::format("Column index {} out of bounds for field '{}'",
//                                                 column_idx, field_name));
//                     return;
//                 }

//                 if (auto result = populate_member_from_column(obj, member, row, static_cast<int>(column_idx));
//                 !result) {
//                     errors.push_back(std::format("Failed to populate field '{}': {}",
//                                                field_name, result.error()));
//                 }
//             }
//         });

//         if (!found) {
//             errors.push_back(std::format("Field '{}' not found in type", field_name));
//         }
//     }

//     if (!errors.empty()) {
//         return std::unexpected{storm::utils::join(errors, "; ")};
//     }

//     return {};
// }

// // Bulk object creation from rows (optimized for select_all)
// template<typename Row>
// static auto create_objects_from_rows(std::span<const Row> rows)
//     -> std::expected<std::vector<T>, std::string> {

//     if (rows.empty()) {
//         return std::vector<T>{};
//     }

//     std::vector<T> result;
//     result.reserve(rows.size());

//     for (size_t i = 0; i < rows.size(); ++i) {
//         T obj{};
//         if (auto populate_result = populate_from_row(obj, rows[i], 0); populate_result) {
//             result.emplace_back(std::move(obj));
//         } else {
//             return std::unexpected{std::format("Failed to populate object at row {}: {}",
//                                              i, populate_result.error())};
//         }
//     }
