module;

export module storm.core_types;

import <string>;
import <optional>;
import <variant>;
import <string_view>;
import <cstdint>;

export namespace storm {

    // Core value types that can be used in both WHERE clauses and database binding
    using SqlValue = std::variant<
            std::string,
            int8_t,
            int16_t,
            int32_t,
            int64_t,
            uint8_t,
            uint16_t,
            uint32_t,
            uint64_t,
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
