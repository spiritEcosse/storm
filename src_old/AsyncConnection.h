#pragma once

#include <coroutine>
#include <future>
#include <memory>
#include <exception>
#include "AbstractConnection.h"
#include "ResultSet.h"

/**
 * @brief Awaitable wrapper for database operations
 *
 * This template class makes any operation awaitable in a coroutine context.
 */
template <typename T> class DatabaseAwaitable {
  private:
    std::function<T()> operation;
    T                  result;

  public:
    explicit DatabaseAwaitable(std::function<T()> op) : operation(std::move(op)) {}

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        // Launch the operation on a separate thread
        std::thread([this, handle]() {
            try {
                result = operation();
                handle.resume();
            } catch (...) {
                // Store exception to be rethrown in await_resume
                std::terminate(); // In a real implementation, store the exception
            }
        }).detach();
    }

    T await_resume() {
        return result;
    }
};

// Specialization for void return type
template <> class DatabaseAwaitable<void> {
  private:
    std::function<void()> operation;

  public:
    explicit DatabaseAwaitable(std::function<void()> op) : operation(std::move(op)) {}

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([this, handle]() {
            try {
                operation();
                handle.resume();
            } catch (...) {
                std::terminate(); // In a real implementation, store the exception
            }
        }).detach();
    }

    void await_resume() {}
};

/**
 * @brief Task class for representing asynchronous operations
 *
 * This class implements the coroutine promise type and return object
 * for asynchronous database operations.
 */
template <typename T> class Task {
  public:
    // The promise type for the coroutine
    struct promise_type {
        std::promise<T>    promise;
        std::exception_ptr exception = nullptr;

        Task<T> get_return_object() {
            return Task<T>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() {
            return {};
        }

        std::suspend_always final_suspend() noexcept {
            return {};
        }

        void unhandled_exception() {
            exception = std::current_exception();
            promise.set_exception(exception);
        }

        void return_value(T value) {
            promise.set_value(std::move(value));
        }
    };

  private:
    std::coroutine_handle<promise_type> handle;

  public:
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Task() {
        if (handle) {
            handle.destroy();
        }
    }

    // Get the future for the task result
    [[nodiscard]] std::future<T> getFuture() const {
        return handle.promise().promise.get_future();
    }

    // Non-copyable
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    // Movable
    Task(Task&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle       = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
};

// Specialization for void return type
template <> class Task<void> {
  public:
    // The promise type for the coroutine
    struct promise_type {
        std::promise<void> promise;
        std::exception_ptr exception = nullptr;

        Task<void> get_return_object() {
            return Task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() {
            return {};
        }

        std::suspend_always final_suspend() noexcept {
            return {};
        }

        void unhandled_exception() {
            exception = std::current_exception();
            promise.set_exception(exception);
        }

        void return_void() {
            promise.set_value();
        }
    };

  private:
    std::coroutine_handle<promise_type> handle;

  public:
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Task() {
        if (handle) {
            handle.destroy();
        }
    }

    // Get the future for the task result
    [[nodiscard]] std::future<void> getFuture() const {
        return handle.promise().promise.get_future();
    }

    // Non-copyable
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    // Movable
    Task(Task&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle       = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
};

/**
 * @brief Asynchronous database connection wrapper
 *
 * This class wraps a regular Connection and provides truly asynchronous
 * methods using C++20 coroutines with direct co_await support.
 */
class AsyncConnection {
  private:
    std::shared_ptr<AbstractConnection> connection;

  public:
    /**
     * @brief Construct a new Async Connection object
     * @param conn Underlying connection to use
     */
    explicit AsyncConnection(std::shared_ptr<AbstractConnection> conn) : connection(std::move(conn)) {}

    /**
     * @brief Connect to the database asynchronously
     * @return Task representing the asynchronous operation
     */
    Task<bool> connectAsync() {
        bool result = co_await DatabaseAwaitable<bool>([this]() { return connection->connect(); });

        co_return result;
    }

    /**
     * @brief Execute a query asynchronously
     * @param query SQL query to execute
     * @return Task representing the asynchronous operation
     */
    Task<bool> executeAsync(const std::string& query) {
        bool result = co_await DatabaseAwaitable<bool>([this, query]() { return connection->execute(query); });

        co_return result;
    }

    /**
     * @brief Execute a query and return results asynchronously
     * @param query SQL query to execute
     * @return Task representing the asynchronous operation
     */
    Task<ResultSet> executeQueryAsync(const std::string& query) {
        ResultSet result =
                co_await DatabaseAwaitable<ResultSet>([this, query]() { return connection->executeQuery(query); });

        co_return result;
    }

    /**
     * @brief Begin a transaction asynchronously
     * @return Task representing the asynchronous operation
     */
    Task<void> beginTransactionAsync() {
        co_await DatabaseAwaitable<void>([this]() { connection->beginTransaction(); });

        co_return;
    }

    /**
     * @brief Commit a transaction asynchronously
     * @return Task representing the asynchronous operation
     */
    Task<void> commitAsync() {
        co_await DatabaseAwaitable<void>([this]() { connection->commit(); });

        co_return;
    }

    /**
     * @brief Rollback a transaction asynchronously
     * @return Task representing the asynchronous operation
     */
    Task<void> rollbackAsync() {
        co_await DatabaseAwaitable<void>([this]() { connection->rollback(); });

        co_return;
    }

    /**
     * @brief Get the underlying connection
     * @return Underlying connection
     */
    [[nodiscard]] std::shared_ptr<AbstractConnection> getConnection() const {
        return connection;
    }
};
