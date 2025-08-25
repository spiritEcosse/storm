#pragma once

#include <string>
#include <cstdint>

/**
 * @brief Structured database error class
 *
 * This class represents a database error with error code,
 * SQL state, and error message.
 */
class DatabaseError {
  private:
    int32_t     errorCode;
    std::string sqlState;
    std::string message;

  public:
    /**
     * @brief Construct a new Database Error object
     * @param code Error code
     * @param state SQL state
     * @param msg Error message
     */
    DatabaseError(int32_t code, std::string state, std::string msg)
        : errorCode(code), sqlState(std::move(state)), message(std::move(msg)) {}

    /**
     * @brief Get the error code
     * @return Error code
     */
    [[nodiscard]] int32_t getErrorCode() const {
        return errorCode;
    }

    /**
     * @brief Get the SQL state
     * @return SQL state
     */
    [[nodiscard]] const std::string& getSqlState() const {
        return sqlState;
    }

    /**
     * @brief Get the error message
     * @return Error message
     */
    [[nodiscard]] const std::string& getMessage() const {
        return message;
    }

    /**
     * @brief Get a formatted error string
     * @return Formatted error string
     */
    [[nodiscard]] std::string toString() const {
        return "Database Error [" + sqlState + "] " + std::to_string(errorCode) + ": " + message;
    }
};
