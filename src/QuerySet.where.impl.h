#pragma once

#include "Where.h"

namespace storm {

    template<typename T>
    template<typename FieldType, typename Value>
    storm::Where QuerySet<T>::create_condition(const FieldType& field_obj, Value&& value, storm::Op op) const {
        switch(op) {
            case storm::Op::EQ: return field_obj == std::forward<Value>(value);
            case storm::Op::NE: return field_obj != std::forward<Value>(value);
            case storm::Op::GT: return field_obj > std::forward<Value>(value);
            case storm::Op::LT: return field_obj < std::forward<Value>(value);
            case storm::Op::GE: return field_obj >= std::forward<Value>(value);
            case storm::Op::LE: return field_obj <= std::forward<Value>(value);
            case storm::Op::LIKE: return field_obj.like(std::forward<Value>(value));
            case storm::Op::IS: return field_obj.is(std::forward<Value>(value));
            default: return field_obj == std::forward<Value>(value);
        }
    }
    
    template<typename T>
    template<typename FieldType, typename Container>
    storm::Where QuerySet<T>::create_in_condition(const FieldType& field_obj, const Container& values) const {
        if (values.empty()) {
            // Return a condition that's always false for empty IN clause
            return storm::Where(std::make_unique<storm::Condition>("1", storm::Op::EQ, 0));
        }
        
        // For now, we'll create OR conditions for each value
        // This is a limitation of the current Storm implementation
        // In a real implementation, you'd want proper IN support
        auto it = values.begin();
        storm::Where result = field_obj == *it;
        ++it;
        
        for (; it != values.end(); ++it) {
            result = result || (field_obj == *it);
        }
        
        return result;
    }

    // WHERE methods for QuerySet
    template<typename T>
    template<auto Field>
    QuerySet<T>& QuerySet<T>::where(typename member_pointer_traits<decltype(Field)>::type value, Op op) {
        using ClassType = typename member_pointer_class<decltype(Field)>::type;
        std::string fieldName = getFieldNameFromMemberPtr<Field>();
        std::string tableName = this->template get_table_name<ClassType>();
        auto formattedField = utils::formatFieldName(tableName, fieldName);
        
        auto field_obj = WhereField(formattedField);
        auto condition = this->create_condition(field_obj, std::move(value), op);
        
        if (this->_whereExpression.has_value()) {
            this->_whereExpression = this->_whereExpression.value() && condition;
        } else {
            this->_whereExpression = condition;
        }
        
        return *this;
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_equals(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::EQ);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_not_equals(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::NE);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_greater_than(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::GT);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_less_than(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::LT);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_greater_or_equals(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::GE);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_less_or_equals(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::LE);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_like(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::LIKE);
    }

    template<typename T>
    template<auto Field, typename Value>
    QuerySet<T>& QuerySet<T>::where_is(Value&& value) {
        return this->template where<Field>(std::forward<Value>(value), Op::IS);
    }

    template<typename T>
    template<auto Field, typename Container>
    QuerySet<T>& QuerySet<T>::where_in(const Container& values) {
        using ClassType = typename member_pointer_class<decltype(Field)>::type;
        std::string fieldName = getFieldNameFromMemberPtr<Field>();
        std::string tableName = this->template get_table_name<ClassType>();
        auto formattedField = utils::formatFieldName(tableName, fieldName);
        
        auto field_obj = WhereField(formattedField);
        auto condition = this->create_in_condition(field_obj, values);
        
        if (this->_whereExpression.has_value()) {
            this->_whereExpression = this->_whereExpression.value() && condition;
        } else {
            this->_whereExpression = condition;
        }
        
        return *this;
    }

    // Direct WHERE methods
    template<typename T>
    QuerySet<T>& QuerySet<T>::where(const storm::Where& condition) {
        if (this->_whereExpression.has_value()) {
            this->_whereExpression = this->_whereExpression.value() && condition;
        } else {
            this->_whereExpression = condition;
        }
        return *this;
    }

    template<typename T>
    QuerySet<T>& QuerySet<T>::where_raw(const std::string& raw_condition, const std::vector<std::any>& parameters) {
        storm::Where condition(std::make_unique<storm::RawCondition>(raw_condition, parameters));
        
        if (this->_whereExpression.has_value()) {
            this->_whereExpression = this->_whereExpression.value() && condition;
        } else {
            this->_whereExpression = condition;
        }
        
        return *this;
    }
    
    template<typename T>
    bool QuerySet<T>::has_where() const {
        return this->_whereExpression.has_value();
    }

} // namespace storm
