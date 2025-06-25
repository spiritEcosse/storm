#pragma once

#include <stdexcept>
#include <string>
#include <regex>

namespace storm {

class SQLQueryException : public std::runtime_error {
public:
    explicit SQLQueryException(const std::string& message, const std::string& query = "")
        : std::runtime_error(message), _query(query) {}

    const std::string& getQuery() const { return _query; }

private:
    std::string _query;
};

class InvalidColumnException : public SQLQueryException {
public:
    explicit InvalidColumnException(const std::string& columnName, const std::string& query = "")
        : SQLQueryException("No such column: " + columnName, query), _columnName(columnName) {}

    const std::string& getColumnName() const { return _columnName; }

private:
    std::string _columnName;
};

} // namespace storm
