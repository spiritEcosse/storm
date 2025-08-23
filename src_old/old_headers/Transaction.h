#pragma once

// Legacy header placeholder; use C++23 modules (import storm.transaction).

import storm.connection;
import <memory>;
import <functional>;
import <stdexcept>;
import <type_traits>;
import <utility>;
import <iostream>;

namespace storm {

/**
 * @brief A RAII wrapper for database transactions
 * 
 * This class provides automatic commit/rollback functionality for transactions.
 * When the object goes out of scope, the transaction will be committed if not explicitly
 * committed or rolled back already. If an exception is thrown within the transaction scope,
 * the transaction will be automatically rolled back.
 */
class Transaction {
public:
    /**
     * @brief Construct a new Transaction object and begin a transaction
     * 
     * @param conn The database connection
     * @param level The transaction isolation level
     */
    explicit Transaction(std::shared_ptr<Connection> conn, 
                         Connection::TransactionLevel level = Connection::TransactionLevel::DEFERRED)
        : conn_(std::move(conn)), active_(false) {
        if (!conn_) {
            throw std::runtime_error("Cannot create transaction: null connection");
        }
        
        conn_->begin_transaction(level);
        active_ = true;
    }

    /**
     * @brief Destructor - automatically commits if transaction is still active
     * 
     * If the transaction has not been explicitly committed or rolled back,
     * the destructor will attempt to commit it. If commit fails, it will
     * roll back the transaction.
     */
    ~Transaction() {
        if (active_) {
            try {
                rollback();
            } catch (const std::exception& e) {
                // Cannot throw from destructor, just log the error
                std::cerr << "Error during automatic rollback in destructor: " << e.what() << std::endl;
            }
        }
    }

    // Disable copying
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Allow moving
    Transaction(Transaction&& other) noexcept
        : conn_(std::move(other.conn_)), active_(other.active_) {
        other.active_ = false;
    }

    Transaction& operator=(Transaction&& other) noexcept {
        if (this != &other) {
            // Rollback current transaction if active
            if (active_) {
                try {
                    rollback();
                } catch (const std::exception& e) {
                    std::cerr << "Error rolling back transaction during move: " << e.what() << std::endl;
                }
            }

            conn_ = std::move(other.conn_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    /**
     * @brief Commit the transaction
     * 
     * @throws std::runtime_error if the transaction is not active or commit fails
     */
    void commit() {
        if (!active_) {
            throw std::runtime_error("Cannot commit: transaction is not active");
        }
        
        conn_->commit();
        active_ = false;
    }

    /**
     * @brief Roll back the transaction
     * 
     * @throws std::runtime_error if the transaction is not active or rollback fails
     */
    void rollback() {
        if (!active_) {
            throw std::runtime_error("Cannot rollback: transaction is not active");
        }
        
        conn_->rollback();
        active_ = false;
    }

    /**
     * @brief Check if the transaction is active
     * 
     * @return true if the transaction is active, false otherwise
     */
    [[nodiscard]] bool is_active() const {
        return active_;
    }

private:
    std::shared_ptr<Connection> conn_;
    bool active_;
};

/**
 * @brief Execute a function within a transaction
 * 
 * This function creates a transaction, executes the provided function,
 * and commits the transaction if the function completes successfully.
 * If an exception is thrown, the transaction is rolled back.
 * 
 * @tparam Func The type of the function to execute
 * @param conn The database connection
 * @param func The function to execute within the transaction
 * @param level The transaction isolation level
 * @return The result of the function execution
 */
template<typename Func>
auto with_transaction(std::shared_ptr<Connection> conn, 
                      Func&& func, 
                      Connection::TransactionLevel level = Connection::TransactionLevel::DEFERRED) 
    -> decltype(func()) {
    
    Transaction tx(conn, level);
    
    try {
        if constexpr (std::is_void_v<decltype(func())>) {
            func();
            tx.commit();
        } else {
            auto result = func();
            tx.commit();
            return result;
        }
    } catch (...) {
        // Transaction will be rolled back in destructor
        throw;
    }
}

} // namespace storm
