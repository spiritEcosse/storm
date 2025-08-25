#pragma once

#include "AbstractConnection.h"
#include "ResultSet.h"
#include "PreparedStatement.h"
#include "DBConfig.h"
#include "Logger.h"
#include <libpq-fe.h>
#include <memory>
#include <string>
#include <mutex>

/**
 * @brief PostgreSQL implementation of the Connection interface
 *
 * This class provides a concrete implementation of the Connection interface
 * for PostgreSQL databases using libpq.
 */
class PostgreSQLConnection : public Connection {
  private:
    PGconn*                 conn = nullptr;
    DBConfig                config;
    std::shared_ptr<Logger> logger;
    std::mutex              mutex; // For thread safety
    bool                    connected = false;
    time_t                  lastUsed  = 0;

  public:
    /**
     * @brief Construct a new PostgreSQL Connection
     * @param dbConfig Database configuration
     */
    explicit PostgreSQLConnection(const DBConfig& dbConfig) : config(dbConfig), logger(Logger::getInstance()) {}

    /**
     * @brief Destroy the PostgreSQL Connection
     */
    ~PostgreSQLConnection() override {
        disconnect();
    }

    /**
     * @brief Connect to the PostgreSQL database
     * @return true if connection successful, false otherwise
     */
    bool connect() override {
        std::lock_guard<std::mutex> lock(mutex);

        if (connected) {
            return true;
        }

        try {
            // Create connection string
            std::string connStr = config.connectionString();

            // Connect to database
            conn = PQconnectdb(connStr.c_str());

            // Check connection status
            if (PQstatus(conn) != CONNECTION_OK) {
                logger->error("Connection to database failed: " + std::string(PQerrorMessage(conn)));
                PQfinish(conn);
                conn = nullptr;
                return false;
            }

            connected = true;
            lastUsed  = time(nullptr);
            logger->info("Connected to PostgreSQL database: " + config.database);
            return true;
        } catch (const std::exception& e) {
            logger->error("Exception during database connection: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * @brief Disconnect from the PostgreSQL database
     */
    void disconnect() override {
        std::lock_guard<std::mutex> lock(mutex);

        if (conn) {
            PQfinish(conn);
            conn      = nullptr;
            connected = false;
            logger->info("Disconnected from PostgreSQL database: " + config.database);
        }
    }

    /**
     * @brief Check if connected to the database
     * @return true if connected, false otherwise
     */
    bool isConnected() const override {
        return connected && conn && (PQstatus(conn) == CONNECTION_OK);
    }

    /**
     * @brief Execute a SQL query
     * @param query SQL query to execute
     * @return true if execution successful, false otherwise
     */
    bool execute(const std::string& query) override {
        std::lock_guard<std::mutex> lock(mutex);

        if (!ensureConnected()) {
            return false;
        }

        lastUsed = time(nullptr);

        PGresult* result  = PQexec(conn, query.c_str());
        bool      success = (PQresultStatus(result) == PGRES_COMMAND_OK || PQresultStatus(result) == PGRES_TUPLES_OK);

        if (!success) {
            logger->error("Query execution failed: " + std::string(PQerrorMessage(conn)));
            logger->error("Query was: " + query);
        }

        PQclear(result);
        return success;
    }

    /**
     * @brief Execute a SQL query and return results
     * @param query SQL query to execute
     * @return ResultSet containing query results
     */
    ResultSet executeQuery(const std::string& query) override {
        std::lock_guard<std::mutex> lock(mutex);

        if (!ensureConnected()) {
            throw std::runtime_error("Not connected to database");
        }

        lastUsed = time(nullptr);

        PGresult* result = PQexec(conn, query.c_str());

        if (PQresultStatus(result) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            logger->error("Query execution failed: " + error);
            logger->error("Query was: " + query);
            throw std::runtime_error("Query execution failed: " + error);
        }

        // Create a ResultSet from the PGresult
        ResultSet resultSet;

        // Get column count and names
        int columns = PQnfields(result);
        for (int i = 0; i < columns; i++) {
            resultSet.addColumn(PQfname(result, i));
        }

        // Get rows
        int rows = PQntuples(result);
        for (int i = 0; i < rows; i++) {
            std::vector<std::string> row;
            for (int j = 0; j < columns; j++) {
                if (PQgetisnull(result, i, j)) {
                    row.push_back("");
                } else {
                    row.push_back(PQgetvalue(result, i, j));
                }
            }
            resultSet.addRow(row);
        }

        PQclear(result);
        return resultSet;
    }

    /**
     * @brief Prepare a SQL statement
     * @param query SQL query to prepare
     * @return Shared pointer to PreparedStatement
     */
    std::shared_ptr<PreparedStatement> prepareStatement(const std::string& query) override;

    /**
     * @brief Begin a database transaction
     */
    void beginTransaction() override {
        if (!execute("BEGIN")) {
            throw std::runtime_error("Failed to begin transaction");
        }
    }

    /**
     * @brief Commit the current transaction
     */
    void commit() override {
        if (!execute("COMMIT")) {
            throw std::runtime_error("Failed to commit transaction");
        }
    }

    /**
     * @brief Rollback the current transaction
     */
    void rollback() override {
        if (!execute("ROLLBACK")) {
            throw std::runtime_error("Failed to rollback transaction");
        }
    }

    /**
     * @brief Update last used timestamp
     */
    void updateLastUsed() override {
        lastUsed = time(nullptr);
    }

    /**
     * @brief Get time since last use
     * @return Time in seconds since last use
     */
    time_t getIdleTime() const override {
        return time(nullptr) - lastUsed;
    }

    /**
     * @brief Get the native PostgreSQL connection handle
     * @return PGconn pointer
     */
    PGconn* getNativeConnection() const {
        return conn;
    }

  private:
    /**
     * @brief Ensure connection is active
     * @return true if connected, false otherwise
     */
    bool ensureConnected() {
        if (!isConnected()) {
            return connect();
        }
        return true;
    }
};

/**
 * @brief PostgreSQL implementation of the PreparedStatement interface
 */
class PostgreSQLPreparedStatement : public PreparedStatement {
  private:
    std::shared_ptr<PostgreSQLConnection> connection;
    std::string                           sql;
    std::string                           statementName;
    std::vector<Parameter>                parameters;

  public:
    /**
     * @brief Construct a new PostgreSQL Prepared Statement
     * @param conn PostgreSQL connection
     * @param query SQL query to prepare
     */
    PostgreSQLPreparedStatement(std::shared_ptr<PostgreSQLConnection> conn, const std::string& query)
        : connection(std::move(conn)), sql(query) {
        // Generate a unique statement name
        statementName = "stmt_" + std::to_string(reinterpret_cast<uintptr_t>(this));

        // Prepare the statement
        PGconn*   pgConn = connection->getNativeConnection();
        PGresult* result = PQprepare(pgConn, statementName.c_str(), sql.c_str(), 0, nullptr);

        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(pgConn);
            PQclear(result);
            throw std::runtime_error("Failed to prepare statement: " + error);
        }

        PQclear(result);
    }

    /**
     * @brief Destroy the PostgreSQL Prepared Statement
     */
    ~PostgreSQLPreparedStatement() override {
        // Deallocate the prepared statement
        try {
            std::string deallocSql = "DEALLOCATE " + statementName;
            connection->execute(deallocSql);
        } catch (...) {
            // Ignore errors during cleanup
        }
    }

    /**
     * @brief Set a parameter value
     * @param index Parameter index (1-based)
     * @param value Parameter value
     */
    void setParameter(int index, const Parameter& value) override {
        if (index <= 0) {
            throw std::invalid_argument("Parameter index must be positive");
        }

        // Ensure the parameters vector is large enough
        if (static_cast<size_t>(index) > parameters.size()) {
            parameters.resize(index);
        }

        // Set the parameter value (index is 1-based, vector is 0-based)
        parameters[index - 1] = value;
    }

    /**
     * @brief Execute the prepared statement
     * @return true if execution successful, false otherwise
     */
    bool execute() override {
        // Convert parameters to PostgreSQL format
        std::vector<const char*> paramValues;
        std::vector<std::string> paramStrings;

        for (const auto& param : parameters) {
            std::string paramStr = parameterToString(param);
            paramStrings.push_back(paramStr);
            paramValues.push_back(paramStrings.back().c_str());
        }

        // Execute the prepared statement
        PGconn*   pgConn = connection->getNativeConnection();
        PGresult* result = PQexecPrepared(
                pgConn,
                statementName.c_str(),
                static_cast<int>(paramValues.size()),
                paramValues.data(),
                nullptr, // Parameter lengths (null for text format)
                nullptr, // Parameter formats (null for text format)
                0        // Result format (0 for text)
        );

        bool success = (PQresultStatus(result) == PGRES_COMMAND_OK || PQresultStatus(result) == PGRES_TUPLES_OK);

        if (!success) {
            std::string error = PQerrorMessage(pgConn);
            Logger::getInstance()->error("Prepared statement execution failed: " + error);
        }

        PQclear(result);
        return success;
    }

    /**
     * @brief Execute the prepared statement and return results
     * @return ResultSet containing query results
     */
    ResultSet executeQuery() override {
        // Convert parameters to PostgreSQL format
        std::vector<const char*> paramValues;
        std::vector<std::string> paramStrings;

        for (const auto& param : parameters) {
            std::string paramStr = parameterToString(param);
            paramStrings.push_back(paramStr);
            paramValues.push_back(paramStrings.back().c_str());
        }

        // Execute the prepared statement
        PGconn*   pgConn = connection->getNativeConnection();
        PGresult* result = PQexecPrepared(
                pgConn,
                statementName.c_str(),
                static_cast<int>(paramValues.size()),
                paramValues.data(),
                nullptr, // Parameter lengths (null for text format)
                nullptr, // Parameter formats (null for text format)
                0        // Result format (0 for text)
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(pgConn);
            PQclear(result);
            throw std::runtime_error("Prepared statement execution failed: " + error);
        }

        // Create a ResultSet from the PGresult
        ResultSet resultSet;

        // Get column count and names
        int columns = PQnfields(result);
        for (int i = 0; i < columns; i++) {
            resultSet.addColumn(PQfname(result, i));
        }

        // Get rows
        int rows = PQntuples(result);
        for (int i = 0; i < rows; i++) {
            std::vector<std::string> row;
            for (int j = 0; j < columns; j++) {
                if (PQgetisnull(result, i, j)) {
                    row.push_back("");
                } else {
                    row.push_back(PQgetvalue(result, i, j));
                }
            }
            resultSet.addRow(row);
        }

        PQclear(result);
        return resultSet;
    }

    /**
     * @brief Execute an update statement
     * @return Number of rows affected
     */
    int executeUpdate() override {
        // Convert parameters to PostgreSQL format
        std::vector<const char*> paramValues;
        std::vector<std::string> paramStrings;

        for (const auto& param : parameters) {
            std::string paramStr = parameterToString(param);
            paramStrings.push_back(paramStr);
            paramValues.push_back(paramStrings.back().c_str());
        }

        // Execute the prepared statement
        PGconn*   pgConn = connection->getNativeConnection();
        PGresult* result = PQexecPrepared(
                pgConn,
                statementName.c_str(),
                static_cast<int>(paramValues.size()),
                paramValues.data(),
                nullptr, // Parameter lengths (null for text format)
                nullptr, // Parameter formats (null for text format)
                0        // Result format (0 for text)
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(pgConn);
            PQclear(result);
            throw std::runtime_error("Prepared statement execution failed: " + error);
        }

        // Get the number of affected rows
        int   affectedRows = 0;
        char* rowsStr      = PQcmdTuples(result);
        if (rowsStr && *rowsStr) {
            affectedRows = std::stoi(rowsStr);
        }

        PQclear(result);
        return affectedRows;
    }

  private:
    /**
     * @brief Convert a parameter to string representation
     * @param param Parameter to convert
     * @return String representation of parameter
     */
    std::string parameterToString(const Parameter& param) {
        return std::visit(
                [](const auto& value) -> std::string {
                    using T = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<T, std::string>) {
                        return value;
                    } else if constexpr (std::is_same_v<T, int>) {
                        return std::to_string(value);
                    } else if constexpr (std::is_same_v<T, double>) {
                        return std::to_string(value);
                    } else if constexpr (std::is_same_v<T, bool>) {
                        return value ? "TRUE" : "FALSE";
                    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                        return "NULL";
                    } else {
                        return ""; // Default case
                    }
                },
                param
        );
    }
};

// Implementation of prepareStatement method
inline std::shared_ptr<PreparedStatement> PostgreSQLConnection::prepareStatement(const std::string& query) {
    if (!ensureConnected()) {
        throw std::runtime_error("Not connected to database");
    }

    return std::make_shared<PostgreSQLPreparedStatement>(
            std::dynamic_pointer_cast<PostgreSQLConnection>(shared_from_this()), query
    );
}
