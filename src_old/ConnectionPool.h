#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <stdexcept>
#include <chrono>
#include <functional>
#include <utility>
#include "PostgresSQLConnection.h"

// Connection Pool Manager
class ConnectionPool {
    DBConfig                                         config;
    std::queue<std::shared_ptr<AbstractConnection>>  availableConnections;
    std::vector<std::shared_ptr<AbstractConnection>> allConnections;
    std::mutex                                       mutex;

    std::shared_ptr<AbstractConnection> createNewConnection() {
        std::shared_ptr<AbstractConnection> conn;

        if (config.driver == "postgresql") {
            conn = std::make_shared<PostgresSQLConnection>(config);
        } else if (config.driver == "mysql") {
            // conn = std::make_shared<MySQLConnection>(config);
            throw std::runtime_error("MySQL connections not yet implemented");
        } else {
            throw std::runtime_error("Unsupported database driver: " + config.driver);
        }

        if (conn && conn->connect()) {
            return conn;
        }
        throw std::runtime_error("Failed to create database connection");
    }

  public:
    explicit ConnectionPool(DBConfig cfg) : config(std::move(cfg)) {
        // Initialize with minimum connections
        for (size_t i = 0; i < config.maxConnections / 2; ++i) {
            auto conn = createNewConnection();
            availableConnections.push(conn);
            allConnections.push_back(conn);
        }
    }

    // RAII wrapper for automatic connection return
    class ConnectionGuard {
        std::shared_ptr<AbstractConnection> conn;
        ConnectionPool&                     pool;

      public:
        ConnectionGuard(std::shared_ptr<AbstractConnection> c, ConnectionPool& p) : conn(std::move(c)), pool(p) {
            if (conn)
                conn->markAsUsed();
        }

        ~ConnectionGuard() {
            if (conn) {
                conn->markAsUnused();
                pool.returnConnection(conn);
            }
        }

        AbstractConnection* operator->() {
            return conn.get();
        }
        const AbstractConnection* operator->() const {
            return conn.get();
        }
    };

    ConnectionGuard getConnection() {
        std::unique_lock<std::mutex> lock(mutex);

        // Clean up idle connections
        cleanIdleConnections();

        if (availableConnections.empty() && allConnections.size() < config.maxConnections) {
            const auto conn = createNewConnection();
            allConnections.push_back(conn);
            availableConnections.push(conn);
        }

        if (availableConnections.empty()) {
            throw std::runtime_error("No available connections");
        }

        const auto conn = availableConnections.front();
        availableConnections.pop();

        return {conn, *this};
    }

    void returnConnection(const std::shared_ptr<AbstractConnection>& conn) {
        std::unique_lock<std::mutex> lock(mutex);
        availableConnections.push(conn);
    }

  private:
    void cleanIdleConnections() {
        const auto now = std::chrono::steady_clock::now();

        // Keep at least minimum connections
        if (allConnections.size() <= config.maxConnections / 2) {
            return;
        }

        auto it = allConnections.begin();
        while (it != allConnections.end()) {
            if (auto conn = *it; !conn->isInUse()) {
                if (auto idleTime = now - conn->getLastUsedTime(); idleTime > config.idleTimeout) {
                    // Remove from available connections queue
                    std::queue<std::shared_ptr<AbstractConnection>> temp;
                    while (!availableConnections.empty()) {
                        auto c = availableConnections.front();
                        availableConnections.pop();
                        if (c != conn) {
                            temp.push(c);
                        }
                    }
                    availableConnections = std::move(temp);

                    // Remove from all connections
                    it = allConnections.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
};
