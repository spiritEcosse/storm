export module storm.core_types;

// Standard library imports
import <string>;
import <optional>;
import <variant>;
import <string_view>;
import <cstdint>;

// Storm modules - removed reflection dependency to break circular imports
import storm.type_traits;

export namespace storm {

    // Core value types that can be used in both WHERE clauses and database binding
    // These types have no reflection dependencies
    using SqlValue = std::variant<
            std::string,
            std::int8_t,
            std::int16_t,
            std::int32_t,
            std::int64_t,
            std::uint8_t,
            std::uint16_t,
            std::uint32_t,
            std::uint64_t,
            float,
            double,
            bool,
            std::nullopt_t,
            std::nullptr_t,
            std::monostate>;

    // Compile-time operator types for type safety
    enum class Op { EQ, NE, GT, LT, GE, LE, LIKE, IS, IN, BETWEEN, IS_NOT };

    // Collation options for string comparisons
    enum class Collation { NONE, BINARY, NOCASE, RTRIM };

} // namespace storm
