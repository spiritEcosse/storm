module;

export module storm.transaction;

import <memory>;
import <functional>;
import <stdexcept>;
import <iostream>;

import storm.connection;

export namespace storm {

// RAII transaction wrapper
class Transaction {
public:
    explicit Transaction(std::shared_ptr<Connection> conn,
                         Connection::TransactionLevel level = Connection::TransactionLevel::DEFERRED)
        : conn_(std::move(conn)), active_(false) {
        if (!conn_) {
            throw std::runtime_error("Cannot create transaction: null connection");
        }
        conn_->begin_transaction(level);
        active_ = true;
    }

    ~Transaction() {
        if (active_) {
            try {
                // Safety-first: rollback if still active
                rollback();
            } catch (const std::exception& e) {
                std::cerr << "Error during automatic rollback in destructor: " << e.what() << std::endl;
            }
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    Transaction(Transaction&& other) noexcept
        : conn_(std::move(other.conn_)), active_(other.active_) {
        other.active_ = false;
    }

    Transaction& operator=(Transaction&& other) noexcept {
        if (this != &other) {
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

    void commit() {
        if (!active_) {
            throw std::runtime_error("Cannot commit: transaction is not active");
        }
        conn_->commit();
        active_ = false;
    }

    void rollback() {
        if (!active_) {
            throw std::runtime_error("Cannot rollback: transaction is not active");
        }
        conn_->rollback();
        active_ = false;
    }

    [[nodiscard]] bool is_active() const { return active_; }

private:
    std::shared_ptr<Connection> conn_;
    bool active_;
};

// Helper to execute a callable within a transaction scope
template <typename Func>
decltype(auto) with_transaction(std::shared_ptr<Connection> conn,
                                Func&& func,
                                Connection::TransactionLevel level = Connection::TransactionLevel::DEFERRED) {
    Transaction tx(std::move(conn), level);
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
        // rollback happens in destructor
        throw;
    }
}

} // namespace storm
