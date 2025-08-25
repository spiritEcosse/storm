#pragma once

#include "AbstractConnection.h"
#include <memory>
#include <stdexcept>

/**
 * @brief RAII wrapper for database transactions
 *
 * This class automatically begins a transaction when constructed
 * and rolls it back when destroyed unless commit() is called.
 */
class Transaction {
  private:
    AbstractConnection& connection;
    bool                committed  = false;
    bool                rolledBack = false;

  public:
    /**
     * @brief Construct a new Transaction object and begin a transaction
     * @param conn AbstractConnection to use for the transaction
     */
    explicit Transaction(AbstractConnection& conn) : connection(conn) {
        connection.beginTransaction();
    }

    /**
     * @brief Destroy the Transaction object and rollback if not committed
     */
    ~Transaction() {
        if (!committed && !rolledBack) {
            try {
                connection.rollback();
            } catch (...) {
                // Suppress exceptions in destructor
            }
        }
    }

    /**
     * @brief Commit the transaction
     * @throws std::runtime_error if already committed or rolled back
     */
    void commit() {
        if (committed || rolledBack) {
            throw std::runtime_error("Transaction already committed or rolled back");
        }
        connection.commit();
        committed = true;
    }

    /**
     * @brief Rollback the transaction
     * @throws std::runtime_error if already committed or rolled back
     */
    void rollback() {
        if (committed || rolledBack) {
            throw std::runtime_error("Transaction already committed or rolled back");
        }
        connection.rollback();
        rolledBack = true;
    }

    /**
     * @brief Check if the transaction has been committed
     * @return true if committed, false otherwise
     */
    [[nodiscard]] bool isCommitted() const {
        return committed;
    }

    /**
     * @brief Check if the transaction has been rolled back
     * @return true if rolled back, false otherwise
     */
    [[nodiscard]] bool isRolledBack() const {
        return rolledBack;
    }

    // Prevent copying and assignment
    Transaction(const Transaction&)            = delete;
    Transaction& operator=(const Transaction&) = delete;
};
