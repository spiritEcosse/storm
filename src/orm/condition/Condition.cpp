module;

// Module implementation unit
module storm.condition;

import <string>;
import <string_view>;
import <stdexcept>;
import <type_traits>;
import <iostream>;
import <memory>;
import <variant>;

// Import required modules
import storm.core_types; // For SqlValue, Op, Collation
import storm.parameter_binder;

namespace storm {

// Custom exceptions for better error handling (internal to implementation unit)
class UnknownCollationException : public std::runtime_error {
public:
  UnknownCollationException() : std::runtime_error("Unknown collation type") {}
};

class UnknownOperatorException : public std::runtime_error {
public:
  UnknownOperatorException() : std::runtime_error("Unknown operator type") {}
};

// Convert collation enum to SQL string
std::string_view collation_to_sql(Collation collation) {
  using enum Collation;
  switch (collation) {
  case BINARY:
    return "BINARY";
  case NOCASE:
    return "NOCASE";
  case RTRIM:
    return "RTRIM";
  case NONE:
    return "";
  default:
    throw UnknownCollationException();
  }
}

// Convert operator enum to SQL string
std::string_view op_to_sql(Op op) {
  using enum Op;
  switch (op) {
  case EQ:
    return "=";
  case NE:
    return "!=";
  case GT:
    return ">";
  case LT:
    return "<";
  case GE:
    return ">=";
  case LE:
    return "<=";
  case LIKE:
    return "LIKE";
  case IS:
    return "IS";
  case IS_NOT:
    return "IS NOT";
  case IN:
    return "IN";
  case BETWEEN:
    return "BETWEEN";
  default:
    throw UnknownOperatorException();
  }
}

// Render SQL for this condition using safe parameter binding
std::string Condition::to_sql(ParameterBinder &binder) const {
  std::string sql = field_name_;

  // Add collation if specified (for string comparisons)
  if (collation_ != Collation::NONE) {
    sql += " COLLATE " + std::string(collation_to_sql(collation_));
  }

  sql += " " + std::string(op_to_sql(operator_)) + " ";

  if (operator_ == Op::BETWEEN) {
    std::string param1 = value_to_sql_safe(value_, binder);
    std::string param2 = value_to_sql_safe(*value2_, binder);
    sql += param1 + " AND " + param2;
  } else if (operator_ == Op::IS &&
             std::holds_alternative<std::nullopt_t>(value_)) {
    // Special case for IS NULL
    sql += "NULL";
    std::cout << "IS NULL" << std::endl;
  } else {
    sql += value_to_sql_safe(value_, binder);
  }

  return sql;
}

std::unique_ptr<Expression> Condition::clone() const {
  if (value2_) {
    return std::make_unique<Condition>(field_name_, value_, *value2_,
                                       collation_);
  }
  return std::make_unique<Condition>(field_name_, operator_, value_,
                                     collation_);
}

// SAFE parameter binding - no direct string interpolation
std::string Condition::value_to_sql_safe(const SqlValue &val,
                                         ParameterBinder &binder) const {
  return std::visit(
      [&binder]<typename T>(const T &v) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<T>, std::nullopt_t>) {
          return "NULL"; // NULL doesn't need parameterization
        } else {
          // All other values get parameterized
          std::string param_name = binder.add_parameter(SqlValue{v});
          return ":" + param_name; // Using :param syntax (adjust for your DB)
        }
      },
      val);
}

} // namespace storm
