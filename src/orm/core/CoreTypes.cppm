module;

// Module global fragment
// (no legacy standard includes)

// Define the module
export module storm.core_types;

// Import standard header units
import <string>;
import <optional>;
import <variant>;
import <string_view>;

export namespace storm {

// Core value types that can be used in WHERE clauses and other SQL operations
using SqlValue = std::variant<
    std::string,
    int, long, long long,
    float, double,
    bool,
    std::nullopt_t
>;

// Compile-time operator types for type safety
enum class Op {
    EQ, NE, GT, LT, GE, LE, LIKE, IS, IN, BETWEEN, IS_NOT
};

// Collation options for string comparisons
enum class Collation {
    NONE, BINARY, NOCASE, RTRIM
};

} // namespace storm
