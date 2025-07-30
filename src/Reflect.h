#pragma once
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
#include <expected>
#include <print>
#include <span>
#include <array>
#include <optional>
#include <format>

// ============================================================================
// COMPILE-TIME STRING UTILITIES
// ============================================================================
namespace refl::detail {

template<size_t N>
struct fixed_string {
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
    
    constexpr const char* c_str() const { return value; }
    constexpr size_t size() const { return N - 1; }
    
    constexpr bool operator==(const fixed_string& other) const {
        return std::string_view(*this) == std::string_view(other);
    }
};

template<size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace refl::detail

// ============================================================================
// COMPILE-TIME REFLECTION METADATA
// ============================================================================
namespace refl::meta {

// Compile-time member descriptor
template<auto MemberPtr, refl::detail::fixed_string Name>
struct member_descriptor {
    static constexpr auto name = Name;
    static constexpr auto member_ptr = MemberPtr;
    
    using class_type = typename std::remove_cv_t<
        std::remove_reference_t<
            decltype(std::declval<std::decay_t<decltype(MemberPtr)>>())
        >
    >;
    
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
    
    template<typename U>
    static constexpr void set(class_type& obj, U&& value) {
        obj.*member_ptr = std::forward<U>(value);
    }
    
    static constexpr const char* type_name() {
        return typeid(member_type).name();
    }
};

// Compile-time type descriptor
template<typename T, refl::detail::fixed_string TypeName, typename... Members>
struct type_descriptor {
    using type = T;
    static constexpr auto name = TypeName;
    static constexpr auto member_count = sizeof...(Members);
    using members_tuple = std::tuple<Members...>;
    
    static constexpr std::string_view get_name() {
        return std::string_view(name);
    }
    
    static constexpr size_t get_member_count() {
        return member_count;
    }
    
    template<size_t I>
    static constexpr auto get_member() {
        static_assert(I < member_count, "Member index out of bounds");
        return std::get<I>(members_tuple{});
    }
    
    static constexpr auto get_member_names() {
        return []<size_t... Is>(std::index_sequence<Is...>) {
            return std::array<std::string_view, member_count>{
                std::get<Is>(members_tuple{}).get_name()...
            };
        }(std::make_index_sequence<member_count>{});
    }
    
    template<typename F>
    static constexpr void for_each_member(F&& func) {
        []<size_t... Is>(std::index_sequence<Is...>, F&& f) {
            (f.template operator()<Is>(std::get<Is>(members_tuple{})), ...);
        }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
    }
    
    template<typename F>
    static constexpr void visit_members(const T& obj, F&& func) {
        []<size_t... Is>(std::index_sequence<Is...>, const T& o, F&& f) {
            auto members = members_tuple{};
            (f(std::get<Is>(members).get_name(), std::get<Is>(members).get(o)), ...);
        }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
    }
    
    template<typename F>
    static constexpr void visit_members(T& obj, F&& func) {
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
template<typename T>
struct type_info {
    // Default: no reflection info
    static constexpr bool has_reflection = false;
};

// Concept for reflectable types
template<typename T>
concept reflectable = type_info<T>::has_reflection;

// Main reflection interface
template<reflectable T>
struct reflect {
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
    
    template<size_t I>
    static constexpr auto get_member() {
        return descriptor::template get_member<I>();
    }
    
    template<typename F>
    static constexpr void for_each_member(F&& func) {
        descriptor::for_each_member(std::forward<F>(func));
    }
    
    template<typename F>
    static constexpr void visit_members(const T& obj, F&& func) {
        descriptor::visit_members(obj, std::forward<F>(func));
    }
    
    template<typename F>
    static constexpr void visit_members(T& obj, F&& func) {
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
    
    template<typename U>
    static auto set_member_value(T& obj, std::string_view name, U&& value) -> std::expected<void, std::string> {
        std::expected<void, std::string> result = std::unexpected{"Member not found"};
        
        descriptor::for_each_member([&]<size_t I>(auto member) {
            if (member.get_name() == name) {
                try {
                    if constexpr (std::is_same_v<std::decay_t<U>, std::any>) {
                        using MemberType = typename decltype(member)::member_type;
                        member.set(obj, std::any_cast<MemberType>(value));
                    } else {
                        member.set(obj, std::forward<U>(value));
                    }
                    result = {};
                } catch (const std::exception& e) {
                    result = std::unexpected{std::format("Failed to set member '{}': {}", name, e.what())};
                }
            }
        });
        
        return result;
    }
    
    static auto to_string(const T& obj) -> std::string {
        std::string result = std::format("{}{{", name());
        bool first = true;
        
        visit_members(obj, [&](std::string_view member_name, const auto& value) {
            if (!first) result += ", ";
            result += std::format("{}: ", member_name);
            
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, std::string>) {
                result += std::format("\"{}\"", value);
            } else if constexpr (std::is_arithmetic_v<ValueType>) {
                if constexpr (std::is_same_v<ValueType, bool>) {
                    result += value ? "true" : "false";
                } else {
                    result += std::format("{}", value);
                }
            } else {
                result += "[complex type]";
            }
            first = false;
        });
        
        result += "}";
        return result;
    }
    
    template<typename... Args>
    static constexpr auto make(Args&&... args) -> T {
        static_assert(sizeof...(Args) == member_count(), "Argument count must match member count");
        return T{std::forward<Args>(args)...};
    }
    
    static constexpr auto equals(const T& lhs, const T& rhs) -> bool {
        bool result = true;
        
        descriptor::visit_members(lhs, [&](std::string_view name, const auto& lhs_value) {
            descriptor::visit_members(rhs, [&](std::string_view rhs_name, const auto& rhs_value) {
                if (name == rhs_name) {
                    using ValueType = std::decay_t<decltype(lhs_value)>;
                    if constexpr (std::is_same_v<ValueType, std::decay_t<decltype(rhs_value)>>) {
                        result = result && (lhs_value == rhs_value);
                    } else {
                        result = false;
                    }
                }
            });
        });
        
        return result;
    }
    
    // ============================================================================
    // DATABASE INTEGRATION METHODS
    // ============================================================================
    
    // Create value map from database row - compile-time version
    template<typename Row>
    static auto create_value_map_from_row(const Row& row) -> std::unordered_map<std::string, std::any> {
        std::unordered_map<std::string, std::any> value_map;
        
        descriptor::for_each_member([&]<size_t I>(auto member) {
            if (I < static_cast<size_t>(row.get_column_count())) {
                auto name = std::string{member.get_name()};
                try {
                    // Try different types in order of preference
                    try {
                        value_map[name] = std::make_any<std::string>(row.get_text(static_cast<int>(I)));
                    } catch (...) {
                        try {
                            value_map[name] = std::make_any<int>(row.get_int(static_cast<int>(I)));
                        } catch (...) {
                            try {
                                value_map[name] = std::make_any<double>(row.get_double(static_cast<int>(I)));
                            } catch (...) {
                                value_map[name] = std::any{};
                            }
                        }
                    }
                } catch (...) {
                    value_map[name] = std::any{};
                }
            }
        });
        
        return value_map;
    }
    
    // Create multiple value maps from multiple rows
    template<typename Row>
    static auto create_value_maps_from_rows(std::span<const Row> rows) 
        -> std::vector<std::unordered_map<std::string, std::any>> {
        
        return rows 
            | std::views::transform([](const Row& row) {
                return create_value_map_from_row(row);
            })
            | std::ranges::to<std::vector>();
    }
    
    template<typename Member, typename Row>
    static void populate_member_from_column(T& obj, Member member, const Row& row, int column_idx) {
        using MemberType = typename Member::member_type;
        
        if constexpr (std::is_same_v<MemberType, std::string>) {
            member.set(obj, row.get_text(column_idx));
        } else if constexpr (std::is_integral_v<MemberType>) {
            if constexpr (std::is_same_v<MemberType, bool>) {
                // Smart boolean conversion
                try {
                    int int_val = row.get_int(column_idx);
                    member.set(obj, int_val != 0);
                } catch (...) {
                    std::string str_val = row.get_text(column_idx);
                    std::transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
                    member.set(obj, str_val == "true" || str_val == "1" || str_val == "yes" || str_val == "on");
                }
            } else {
                member.set(obj, static_cast<MemberType>(row.get_int(column_idx)));
            }
        } else if constexpr (std::is_floating_point_v<MemberType>) {
            member.set(obj, static_cast<MemberType>(row.get_double(column_idx)));
        } else if constexpr (std::is_enum_v<MemberType>) {
            // Handle enums
            try {
                member.set(obj, static_cast<MemberType>(row.get_int(column_idx)));
            } catch (...) {
                // Fallback to string-based enum parsing if needed
                throw std::runtime_error("Enum parsing from string not implemented");
            }
        } else {
            // For custom types, try string conversion or require specialization
            static_assert(std::is_constructible_v<MemberType, std::string>, 
                        "Type must be constructible from string or have specialized population");
            member.set(obj, MemberType{row.get_text(column_idx)});
        }
    }

    // Populate object from database row - compile-time version
    template<typename Row>
    static auto populate_from_row(T& obj, const Row& row, int start_column = 0) 
        -> std::expected<void, std::string> {
        
        std::string accumulated_errors;
        int successful_fields = 0;
        
        descriptor::for_each_member([&]<size_t I>(auto member) {
            int column_idx = start_column + static_cast<int>(I);
            
            if (column_idx >= row.get_column_count()) {
                return; // Stop if we run out of columns
            }
            
            try {
                populate_member_from_column(obj, member, row, column_idx);
                successful_fields++;
            } catch (const std::exception& e) {
                if (!accumulated_errors.empty()) accumulated_errors += "; ";
                accumulated_errors += std::format("Field '{}' at column {}: {}", 
                                                 member.get_name(), column_idx, e.what());
            }
        });
        
        // Return error only if ALL fields failed
        if (successful_fields == 0 && !accumulated_errors.empty()) {
            return std::unexpected(accumulated_errors);
        }
        
        return {};
    }
    
    template<typename QueryBuilder>
    static auto create_objects_from_query(std::span<const Row> rows, const QueryBuilder& query_builder) 
        -> std::expected<std::vector<T>, std::string> {
        
        if (rows.empty()) {
            return std::vector<T>{};
        }
        
        // Let reflection system intelligently handle the query context
        if (query_builder.should_use_bulk_population()) {
            // All fields - use optimized bulk creation
            return create_objects_bulk_optimized(rows);
        } else {
            // Partial fields - use context-aware population
            return create_objects_with_query_context(rows, query_builder);
        }
    }

    template<typename QueryBuilder>
    static auto create_object_from_query(const Row& row, const QueryBuilder& query_builder) 
        -> std::expected<T, std::string> {
        
        T obj{};
        
        // Delegate population strategy to reflection system
        auto populate_result = [&]() -> std::expected<void, std::string> {
            if (!query_builder.distinctFields.empty()) {
                auto field_mappings = extract_distinct_mappings(query_builder.distinctFields, row);
                return populate_distinct_fields(obj, row, field_mappings);
            } else if (!query_builder.onlyFields.empty()) {
                auto selected_fields = extract_only_field_names(query_builder.onlyFields);
                return populate_selected_fields(obj, row, std::span{selected_fields});
            } else {
                return populate_from_row(obj, row);
            }
        }();
        
        if (populate_result) {
            return obj;
        } else {
            return std::unexpected(populate_result.error());
        }
    }

    // Helper methods for the reflection system
    template<typename DistinctFields>
    static auto extract_distinct_mappings(const DistinctFields& distinct_fields, const auto& row) 
        -> std::vector<std::pair<size_t, std::string>> {
        return distinct_fields 
            | std::views::enumerate
            | std::views::filter([&row](const auto& indexed_field) { 
                const auto& [idx, fieldAlias] = indexed_field;
                return fieldAlias != nullptr && idx < static_cast<size_t>(row.get_column_count()); 
            })
            | std::views::transform([](const auto& indexed_field) {
                const auto& [idx, fieldAlias] = indexed_field;
                return std::pair{static_cast<size_t>(idx), fieldAlias->getFieldName()};
            })
            | std::ranges::to<std::vector>();
    }

    template<typename OnlyFields>
    static auto extract_only_field_names(const OnlyFields& only_fields) 
        -> std::vector<std::string> {
        return only_fields 
            | std::views::filter([](const auto& field) { return field != nullptr; })
            | std::views::transform([](const auto& field) { return field->getFieldName(); })
            | std::ranges::to<std::vector>();
    }

    template<typename QueryBuilder>
    static auto create_objects_with_query_context(std::span<const Row> rows, const QueryBuilder& query_builder) 
        -> std::expected<std::vector<T>, std::string> {
        
        std::vector<T> result;
        result.reserve(rows.size());
        
        for (size_t i = 0; i < rows.size(); ++i) {
            if (auto obj_result = create_object_from_query(rows[i], query_builder); obj_result) {
                result.emplace_back(std::move(*obj_result));
            } else {
                return std::unexpected(std::format("Failed to create object at row {}: {}", 
                                                i, obj_result.error()));
            }
        }
        
        return result;
    }

    // Populate selected fields only
    template<typename Row>
    static auto populate_selected_fields(T& obj, const Row& row, std::span<const std::string> selected_fields) 
        -> std::expected<void, std::string> {
        
        if (selected_fields.size() > static_cast<size_t>(row.get_column_count())) {
            return std::unexpected(std::format("Selected fields count ({}) exceeds row columns ({})", 
                                            selected_fields.size(), row.get_column_count()));
        }
        
        std::vector<std::string> missing_fields;
        
        for (size_t field_idx = 0; field_idx < selected_fields.size(); ++field_idx) {
            const auto& field_name = selected_fields[field_idx];
            
            bool found = false;
            descriptor::for_each_member([&]<size_t I>(auto member) {
                if (!found && member.get_name() == field_name) {
                    found = true;
                    try {
                        populate_member_from_column(obj, member, row, static_cast<int>(field_idx));
                    } catch (const std::exception& e) {
                        // Log warning but continue
                        std::print(stderr, "Warning: Failed to populate field '{}': {}\n", 
                                field_name, e.what());
                    }
                }
            });
            
            if (!found) {
                missing_fields.push_back(field_name);
            }
        }
        
        if (!missing_fields.empty()) {
            return std::unexpected(std::format("Fields not found in type: {}", 
                                            fmt::join(missing_fields, ", ")));
        }
        
        return {};
    }

    // Helper methods for query context
    template<typename FieldAliasList>
    static auto create_field_mappings_from_distinct(const FieldAliasList& distinct_fields, const auto& row) 
        -> std::vector<std::pair<size_t, std::string>> {
        
        return distinct_fields 
            | std::views::enumerate
            | std::views::filter([&row](const auto& indexed_field) { 
                const auto& [idx, fieldAlias] = indexed_field;
                return fieldAlias != nullptr && idx < static_cast<size_t>(row.get_column_count()); 
            })
            | std::views::transform([](const auto& indexed_field) {
                const auto& [idx, fieldAlias] = indexed_field;
                return std::pair{static_cast<size_t>(idx), fieldAlias->getFieldName()};
            })
            | std::ranges::to<std::vector>();
    }

    template<typename OnlyFieldsList>
    static auto extract_field_names_from_only(const OnlyFieldsList& only_fields) 
        -> std::vector<std::string> {
        
        return only_fields 
            | std::views::filter([](const auto& field) { return field != nullptr; })
            | std::views::transform([](const auto& field) { return field->getFieldName(); })
            | std::ranges::to<std::vector>();
    }

    static auto validate_field_names(std::span<const std::string> field_names) 
        -> std::expected<void, std::string> {
        
        auto valid_names = descriptor::get_member_names();
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
            return std::unexpected(std::format("Invalid field names: {}. Valid fields: {}", 
                                            fmt::join(invalid_fields, ", "),
                                            fmt::join(valid_names, ", ")));
        }
        
        return {};
    }

    // Populate distinct fields with column mappings
    template<typename Row>
    static auto populate_distinct_fields(T& obj, const Row& row, 
                                       const std::vector<std::pair<size_t, std::string>>& field_mappings) 
        -> std::expected<void, std::string> {
        
        for (const auto& [column_idx, field_name] : field_mappings) {
            bool found = false;
            
            descriptor::for_each_member([&]<size_t I>(auto member) {
                if (!found && member.get_name() == field_name) {
                    found = true;
                    
                    if (static_cast<int>(column_idx) >= row.get_column_count()) {
                        return;
                    }
                    
                    try {
                        using MemberType = typename decltype(member)::member_type;
                        
                        if constexpr (std::is_same_v<MemberType, std::string>) {
                            member.set(obj, row.get_text(static_cast<int>(column_idx)));
                        } else if constexpr (std::is_same_v<MemberType, int>) {
                            member.set(obj, row.get_int(static_cast<int>(column_idx)));
                        } else if constexpr (std::is_same_v<MemberType, double>) {
                            member.set(obj, row.get_double(static_cast<int>(column_idx)));
                        } else if constexpr (std::is_same_v<MemberType, bool>) {
                            try {
                                int int_val = row.get_int(static_cast<int>(column_idx));
                                member.set(obj, int_val != 0);
                            } catch (...) {
                                std::string str_val = row.get_text(static_cast<int>(column_idx));
                                std::transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
                                member.set(obj, str_val == "true" || str_val == "1" || str_val == "yes");
                            }
                        }
                    } catch (const std::exception& e) {
                        return;
                    }
                }
            });
            
            if (!found) {
                return std::unexpected{std::format("Field '{}' not found in type", field_name)};
            }
        }
        
        return {};
    }
    
    // Bulk object creation from rows (optimized for select_all)
    template<typename Row>
    static auto create_objects_from_rows(std::span<const Row> rows) 
        -> std::expected<std::vector<T>, std::string> {
        
        if (rows.empty()) {
            return std::vector<T>{};
        }
        
        std::vector<T> result;
        result.reserve(rows.size());
        
        for (size_t i = 0; i < rows.size(); ++i) {
            T obj{};
            if (auto populate_result = populate_from_row(obj, rows[i], 0); populate_result) {
                result.emplace_back(std::move(obj));
            } else {
                return std::unexpected(std::format("Failed to populate object at row {}: {}", 
                                                 i, populate_result.error()));
            }
        }
        
        return result;
    }
    
    // Get field names as vector
    static auto get_field_names() -> std::vector<std::string_view> {
        auto names = descriptor::get_member_names();
        return std::vector<std::string_view>(names.begin(), names.end());
    }
};

} // namespace refl

// ============================================================================
// REGISTRATION MACRO FOR COMPILE-TIME REFLECTION
// ============================================================================
#define REFL_DEFINE_TYPE(Type, TypeNameStr, ...) \
    namespace refl { \
        template<> \
        struct type_info<Type> { \
            static constexpr bool has_reflection = true; \
            using descriptor = ::refl::meta::type_descriptor< \
                Type, \
                ::refl::detail::fixed_string{TypeNameStr}, \
                __VA_ARGS__ \
            >; \
        }; \
    }

#define REFL_MEMBER(Type, member) \
    ::refl::meta::member_descriptor<&Type::member, ::refl::detail::fixed_string{#member}>

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

// // ============================================================================
// // UTILITY FUNCTIONS
// // ============================================================================
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
//     std::println("Object state: {}", refl::reflect<T>::to_string(obj));
    
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
//         return std::max({text_data.size(), int_data.size(), double_data.size()}); 
//     }
    
//     std::string get_text(int col) const { 
//         return col < text_data.size() ? text_data[col] : ""; 
//     }
    
//     int get_int(int col) const { 
//         return col < int_data.size() ? int_data[col] : 0; 
//     }
    
//     double get_double(int col) const { 
//         return col < double_data.size() ? double_data[col] : 0.0; 
//     }
// };

// void demo_database_integration() {
//     std::println("\n=== Database Integration Demo ===");
    
//     // Create mock database rows
//     std::vector<MockRow> rows = {
//         MockRow({"Alice Johnson", "", "", "true"}, {0, 28, 0, 1}, {0.0, 0.0, 75000.0, 0.0}),
//         MockRow({"Bob Smith", "", "", "false"}, {0, 35, 0, 0}, {0.0, 0.0, 82000.0, 0.0}),
//         MockRow({"Carol Davis", "", "", "true"}, {0, 31, 0, 1}, {0.0, 0.0, 68000.0, 0.0})
//     };
    
//     // Test create_objects_from_rows (like select_all)
//     std::println("Creating objects from rows...");
//     auto persons = refl::reflect<Person>::create_objects_from_rows(std::span{rows});
    
//     for (const auto& person : persons) {
//         std::println("  {}", refl::reflect<Person>::to_string(person));
//     }
    
//     // Test create_value_map_from_row (like select_value)
//     std::println("\nCreating value map from first row...");
//     auto value_map = refl::reflect<Person>::create_value_map_from_row(rows[0]);
    
//     std::println("Value map contents:");
//     for (const auto& [key, value] : value_map) {
//         std::print("  {}: ", key);
//         try {
//             if (auto str_val = std::any_cast<std::string>(&value); str_val) {
//                 std::println("\"{}\"", *str_val);
//             } else if (auto int_val = std::any_cast<int>(&value); int_val) {
//                 std::println("{}", *int_val);
//             } else if (auto double_val = std::any_cast<double>(&value); double_val) {
//                 std::println("{}", *double_val);
//             } else {
//                 std::println("[unknown type]");
//             }
//         } catch (...) {
//             std::println("[any_cast failed]");
//         }
//     }
    
//     // Test create_value_maps_from_rows (like select_values)
//     std::println("\nCreating value maps from all rows...");
//     auto value_maps = refl::reflect<Person>::create_value_maps_from_rows(std::span{rows});
    
//     std::println("Created {} value maps", value_maps.size());
    
//     // Test populate_selected_fields
//     std::println("\nTesting populate_selected_fields...");
//     Person partial_person{};
//     std::vector<std::string> selected_fields = {"name", "age"};
    
//     if (auto result = refl::reflect<Person>::populate_selected_fields(
//         partial_person, rows[0], std::span{selected_fields}); result) {
//         std::println("Partially populated person: {}", refl::reflect<Person>::to_string(partial_person));
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
//         std::println("Distinct populated person: {}", refl::reflect<Person>::to_string(distinct_person));
//     } else {
//         std::println("Failed to populate distinct fields: {}", result.error());
//     }
// }
// ============================================================================
// MAIN DEMO
// ============================================================================
// int main() {
//     std::println("=== C++26 Style Compile-Time Reflection Demo ===\n");
    
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
//         std::println("Updated person: {}", refl::reflect<Person>::to_string(person));
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
    
//     std::println("person == person2: {}", refl::reflect<Person>::equals(person, person2));
//     std::println("person == person3: {}", refl::reflect<Person>::equals(person, person3));
    
//     // Test object creation
//     std::println("\n=== Object Creation Test ===");
//     auto created_person = refl::reflect<Person>::make("Charlie Brown", 35, 85000.0, false);
//     std::println("Created person: {}", refl::reflect<Person>::to_string(created_person));
    
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
=== C++26 Style Compile-Time Reflection Demo ===

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
*/