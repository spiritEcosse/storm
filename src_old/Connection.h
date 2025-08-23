#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <unordered_map>

// Forward declarations
class ResultSet;
class PreparedStatement;

/**
 * @brief Base abstract class for database connections
 * 
 * This class defines the interface for all database connections
 * and provides basic functionality for connection management.
 */
class AbstractConnection {
protected:
    bool inUse = false;
    std::chrono::steady_clock::time_point lastUsed;

public:
    /**
     * @brief Construct a new AbstractConnection object
     */
    AbstractConnection();

    /**
     * @brief Destroy the AbstractConnection object
     */
    virtual ~AbstractConnection() = default;

    /**
     * @brief Connect to the database
     * @return true if connection successful, false otherwise
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from the database
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected to the database
     * @return true if connected, false otherwise
     */
    [[nodiscard]] virtual bool isConnected() const = 0;

    /**
     * @brief Execute a SQL query
     * @param query SQL query to execute
     * @return true if execution successful, false otherwise
     */
    virtual bool execute(const std::string &query) = 0;
    
    /**
     * @brief Execute a SQL query and return results
     * @param query SQL query to execute
     * @return ResultSet containing query results
     */
    virtual ResultSet executeQuery(const std::string &query) = 0;

    /**
     * @brief Prepare a SQL statement
     * @param query SQL query to prepare
     * @return Prepared statement object
     */
    virtual std::shared_ptr<PreparedStatement> prepareStatement(const std::string &query) = 0;

    /**
     * @brief Begin a database transaction
     */
    virtual void beginTransaction() = 0;

    /**
     * @brief Commit the current transaction
     */
    virtual void commit() = 0;

    /**
     * @brief Rollback the current transaction
     */
    virtual void rollback() = 0;

    /**
     * @brief Mark the connection as in use
     */
    void markAsUsed();

    /**
     * @brief Mark the connection as not in use
     */
    void markAsUnused();

    /**
     * @brief Check if the connection is in use
     * @return true if in use, false otherwise
     */
    [[nodiscard]] bool isInUse() const { return inUse; }
    
    /**
     * @brief Get the time when the connection was last used
     * @return Time point when the connection was last used
     */
    [[nodiscard]] auto getLastUsedTime() const { return lastUsed; }
};
