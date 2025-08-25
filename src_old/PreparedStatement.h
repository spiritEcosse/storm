#pragma once

#include <string>
#include <vector>
#include <variant>
#include <memory>
#include "ResultSet.h"

/**
 * @brief Abstract class for prepared SQL statements
 *
 * This class provides an interface for prepared statements
 * with parameter binding and execution capabilities.
 */
class PreparedStatement {
  protected:
    std::string sql;

  public:
    using Parameter = std::variant<std::string, int, double, bool, std::nullptr_t>;

    /**
     * @brief Construct a new Prepared Statement object
     * @param sqlQuery The SQL query with placeholders
     */
    explicit PreparedStatement(std::string sqlQuery) : sql(std::move(sqlQuery)) {}

    /**
     * @brief Destroy the Prepared Statement object
     */
    virtual ~PreparedStatement() = default;

    /**
     * @brief Set a string parameter
     * @param index Parameter index (1-based)
     * @param value String value
     */
    virtual void setString(int index, const std::string& value) = 0;

    /**
     * @brief Set an integer parameter
     * @param index Parameter index (1-based)
     * @param value Integer value
     */
    virtual void setInt(int index, int value) = 0;

    /**
     * @brief Set a double parameter
     * @param index Parameter index (1-based)
     * @param value Double value
     */
    virtual void setDouble(int index, double value) = 0;

    /**
     * @brief Set a boolean parameter
     * @param index Parameter index (1-based)
     * @param value Boolean value
     */
    virtual void setBool(int index, bool value) = 0;

    /**
     * @brief Set a parameter to NULL
     * @param index Parameter index (1-based)
     */
    virtual void setNull(int index) = 0;

    /**
     * @brief Set a parameter of any supported type
     * @param index Parameter index (1-based)
     * @param value Parameter value
     */
    virtual void setParameter(int index, const Parameter& value) = 0;

    /**
     * @brief Execute the prepared statement
     * @return true if execution successful, false otherwise
     */
    virtual bool execute() = 0;

    /**
     * @brief Execute the prepared statement and return results
     * @return ResultSet containing query results
     */
    virtual ResultSet executeQuery() = 0;

    /**
     * @brief Execute the prepared statement and return number of affected rows
     * @return Number of affected rows
     */
    virtual int executeUpdate() = 0;

    /**
     * @brief Clear all parameter bindings
     */
    virtual void clearParameters() = 0;

    /**
     * @brief Get the SQL query string
     * @return SQL query string
     */
    [[nodiscard]] const std::string& getSql() const {
        return sql;
    }
};
