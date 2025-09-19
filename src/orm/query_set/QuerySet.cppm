module;

// Module global fragment - third-party C headers (macros not exported by
// modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.query_set;

// Import the new hierarchy
import storm.joinable_query;

// Import required modules for CRUD operations
import storm.statement.insert;
import storm.statement.update;
import storm.statement.remove;
import storm.reflect;

// Import standard header units
import <string>;
import <vector>;
import <expected>;
import <ranges>;
import <span>;
import <type_traits>;

export namespace storm {
    // Concept for contiguous ranges containing elements of type T
    template <typename R, typename T>
    concept ContiguousRangeOf = std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
                                std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, T>;

    // Concept for validating member pointers and convertible values
    template <auto MemberPtr, typename T, typename Value>
    concept MemberUpdateCompatible =
            std::is_member_pointer_v<decltype(MemberPtr)> &&
            std::same_as<typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::class_type, T> &&
            std::convertible_to<Value, typename refl::meta::member_pointer_traits<decltype(MemberPtr)>::member_type>;

    template <class T> class QuerySet : public JoinableQuery<T> {
      public:
        // Inherit constructors
        using JoinableQuery<T>::JoinableQuery;

        // C++26 REMOVE API with compile-time validation and constraint checking
        std::expected<bool, std::string> remove(const T& obj);
        std::expected<bool, std::string> remove(std::span<const T> objects);
        std::expected<bool, std::string> remove();
        // C++26 generic contiguous range for remove
        template <ContiguousRangeOf<T> R> std::expected<bool, std::string> remove(R&& objects);

        // C++26 UPDATE API with compile-time validation and concepts
        std::expected<bool, std::string> update(const T& obj);
        std::expected<bool, std::string> update(std::span<const T> objects);
        // C++26 generic contiguous range for update
        template <ContiguousRangeOf<T> R> std::expected<bool, std::string> update(R&& objects);

        // C++26 field-specific update with compile-time validation
        template <auto MemberPtr, typename Value>
            requires MemberUpdateCompatible<MemberPtr, T, Value>
        std::expected<bool, std::string> update(Value&& value);

        // C++26 INSERT API with compile-time validation and zero-overhead abstractions
        std::expected<int, std::string>              insert(const T& obj);
        std::expected<std::vector<int>, std::string> insert(std::span<const T> objects);
        // C++26 generic contiguous range with enhanced type safety
        template <ContiguousRangeOf<T> R> std::expected<std::vector<int>, std::string> insert(R&& objects);

        // C++26 compile-time statement preparation with reflection validation
        InsertStatement<T> stmt_insert(const T& obj);
        InsertStatement<T> stmt_insert(std::span<const T> objs);

      private:
        // Deducing this for perfect forwarding
        template <typename Self> static constexpr decltype(auto) self_cast(Self&& self) {
            return std::forward<Self>(self);
        }

        [[nodiscard]] std::expected<std::vector<int>, std::string>
        execute_insert(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return std::vector<int>{};
            return InsertStatement<T>(this->conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_update(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return false;
            return UpdateStatement<T>(this->conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_delete(std::span<const T> objects) const noexcept {
            if (objects.empty())
                return false;
            return DeleteStatement<T>(this->conn).execute(objects);
        }

        [[nodiscard]] std::expected<bool, std::string> execute_delete() noexcept {
            return DeleteStatement<T>(this->conn, std::move(this->_whereExpression)).execute();
        }
    };

    // UPDATE implementation
    // 1. Single object - handles move
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(const T& obj) {
        return execute_update(std::span<const T>{&obj, 1});
    }

    // 2. Batch update - modern span-based API
    template <typename T> std::expected<bool, std::string> QuerySet<T>::update(std::span<const T> objects) {
        return execute_update(objects);
    }

    // INSERT implementation
    // === MINIMAL NECESSARY OVERLOADS ===

    // 1. Single object - handles move
    template <typename T> std::expected<int, std::string> QuerySet<T>::insert(const T& obj) {
        auto result = execute_insert(std::span<const T>{&obj, 1});
        if (!result) {
            return std::unexpected(result.error());
        }
        if (result->empty()) {
            return std::unexpected("No ID returned from insert");
        }
        return result->front();
    }

    // 2. Batch insert - modern span-based API
    template <typename T> std::expected<std::vector<int>, std::string> QuerySet<T>::insert(std::span<const T> objects) {
        return execute_insert(objects);
    }

    // 3. Generic contiguous range - forwards to span<const T>
    template <typename T>
    template <ContiguousRangeOf<T> R>
    std::expected<std::vector<int>, std::string> QuerySet<T>::insert(R&& objects) {
        return execute_insert(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
    }

    // Single object REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(const T& obj) {
        return execute_delete(std::span<const T>{&obj, 1});
    }

    // Batch REMOVE implementation
    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove(std::span<const T> objects) {
        return execute_delete(objects);
    }

    template <typename T> std::expected<bool, std::string> QuerySet<T>::remove() {
        return execute_delete();
    }

    // Generic contiguous range REMOVE - forwards to span<const T>
    template <typename T>
    template <ContiguousRangeOf<T> R>
    std::expected<bool, std::string> QuerySet<T>::remove(R&& objects) {
        return execute_delete(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
    }

    template <typename T> InsertStatement<T> QuerySet<T>::stmt_insert(const T& obj) {
        return InsertStatement<T>(this->conn);
    }

    template <typename T> InsertStatement<T> QuerySet<T>::stmt_insert(std::span<const T> objs) {
        return InsertStatement<T>(this->conn);
    }

    // Generic contiguous range UPDATE - forwards to span<const T>
    template <typename T>
    template <ContiguousRangeOf<T> R>
    std::expected<bool, std::string> QuerySet<T>::update(R&& objects) {
        return execute_update(std::span<const T>{std::ranges::data(objects), std::ranges::size(objects)});
    }

    // Field-specific update implementation
    template <typename T>
    template <auto MemberPtr, typename Value>
        requires MemberUpdateCompatible<MemberPtr, T, Value>
    std::expected<bool, std::string> QuerySet<T>::update(Value&& value) {
        // This would need to be implemented with a specialized UpdateStatement
        // For now, return error indicating this feature needs implementation
        return std::unexpected("Field-specific update not yet implemented");
    }

} // namespace storm
