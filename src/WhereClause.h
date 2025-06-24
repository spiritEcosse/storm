#pragma once

#include "BaseClass.h"
#include "BaseField.h"
#include "MemberPointerUtils.h"
#include "Reflect.h"

#include <refl.hpp>

#include <string>
#include <optional>
#include <type_traits>
#include <memory>
#include <vector>
#include <sstream>
#include <iostream>
#include <fmt/format.h>

enum class Operator { EQUALS, NOT_EQUALS, GREATER_THAN, LESS_THAN, GREATER_OR_EQUAL, LESS_OR_EQUAL, LIKE, IS };

inline std::string operatorToString(const Operator op) {
    using enum Operator;

    switch(op) {
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
        enum class ValueType { STRING, SPECIAL };
        
        // Default constructor
        WhereClause() : BaseClass(), _fieldName(""), _op(Operator::EQUALS), _valueType(ValueType::STRING), _needsGrouping(false), _type(LogicalType::SINGLE) {}

        // Runtime constructor for member pointers with arithmetic values (excluding bool)
        template<typename ClassType, typename FieldType, typename Value,
        std::enable_if_t<std::is_arithmetic_v<std::decay_t<Value>> && !std::is_same_v<std::decay_t<Value>, bool>, int> = 0>
        WhereClause(FieldType ClassType::* memberPtr, Value value, const Operator op = Operator::EQUALS) :
        BaseClass() {
            std::string tableName = Reflect<ClassType>::get_struct_name();
            std::string fieldName = orm::detail::getFieldNameFromMemberPtr(memberPtr);
            _fieldName = formatFieldName(tableName, fieldName);
            _value = std::to_string(value);
            _op = op;
            _valueType = ValueType::SPECIAL;
        }

        // Runtime constructor for member pointers with non-arithmetic values (including bool)
        template<typename ClassType, typename FieldType, typename Value,
        std::enable_if_t<!std::is_arithmetic_v<std::decay_t<Value>> || std::is_same_v<std::decay_t<Value>, bool>, int> = 0>
        WhereClause(FieldType ClassType::* memberPtr, Value&& value, const Operator op = Operator::EQUALS) :
        BaseClass() {
            std::string tableName = Reflect<ClassType>::get_struct_name();
            std::string fieldName = orm::detail::getFieldNameFromMemberPtr(memberPtr);
            _fieldName = formatFieldName(tableName, fieldName);

            if constexpr (std::is_same_v<std::decay_t<Value>, bool>) {
                _value = boolToString(value);
            } else {
                std::ostringstream oss;
                oss << value;
                _value = oss.str();
            }

            _op = op;
            _valueType = ValueType::STRING;
        }

        WhereClause(LogicalType type, std::vector<WhereClause> subclauses, bool isGrouped = false) :
            BaseClass(), _fieldName(""), _op(), _valueType(), _needsGrouping(isGrouped), _type(type),
            _subclauses(std::move(subclauses)) {}

        // Modified group method that creates a new wrapper clause
        static WhereClause createGroup(WhereClause clause) {
            std::vector<WhereClause> subclauses;
            subclauses.push_back(std::move(clause));
            return WhereClause(LogicalType::SINGLE, std::move(subclauses), true);
        }

        // Modified operator overloads
        friend WhereClause operator&(WhereClause lhs, WhereClause rhs) {
            // If we're already building an AND clause, just append
            if(lhs._type == LogicalType::AND && lhs._fieldName.empty()) {
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
            if(lhs._type == LogicalType::OR && lhs._fieldName.empty()) {
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
            if(_fieldName.empty() && _type == LogicalType::SINGLE && _subclauses.size() == 1 && _needsGrouping) {
                _subclauses[0].serializeRecursive(ss);
                return;
            }

            // For real clauses with multiple subclauses
            bool needParentheses = _needsGrouping || (_type != LogicalType::SINGLE && _subclauses.size() > 1);

            if(needParentheses) {
                ss << "(";
            }

            // If this is a logical operator clause
            if(_fieldName.empty()) {
                if(!_subclauses.empty()) {
                    _subclauses[0].serializeRecursive(ss);

                    for(size_t i = 1; i < _subclauses.size(); ++i) {
                        ss << (_type == LogicalType::AND ? " AND " : " OR ");
                        _subclauses[i].serializeRecursive(ss);
                    }
                }
            } else {
                // This is a leaf clause
                ss << serializeSingleClause();

                // Handle any subclauses
                if(!_subclauses.empty()) {
                    for(const auto& subclause: _subclauses) {
                        ss << (_type == LogicalType::AND ? " AND " : " OR ");
                        subclause.serializeRecursive(ss);
                    }
                }
            }

            if(needParentheses) {
                ss << ")";
            }
        }

        std::string serializeSingleClause() const {
            switch(_valueType) {

                case ValueType::STRING:
                    return _fieldName + " " + operatorToString(_op) + " '" + _value + "'";

                case ValueType::SPECIAL:
                    return _fieldName + " " + operatorToString(_op) + " " + _value;

                default:
                    return "";
            }
        }
        
        std::string formatFieldName(const std::string& tableName, const std::string& fieldName) const {
            return fmt::format(R"("{}"."{}")", tableName, fieldName);
        }
        
        std::string _fieldName;
        std::string _value;
        Operator _op;
        ValueType _valueType;
        bool _needsGrouping = false;
        LogicalType _type = LogicalType::SINGLE;
        std::vector<WhereClause> _subclauses;
    };
}
