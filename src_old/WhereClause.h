#pragma once

#include "BaseClass.h"
#include "BaseField.h"

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <sstream>
#include <iostream>
#include <fmt/format.h>

enum class Operator { EQUALS, NOT_EQUALS, GREATER_THAN, LESS_THAN, GREATER_OR_EQUAL, LESS_OR_EQUAL, LIKE, IS };

inline std::string operatorToString(const Operator op) {
    using enum Operator;

    switch (op) {
    case EQUALS:
        return "=";
    case NOT_EQUALS:
        return "!=";
    case GREATER_THAN:
        return ">";
    case LESS_THAN:
        return "<";
    case GREATER_OR_EQUAL:
        return ">=";
    case LESS_OR_EQUAL:
        return "<=";
    case LIKE:
        return "LIKE";
    case IS:
        return "IS";
    default:
        throw std::invalid_argument("Unknown operator");
    }
}

inline std::string boolToString(const bool value) {
    return value ? "TRUE" : "FALSE";
}

namespace orm {
    class WhereClause final : public BaseClass {
      public:
        enum class LogicalType { SINGLE, AND, OR };
        enum class ValueType { STRING, SPECIAL, FIELD_COMPARISON, RAW_SQL };

        WhereClause(const BaseField* field, std::string value, const Operator op = Operator::EQUALS)
            : BaseClass(), _field(field), _value(std::move(value)), _op(op), _valueType(ValueType::STRING) {}

        WhereClause(const BaseField* field, const std::optional<bool> value, const Operator op = Operator::EQUALS)
            : BaseClass()
            , _field(field)
            , _value(value ? boolToString(value.value()) : "NULL")
            , _op(op)
            , _valueType(ValueType::SPECIAL) {}

        WhereClause(const BaseField* field1, const BaseField* field2)
            : BaseClass()
            , _field(field1)
            , _comparisonField(field2)
            , _op(Operator::EQUALS)
            , _valueType(ValueType::FIELD_COMPARISON) {}

        WhereClause(LogicalType type, std::vector<WhereClause> subclauses, bool isGrouped = false)
            : BaseClass()
            , _field(nullptr)
            , _op()
            , _valueType()
            , _needsGrouping(isGrouped)
            , _type(type)
            , _subclauses(std::move(subclauses)) {}

        // Modified group method that creates a new wrapper clause
        static WhereClause createGroup(WhereClause clause) {
            std::vector<WhereClause> subclauses;
            subclauses.push_back(std::move(clause));
            return WhereClause(LogicalType::SINGLE, std::move(subclauses), true);
        }

        static WhereClause rawSql(const BaseField* field, std::string sql, const Operator op = Operator::EQUALS) {
            WhereClause clause(field, std::move(sql), op);
            clause._valueType = ValueType::RAW_SQL;
            return clause;
        }

        // Modified operator overloads
        friend WhereClause operator&(WhereClause lhs, WhereClause rhs) {
            // If we're already building an AND clause, just append
            if (lhs._type == LogicalType::AND && lhs._field == nullptr) {
                lhs._subclauses.push_back(std::move(rhs));
                return std::move(lhs);
            }

            // Otherwise, create a new AND clause
            std::vector<WhereClause> subclauses;
            subclauses.push_back(std::move(lhs));
            subclauses.push_back(std::move(rhs));
            return WhereClause(LogicalType::AND, std::move(subclauses));
        }

        friend WhereClause operator|(WhereClause lhs, WhereClause rhs) {
            // If we're already building an OR clause, just append
            if (lhs._type == LogicalType::OR && lhs._field == nullptr) {
                lhs._subclauses.push_back(std::move(rhs));
                return std::move(lhs);
            }

            // Otherwise, create a new OR clause
            std::vector<WhereClause> subclauses;
            subclauses.push_back(std::move(lhs));
            subclauses.push_back(std::move(rhs));
            return WhereClause(LogicalType::OR, std::move(subclauses));
        }

        std::string serialize() const {
            std::stringstream ss;
            serializeRecursive(ss);
            return ss.str();
        }

        // Add this as a public method to WhereClause class
        WhereClause&& group() && {
            _needsGrouping = true;
            return std::move(*this);
        }

      private:
        // Modified serialization
        void serializeRecursive(std::stringstream& ss) const {
            // If this is a grouping wrapper, just serialize the first subclause with forced parentheses
            if (_field == nullptr && _type == LogicalType::SINGLE && _subclauses.size() == 1 && _needsGrouping) {
                _subclauses[0].serializeRecursive(ss);
                return;
            }

            // For real clauses with multiple subclauses
            bool needParentheses = _needsGrouping || (_type != LogicalType::SINGLE && _subclauses.size() > 1);

            if (needParentheses) {
                ss << "(";
            }

            // If this is a logical operator clause
            if (_field == nullptr) {
                if (!_subclauses.empty()) {
                    _subclauses[0].serializeRecursive(ss);

                    for (size_t i = 1; i < _subclauses.size(); ++i) {
                        ss << (_type == LogicalType::AND ? " AND " : " OR ");
                        _subclauses[i].serializeRecursive(ss);
                    }
                }
            } else {
                // This is a leaf clause
                ss << serializeSingleClause();

                // Handle any subclauses
                if (!_subclauses.empty()) {
                    for (const auto& subclause : _subclauses) {
                        ss << (_type == LogicalType::AND ? " AND " : " OR ");
                        subclause.serializeRecursive(ss);
                    }
                }
            }

            if (needParentheses) {
                ss << ")";
            }
        }

        std::string serializeSingleClause() const {
            switch (_valueType) {
            case ValueType::FIELD_COMPARISON:
                return fmt::format(
                        "{} {} {}",
                        _field->getFullFieldName(),
                        operatorToString(_op),
                        _comparisonField->getFullFieldName()
                );

            case ValueType::STRING:
                return fmt::format("{} {} '{}'", _field->getFullFieldName(), operatorToString(_op), _value);

            case ValueType::RAW_SQL:
                return fmt::format("{} {} {}", _field->getFullFieldName(), operatorToString(_op), _value);

            case ValueType::SPECIAL:
                return fmt::format("{} {} {}", _field->getFullFieldName(), operatorToString(_op), _value);

            default:
                return "";
            }
        }

        const BaseField*         _field;
        const BaseField*         _comparisonField = nullptr;
        std::string              _value;
        Operator                 _op;
        ValueType                _valueType;
        bool                     _needsGrouping = false;
        LogicalType              _type          = LogicalType::SINGLE;
        std::vector<WhereClause> _subclauses;
    };
} // namespace orm
