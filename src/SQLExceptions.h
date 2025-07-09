#pragma once

#include <stdexcept>
#include <string>
#include <regex>

namespace storm {

// Base exception class for all Storm-related exceptions
class StormException : public std::runtime_error {
public:
    explicit StormException(const std::string& message) : std::runtime_error(message) {}
};

// Database connection exceptions
class ConnectionException : public StormException {
public:
    explicit ConnectionException(const std::string& message) : StormException(message) {}
};

class ConnectionNotOpenException : public ConnectionException {
public:
    ConnectionNotOpenException() : ConnectionException("Database connection is not open") {}
};

// Transaction exceptions
class TransactionException : public StormException {
public:
    explicit TransactionException(const std::string& message) : StormException(message) {}
};

class TransactionAlreadyActiveException : public TransactionException {
public:
    TransactionAlreadyActiveException() : TransactionException("Transaction already in progress") {}
};

class TransactionNotActiveException : public TransactionException {
public:
    TransactionNotActiveException() : TransactionException("No active transaction") {}
};

class TransactionStartFailedException : public TransactionException {
public:
    explicit TransactionStartFailedException(const std::string& error) 
        : TransactionException("Failed to begin transaction: " + error) {}
};

class TransactionCommitFailedException : public TransactionException {
public:
    explicit TransactionCommitFailedException(const std::string& error) 
        : TransactionException("Failed to commit transaction: " + error) {}
};

class TransactionRollbackFailedException : public TransactionException {
public:
    explicit TransactionRollbackFailedException(const std::string& error) 
        : TransactionException("Failed to rollback transaction: " + error) {}
};

// SQLite specific exceptions
class SQLiteException : public StormException {
public:
    explicit SQLiteException(const std::string& message, int error_code) 
        : StormException(message), error_code_(error_code) {}
    
    int error_code() const { return error_code_; }

private:
    int error_code_;
};

// SQL Query exceptions
class SQLQueryException : public StormException {
public:
    explicit SQLQueryException(const std::string& message, const std::string& query = "")
        : StormException(message), _query(query) {}

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
