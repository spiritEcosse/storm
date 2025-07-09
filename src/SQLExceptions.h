#pragma once

#include <stdexcept>
#include <string>
#include <regex>

namespace storm {

// Base exception class for all Storm-related exceptions
class StormException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Database connection exceptions
class ConnectionException : public StormException {
public:
    using StormException::StormException;
};

class ConnectionNotOpenException : public ConnectionException {
public:
    ConnectionNotOpenException() : ConnectionException("Database connection is not open") {}
};

// Transaction exceptions
class TransactionException : public StormException {
public:
    using StormException::StormException;
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

class NullStatementException : public SQLQueryException {
public:
    explicit NullStatementException(const std::string& query = "")
        : SQLQueryException("Cannot execute a null statement. Preparation likely failed.", query) {}
};

class StatementPreparationException : public SQLQueryException {
public:
    explicit StatementPreparationException(const std::string& error, const std::string& query = "")
        : SQLQueryException("Failed to prepare statement: " + error, query) {}
};

class StatementExecutionException : public SQLQueryException {
public:
    explicit StatementExecutionException(const std::string& error, const std::string& query = "")
        : SQLQueryException("Error executing statement: " + error, query) {}
};

} // namespace storm
