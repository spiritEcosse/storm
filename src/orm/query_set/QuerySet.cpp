module;


// Module implementation unit
module storm.query_set;

import <string>;

// Import required modules for implementation
import storm.utils;

namespace storm {

// Implementation for StringFieldAlias methods
std::string StringFieldAlias::getFullFieldName() const {
    return utils::formatFieldName(tableName, fieldName);
}

std::string StringFieldAlias::getFieldName() const {
    return fieldName;
}

std::string StringFieldAlias::getTableName() const {
    return tableName;
}

std::string StringFieldAlias::getAlias() const {
    return alias;
}

bool StringFieldAlias::isStringField() const {
    return true;  // Conservative assumption
}

bool StringFieldAlias::isBoolField() const {
    return false;  // Conservative assumption
}

bool StringFieldAlias::isNumericField() const {
    return false;  // Conservative assumption
}

FieldAliasBase* StringFieldAlias::clone() const {
    return new StringFieldAlias(tableName, fieldName, alias);
}

// Implementation for non-template utility functions
std::string joinTypeToString(const JoinInfo::JoinType type) {
    switch(type) {
        using enum JoinInfo::JoinType;
        case INNER:
            return "INNER";
        case LEFT:
            return "LEFT";
        case RIGHT:
            return "RIGHT";
    }
    return "INNER";
}

std::string addExtraQuotes(const std::string& str) {
    std::string result;
    for(char c: str) {
        if(c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }
    return result;
}

// For template classes like QuerySet<T>, we can provide explicit instantiations
// for common types to reduce compile time and binary size
// Example:
// template class QuerySet<User>;
// template class QuerySet<Product>;

} // namespace storm
