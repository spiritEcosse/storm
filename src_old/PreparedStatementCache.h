#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include "PreparedStatement.h"
#include "AbstractConnection.h"

/**
 * @brief Cache for prepared statements to improve performance
 *
 * This class caches prepared statements to avoid re-preparing
 * frequently used queries, improving performance.
 */
class PreparedStatementCache {
  private:
    std::shared_ptr<AbstractConnection>                                 connection;
    std::unordered_map<std::string, std::shared_ptr<PreparedStatement>> cache;
    mutable std::mutex cacheMutex; // Make mutex mutable to allow locking in const methods
    size_t             maxSize = 100;

  public:
    /**
     * @brief Construct a new Prepared Statement Cache object
     * @param conn Connection to use for preparing statements
     * @param maxCacheSize Maximum number of statements to cache
     */
    explicit PreparedStatementCache(std::shared_ptr<AbstractConnection> conn, size_t maxCacheSize = 100)
        : connection(std::move(conn)), maxSize(maxCacheSize) {}

    /**
     * @brief Get a prepared statement from the cache or create a new one
     * @param sql SQL query to prepare
     * @return Prepared statement
     */
    std::shared_ptr<PreparedStatement> get(const std::string& sql) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        // Check if statement is already in cache
        auto it = cache.find(sql);
        if (it != cache.end()) {
            return it->second;
        }

        // If cache is full, remove oldest entry (simple LRU implementation)
        if (cache.size() >= maxSize) {
            cache.erase(cache.begin());
        }

        // Prepare new statement and add to cache
        auto stmt  = connection->prepareStatement(sql);
        cache[sql] = stmt;
        return stmt;
    }

    /**
     * @brief Clear the cache
     */
    void clear() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache.clear();
    }

    /**
     * @brief Remove a specific statement from the cache
     * @param sql SQL query to remove
     */
    void remove(const std::string& sql) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache.erase(sql);
    }

    /**
     * @brief Get the current cache size
     * @return Number of statements in the cache
     */
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return cache.size();
    }

    /**
     * @brief Set the maximum cache size
     * @param size New maximum cache size
     */
    void setMaxSize(size_t size) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        maxSize = size;

        // If current size exceeds new max size, trim the cache
        while (cache.size() > maxSize) {
            cache.erase(cache.begin());
        }
    }
};
