#pragma once

#include "Where.h"

namespace storm {

    // Forward declarations
    template<typename T> class QuerySet;
    template<typename T> class BaseQuerySet;

    // Define WHERE related functionality here that will be included in QuerySet.h
    // This approach creates better separation while still making template definitions
    // available at compile-time where they're needed.

    // WHERE condition creation methods
    template<typename T>
    template<typename FieldType, typename Value>
    storm::Where BaseQuerySet<T>::create_condition(const FieldType& field_obj, Value&& value, storm::Op op) const {
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
    storm::Where BaseQuerySet<T>::create_in_condition(const FieldType& field_obj, const Container& values) const {
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

    // QuerySet WHERE methods implementations
    template<typename T>
    QuerySet<T>& QuerySet<T>::where(const storm::Where& where_clause) {
        if (this->_whereExpression) {
            // Combine with existing WHERE using AND
            this->_whereExpression = *this->_whereExpression && where_clause;
        } else {
            this->_whereExpression = where_clause;
        }
        return *this;
    }

    template<typename T>
    QuerySet<T>& QuerySet<T>::where(storm::Where&& where_clause) {
        if (this->_whereExpression) {
            this->_whereExpression = *this->_whereExpression && where_clause;
        } else {
            this->_whereExpression = std::move(where_clause);
        }
        return *this;
    }

    template<typename T>
    template<typename ClassType, typename FieldType, typename Value>
    QuerySet<T>& QuerySet<T>::where(FieldType ClassType::* memberPtr, Value&& value, storm::Op op) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        storm::Where condition = this->create_condition(field_obj, std::forward<Value>(value), op);
        return where(std::move(condition));
    }

    template<typename T>
    template<typename ClassType, typename FieldType, typename Container>
    QuerySet<T>& QuerySet<T>::where_in(FieldType ClassType::* memberPtr, const Container& values) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        // For IN operator, we need to create a special condition
        storm::Where condition = this->create_in_condition(field_obj, values);
        return where(std::move(condition));
    }

    template<typename T>
    template<typename ClassType, typename FieldType, typename T1, typename T2>
    QuerySet<T>& QuerySet<T>::where_between(FieldType ClassType::* memberPtr, T1&& value1, T2&& value2) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        storm::Where condition = field_obj.between(std::forward<T1>(value1), std::forward<T2>(value2));
        return where(std::move(condition));
    }

    template<typename T>
    template<typename ClassType, typename FieldType>
    QuerySet<T>& QuerySet<T>::where_null(FieldType ClassType::* memberPtr) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        storm::Where condition = field_obj.is_null();
        return where(std::move(condition));
    }

    template<typename T>
    template<typename ClassType, typename FieldType>
    QuerySet<T>& QuerySet<T>::where_not_null(FieldType ClassType::* memberPtr) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        storm::Where condition = field_obj != std::nullopt;
        return where(std::move(condition));
    }

    template<typename T>
    template<typename ClassType, typename FieldType, typename Value>
    QuerySet<T>& QuerySet<T>::where_like(FieldType ClassType::* memberPtr, Value&& pattern) {
        auto field_obj = storm::field(memberPtr, this->get_field_name<ClassType>(memberPtr));
        storm::Where condition = field_obj.like(std::forward<Value>(pattern));
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr, typename Value>
    QuerySet<T>& QuerySet<T>::where(Value&& value, storm::Op op) {
        // Simpler approach - directly extract class type from member pointer
        using MemberPtrType = decltype(MemberPtr);
        using ClassType = typename member_pointer_traits<MemberPtrType>::class_type;
        
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = this->create_condition(field_obj, std::forward<Value>(value), op);
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr, typename Container>
    QuerySet<T>& QuerySet<T>::where_in(const Container& values) {
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = this->create_in_condition(field_obj, values);
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr, typename T1, typename T2>
    QuerySet<T>& QuerySet<T>::where_between(T1&& value1, T2&& value2) {
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = field_obj.between(std::forward<T1>(value1), std::forward<T2>(value2));
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr>
    QuerySet<T>& QuerySet<T>::where_null() {
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = field_obj.is_null();
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr>
    QuerySet<T>& QuerySet<T>::where_not_null() {
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = field_obj != std::nullopt;
        return where(std::move(condition));
    }

    template<typename T>
    template<auto MemberPtr, typename Value>
    QuerySet<T>& QuerySet<T>::where_like(Value&& pattern) {
        using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
        auto field_obj = storm::field(MemberPtr, this->get_field_name<ClassType>(MemberPtr));
        storm::Where condition = field_obj.like(std::forward<Value>(pattern));
        return where(std::move(condition));
    }

    template<typename T>
    QuerySet<T>& QuerySet<T>::where_and(const storm::Where& condition) {
        return where(condition); // Uses existing AND logic
    }

    template<typename T>
    QuerySet<T>& QuerySet<T>::where_or(const storm::Where& condition) {
        if (this->_whereExpression) {
            this->_whereExpression = *this->_whereExpression || condition;
        } else {
            this->_whereExpression = condition;
        }
        return *this;
    }

    template<typename T>
    QuerySet<T>& QuerySet<T>::where_raw(const std::string& raw_condition, const std::vector<std::any>& parameters) {
        storm::Where condition(std::make_unique<storm::RawCondition>(raw_condition, parameters));
        return where(std::move(condition));
    }
    
    template<typename T>
    bool QuerySet<T>::has_where() const {
        return this->_whereExpression.has_value();
    }

} // namespace storm
