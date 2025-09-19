module;

// Module global fragment - third-party C headers (macros not exported by modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.selectable_query;

// Import required modules
import storm.base_query;
import storm.core_types; // For SqlValue, ValueMap, etc.
import storm.statement.select;
import storm.utils;
import storm.reflect; // For FieldWrapper

// Import standard header units
import <string>;
import <utility>;
import <vector>;
import <expected>;
import <flat_map>;
import <functional>;

export namespace storm {
    // Local type alias for return type consistency
    using ValueMap = std::flat_map<std::string, SqlValue, std::less<>>;

    template <class T> class SelectableQuery : public BaseQuery<T> {
      protected:
        std::vector<std::pair<refl::FieldWrapper, std::string_view>> onlyFields;

      public:
        using BaseQuery<T>::BaseQuery;

        SelectableQuery(const SelectableQuery& other) : BaseQuery<T>(other), onlyFields(other.onlyFields) {}

        SelectableQuery(SelectableQuery&& other) noexcept = default;

        SelectableQuery& operator=(const SelectableQuery& other) {
            if (this != &other) {
                BaseQuery<T>::operator=(other);
                onlyFields = other.onlyFields;
            }
            return *this;
        }

        SelectableQuery& operator=(SelectableQuery&& other) noexcept = default;

        // ONLY API (C++26 upgraded declarations with function parameter deduction)
        // Simple version: .only(field1, field2, ...)
        template <typename Self>
        auto&& only(this Self&& self, auto... fields)
            requires(sizeof...(fields) > 0) && (requires { decltype(fields)::member_ptr; } && ...);

        // Overloaded version for field-alias pairs: .only(field, alias, field, alias, ...)
        template <typename Self>
        auto&& only(this Self&& self, auto first_field, auto first_alias, auto... rest)
            requires(sizeof...(rest) % 2 == 0) && requires { decltype(first_field)::member_ptr; } &&
                    (!requires { decltype(first_alias)::member_ptr; });

        // C++26 SELECT API with compile-time query validation and type safety
        std::expected<std::vector<T>, std::string> select_all()
            requires refl::reflectable<T>;
        std::expected<T, std::string> select_one()
            requires refl::reflectable<T>;
        std::expected<std::vector<ValueMap>, std::string> select_values()
            requires refl::reflectable<T>;

      protected:
        // Helper method to build SelectOptions
        [[nodiscard]] SelectOptions build_select_options() const {
            return SelectOptions{
                    .distinct_fields = this->distinctFields,
                    .only_fields     = onlyFields,
                    .order_terms     = this->orderTerms,
                    .group_by_fields = this->groupByFields,
                    .limit           = this->query_flags.limit,
                    .offset          = this->query_flags.offset,
                    .where_clause    = this->_whereExpression,
            };
        }
    };

    // C++26 ONLY implementation - Simple version: .only(field1, field2, ...)
    template <typename T>
    template <typename Self>
    auto&& SelectableQuery<T>::only(this Self&& self, auto... fields)
        requires(sizeof...(fields) > 0) && (requires { decltype(fields)::member_ptr; } && ...)
    {
        // Optimize container capacity
        auto field_count = sizeof...(fields);
        self.onlyFields.reserve(self.onlyFields.size() + field_count);

        // Create wrappers directly in fold expression
        (self.onlyFields.emplace_back(refl::FieldWrapper::create(fields), std::string_view{}), ...);

        return std::forward<Self>(self);
    }

    // C++26 ONLY implementation - Overloaded version for field-alias pairs: .only(field, alias, field, alias, ...)
    template <typename T>
    template <typename Self>
    auto&& SelectableQuery<T>::only(this Self&& self, auto first_field, auto first_alias, auto... rest)
        requires(sizeof...(rest) % 2 == 0) && requires { decltype(first_field)::member_ptr; } &&
                (!requires { decltype(first_alias)::member_ptr; })
    {
        // Reserve space for all pairs
        const auto pair_count = sizeof...(rest) / 2 + 1;
        self.onlyFields.reserve(self.onlyFields.size() + pair_count);

        // Process first pair
        self.onlyFields.emplace_back(refl::FieldWrapper::create(first_field), first_alias);

        // Process remaining pairs using index sequence
        if constexpr (sizeof...(rest) > 0) {
            auto args_tuple = std::make_tuple(rest...);
            [&self, &args_tuple]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((self.onlyFields.emplace_back(
                         refl::FieldWrapper::create(std::get<Is * 2>(args_tuple)), std::get<Is * 2 + 1>(args_tuple)
                 )),
                 ...);
            }(std::make_index_sequence<sizeof...(rest) / 2>{});
        }

        return std::forward<Self>(self);
    }

    // SELECT ONE implementation (returns single object with LIMIT 1)
    template <typename T>
    std::expected<T, std::string> SelectableQuery<T>::select_one()
        requires refl::reflectable<T>
    {
        return std::move(*this).limit(1).select_all().and_then([](const auto& rows) -> std::expected<T, std::string> {
            if (rows.empty()) {
                return std::unexpected("No results found for select_one query");
            }
            return rows[0];
        });
    }

    // SELECT ALL implementation
    template <typename T>
    std::expected<std::vector<T>, std::string> SelectableQuery<T>::select_all()
        requires refl::reflectable<T>
    {
        auto opts = build_select_options();
        return SelectStatement<T>(this->conn, std::move(opts)).execute_objects();
    }

    // SELECT VALUES implementation (returns dictionary-like data)
    template <typename T>
    [[nodiscard]] auto SelectableQuery<T>::select_values() -> std::expected<std::vector<ValueMap>, std::string>
        requires refl::reflectable<T>
    {
        // Pre-calculate capacity hints
        const auto expected_size = this->estimate_result_size();

        auto opts = build_select_options();
        auto stmt = SelectStatement<T>(this->conn, std::move(opts));
        stmt.reserve_hint(expected_size);
        return stmt.execute_values();
    }

} // namespace storm