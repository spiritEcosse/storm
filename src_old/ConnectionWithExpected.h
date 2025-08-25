#pragma once

#include <expected>
#include <string>
#include <memory>
#include "AbstractConnection.h"
#include "ResultSet.h"
#include "PreparedStatement.h"
#include "DatabaseError.h"

/**
 * @brief Enhanced connection interface using std::expected for error handling
 *
 * This class wraps a regular AbstractConnection and provides methods that return
 * std::expected instead of throwing exceptions.
 */
class ConnectionWithExpected {
  private:
    std::shared_ptr<AbstractConnection> connection;

  public:
    /**
     * @brief Construct a new Connection With Expected object
     * @param conn Underlying connection to use
     */
    explicit ConnectionWithExpected(std::shared_ptr<AbstractConnection> conn) : connection(std::move(conn)) {}

    /**
     * @brief Connect to the database
     * @return std::expected with true on success or DatabaseError on failure
     */
    std::expected<bool, DatabaseError> connect() {
        try {
            return connection->connect();
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "CONN_ERR", e.what()));
        }
    }

    /**
     * @brief Disconnect from the database
     * @return std::expected with void on success or DatabaseError on failure
     */
    std::expected<void, DatabaseError> disconnect() {
        try {
            connection->disconnect();
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "DISC_ERR", e.what()));
        }
    }

    /**
     * @brief Check if connected to the database
     * @return true if connected, false otherwise
     */
    [[nodiscard]] bool isConnected() const {
        return connection->isConnected();
    }

    /**
     * @brief Execute a SQL query
     * @param query SQL query to execute
     * @return std::expected with true on success or DatabaseError on failure
     */
    std::expected<bool, DatabaseError> execute(const std::string& query) {
        try {
            return connection->execute(query);
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "EXEC_ERR", e.what()));
        }
    }

    /**
     * @brief Execute a SQL query and return results
     * @param query SQL query to execute
     * @return std::expected with ResultSet on success or DatabaseError on failure
     */
    std::expected<ResultSet, DatabaseError> executeQuery(const std::string& query) {
        try {
            return connection->executeQuery(query);
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "QUERY_ERR", e.what()));
        }
    }

    /**
     * @brief Prepare a SQL statement
     * @param query SQL query to prepare
     * @return std::expected with PreparedStatement on success or DatabaseError on failure
     */
    std::expected<std::shared_ptr<PreparedStatement>, DatabaseError> prepareStatement(const std::string& query) {
        try {
            return connection->prepareStatement(query);
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "PREP_ERR", e.what()));
        }
    }

    /**
     * @brief Begin a database transaction
     * @return std::expected with void on success or DatabaseError on failure
     */
    std::expected<void, DatabaseError> beginTransaction() {
        try {
            connection->beginTransaction();
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "TX_BEGIN_ERR", e.what()));
        }
    }

    /**
     * @brief Commit the current transaction
     * @return std::expected with void on success or DatabaseError on failure
     */
    std::expected<void, DatabaseError> commit() {
        try {
            connection->commit();
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "TX_COMMIT_ERR", e.what()));
        }
    }

    /**
     * @brief Rollback the current transaction
     * @return std::expected with void on success or DatabaseError on failure
     */
    std::expected<void, DatabaseError> rollback() {
        try {
            connection->rollback();
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(DatabaseError(-1, "TX_ROLLBACK_ERR", e.what()));
        }
    }

    /**
     * @brief Get the underlying connection
     * @return Underlying connection
     */
    [[nodiscard]] std::shared_ptr<AbstractConnection> getConnection() const {
        return connection;
    }
};
