module;

// Define the module
export module storm.reflect;
import storm.utils;
import storm.core_types;

// Import standard header units
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

// Convert SqlValue to actual SQL binding
template <bindable_statement Statement>
auto bind_variant(Statement& stmt, int param_index, const storm::SqlValue& value) -> std::expected<void, std::string> {
    return std::visit(
            [&](const auto& v) -> std::expected<void, std::string> { return stmt.bind(param_index, v); }, value
    );
}

// ============================================================================
// COMPILE-TIME STRING UTILITIES
// ============================================================================
export namespace refl::detail {

    template <size_t N> struct fixed_string {
        consteval fixed_string(const char (&str)[N]) {
            std::copy_n(str, N, value);
        }

        consteval fixed_string(std::string_view sv) {
            if (sv.size() >= N) {
                std::copy_n(sv.data(), N - 1, value);
                value[N - 1] = '\0';
            } else {
                std::copy_n(sv.data(), sv.size(), value);
                value[sv.size()] = '\0';
            }
        }

        char value[N];

        consteval operator std::string_view() const {
            return std::string_view(value);
        }

        consteval const char* c_str() const {
            return value;
        }
        consteval size_t size() const {
            return N - 1;
        }

        consteval bool operator==(const fixed_string& other) const {
            return std::string_view(*this) == std::string_view(other);
        }
    };

    template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace refl::detail

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

        static consteval std::string_view get_name() {
            return std::string_view(name);
        }

        static consteval auto get(const class_type& obj) -> const member_type& {
            return obj.*member_ptr;
        }

        static consteval auto get(class_type& obj) -> member_type& {
            return obj.*member_ptr;
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

        static consteval const char* type_name() {
            return typeid(member_type).name();
        }
    };

    // Compile-time type descriptor
    template <typename T, auto TypeName, typename... Members> struct type_descriptor {
        using type                         = T;
        static constexpr auto name         = TypeName;
        static constexpr auto member_count = sizeof...(Members);
        using members_tuple                = std::tuple<Members...>;

        static consteval std::string_view get_name() {
            return std::string_view(name);
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

        template <typename F> static consteval void for_each_member(F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, F&& f) {
                (f(std::get<Is>(members_tuple{})), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
        }

        // New method that passes both member descriptor and name to the callback
        template <typename F> static consteval void for_each_member_with_name(F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, F&& f) {
                (f(std::get<Is>(members_tuple{}), std::get<Is>(members_tuple{}).get_name()), ...);
            }(std::make_index_sequence<member_count>{}, std::forward<F>(func));
        }

        template <typename F> static consteval void visit_members(const T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, const T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members).get_name(), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        // New method that passes member descriptor directly to the callback
        template <typename F> static consteval void visit_members_with_descriptor(const T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, const T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        template <typename F> static consteval void visit_members(T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members).get_name(), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        // New method that passes member descriptor directly to the callback (mutable
        // version)
        template <typename F> static consteval void visit_members_with_descriptor(T& obj, F&& func) {
            []<size_t... Is>(std::index_sequence<Is...>, T& o, F&& f) {
                auto members = members_tuple{};
                (f(std::get<Is>(members), std::get<Is>(members).get(o)), ...);
            }(std::make_index_sequence<member_count>{}, obj, std::forward<F>(func));
        }

        static consteval auto find_member_index(std::string_view name) -> std::optional<size_t> {
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
export namespace refl {

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

        static consteval std::string_view name() {
            return descriptor::get_name();
        }

        static consteval std::string_view get_struct_name() {
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

        template <typename F> static consteval void for_each_member(F&& func) {
            descriptor::for_each_member(std::forward<F>(func));
        }

        template <typename F> static consteval void visit_members(const T& obj, F&& func) {
            descriptor::visit_members(obj, std::forward<F>(func));
        }

        template <typename F> static consteval void visit_members(T& obj, F&& func) {
            descriptor::visit_members(obj, std::forward<F>(func));
        }

        // New methods that pass member descriptor directly to the callback
        template <typename F> static consteval void visit_members_with_descriptor(const T& obj, F&& func) {
            descriptor::visit_members_with_descriptor(obj, std::forward<F>(func));
        }

        template <typename F> static consteval void visit_members_with_descriptor(T& obj, F&& func) {
            descriptor::visit_members_with_descriptor(obj, std::forward<F>(func));
        }

        // New method that passes both member descriptor and name to the callback
        template <typename F> static consteval void for_each_member_with_name(F&& func) {
            descriptor::for_each_member_with_name(std::forward<F>(func));
        }

        static auto get_member_value(const T& obj, std::string_view name) -> std::expected<std::any, std::string> {
            std::optional<std::any> result;

            descriptor::visit_members(obj, [&](std::string_view member_name, const auto& value) {
                if (member_name == name) {
                    result = std::make_any<std::decay_t<decltype(value)>>(value);
                }
            });

            return result.has_value() ? std::expected<std::any, std::string>{*result}
                                      : std::unexpected<std::string>{"Member not found"};
        }

        // Get field names as vector
        static auto get_field_names() -> std::expected<std::vector<std::string_view>, std::string> {
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

        // Helper to get member name from descriptor
        template <typename MemberDesc> static consteval std::string_view get_member_name(const MemberDesc& member) {
            return member.get_name();
        }

        template <typename Predicate>
        static consteval auto get_member_names_if(Predicate&& predicate)
                -> std::expected<std::vector<std::string>, std::string> {
            std::vector<std::string> filtered_names;

            // Get all member names at compile time
            constexpr auto all_names = descriptor::get_member_names();

            // Filter at compile time where possible
            for (constexpr auto name : all_names) {
                if constexpr (std::is_invocable_r_v<bool, Predicate, std::string_view>) {
                    if (predicate(name)) {
                        filtered_names.emplace_back(name);
                    }
                }
            }

            return filtered_names;
        }

        /**
         * Check if a member is a primary key at compile time
         *
         * @tparam Member Member descriptor to check
         * @return True if the member is a primary key
         */
        template <auto Member> static consteval bool is_primary_key() {
            // For now, we consider a field to be a primary key if its name is "id"
            // This can be extended later with attributes or other mechanisms
            return std::string_view(Member.get_name()) == "id";
        }

        /**
         * Check if a field name represents a primary key
         *
         * @param field_name Field name to check
         * @return True if the field is a primary key
         */
        static bool is_primary_key(std::string_view field_name) {
            // For now, we consider a field to be a primary key if its name is "id"
            // This can be extended later with attributes or other mechanisms
            return field_name == "id";
        }

        template <size_t I = 0> static consteval auto get_primary_key_member() {
            if constexpr (I < member_count()) {
                auto member = descriptor::template get_member<I>();
                if constexpr (is_primary_key(member.get_name())) {
                    return member;
                } else {
                    return get_primary_key_member<I + 1>();
                }
            } else {
                // Return empty member descriptor or handle no-PK case
                static_assert(I < member_count(), "No primary key field found");
            }
        }
    };

} // namespace refl

// ============================================================================
// REGISTRATION MACRO FOR COMPILE-TIME REFLECTION
// ============================================================================

// Define reflection for a class with members
#define REFLECT_CLASS(ClassName, ...)                                                                                  \
    template <> struct refl::type_info<ClassName> {                                                                    \
        static consteval bool has_reflection = true;                                                                   \
        using descriptor                     = refl::meta::type_descriptor<ClassName, #ClassName, __VA_ARGS__>;        \
    };

// Define a member for reflection
#define REFLECT_MEMBER(Class, Member) refl::meta::member_descriptor<&Class::Member, #Member>
