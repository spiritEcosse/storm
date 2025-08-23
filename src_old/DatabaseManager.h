#pragma once

#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <optional>
#include "ConnectionPool.h"
#include "QueryBuilderAdapter.h"
#include "PreparedStatementCache.h"
#include "Logger.h"
#include "Transaction.h"
#include "AsyncConnection.h"
#include "ConnectionWithExpected.h"

/**
 * @brief Central manager for database connections and operations
 * 
 * This class manages connection pools, provides access to database connections,
 * and integrates the query building system with database operations.
 */
class DatabaseManager {
private:
    std::unordered_map<std::string, std::unique_ptr<ConnectionPool>> pools;
    std::unordered_map<std::string, std::unique_ptr<PreparedStatementCache>> statementCaches;
    std::shared_ptr<Logger> logger;
    
public:
    /**
     * @brief Construct a new Database Manager
     */
    DatabaseManager() : logger(Logger::getInstance()) {
        // Configure logger
        logger->setLevel(Logger::Level::INFO);
        logger->setLogFile("database.log");
    }
    
    /**
     * @brief Add a connection pool
     * @param name Pool name
     * @param config Database configuration
     */
    void addConnectionPool(const std::string& name, const DBConfig& config) {
        pools.emplace(name, std::make_unique<ConnectionPool>(config));
        logger->info("Added connection pool: " + name);
    }

    /**
     * @brief Get a connection from a pool
     * @param poolName Pool name
     * @return Connection guard with RAII connection management
     */
    ConnectionPool::ConnectionGuard getConnection(const std::string& poolName) {
        const auto it = pools.find(poolName);
        if (it == pools.end()) {
            logger->error("Connection pool not found: " + poolName);
            throw std::runtime_error("Connection pool not found: " + poolName);
        }
        return it->second->getConnection();
    }
    
    /**
     * @brief Get a prepared statement cache for a connection pool
     * @param poolName Pool name
     * @return Prepared statement cache
     */
    PreparedStatementCache& getStatementCache(const std::string& poolName) {
        // Check if cache exists
        auto it = statementCaches.find(poolName);
        if (it != statementCaches.end()) {
            return *it->second;
        }
        
        // Create new cache
        auto connGuard = getConnection(poolName);
        auto cache = std::make_unique<PreparedStatementCache>(connGuard.operator->());
        auto& result = *cache;
        statementCaches.emplace(poolName, std::move(cache));
        return result;
    }
    
    /**
     * @brief Execute a QuerySet and return results
     * @tparam T Model type for the QuerySet
     * @param querySet QuerySet to execute
     * @param poolName Connection pool to use
     * @return ResultSet containing query results
     */
    template<typename T>
    ResultSet execute(const orm::BaseQuerySet<T>& querySet, const std::string& poolName) {
        auto connGuard = getConnection(poolName);
        orm::QueryBuilderAdapter adapter(connGuard.operator->());
        return adapter.execute(querySet);
    }
    
    /**
     * @brief Execute a WhereClause and return results
     * @param whereClause WhereClause to execute
     * @param tableName Table to query
     * @param poolName Connection pool to use
     * @return ResultSet containing query results
     */
    ResultSet execute(const orm::WhereClause& whereClause, 
                      const std::string& tableName,
                      const std::string& poolName) {
        auto connGuard = getConnection(poolName);
        orm::QueryBuilderAdapter adapter(connGuard.operator->());
        
        auto builder = adapter.createFromWhereClause(whereClause);
        builder.from(tableName);
        return builder.execute();
    }
    
    /**
     * @brief Begin a transaction
     * @param poolName Connection pool to use
     * @return Transaction object with RAII transaction management
     */
    std::unique_ptr<Transaction> beginTransaction(const std::string& poolName) {
        auto connGuard = getConnection(poolName);
        auto transaction = std::make_unique<Transaction>(connGuard.operator->());
        return transaction;
    }
    
    /**
     * @brief Get an asynchronous connection wrapper
     * @param poolName Connection pool to use
     * @return AsyncConnection wrapper
     */
    AsyncConnection getAsyncConnection(const std::string& poolName) {
        auto connGuard = getConnection(poolName);
        return AsyncConnection(connGuard.operator->());
    }
    
    /**
     * @brief Get a connection with std::expected error handling
     * @param poolName Connection pool to use
     * @return ConnectionWithExpected wrapper
     */
    ConnectionWithExpected getConnectionWithExpected(const std::string& poolName) {
        auto connGuard = getConnection(poolName);
        return ConnectionWithExpected(connGuard.operator->());
    }
    
    /**
     * @brief Set the logger level
     * @param level Log level
     */
    void setLogLevel(Logger::Level level) {
        logger->setLevel(level);
    }
};
