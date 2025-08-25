#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <variant>

/**
 * @brief Class representing a database query result set
 *
 * This class provides access to the results of a database query,
 * with methods to access rows and columns by index or name.
 */
class ResultSet {
  public:
    using Value = std::variant<std::string, int, double, bool, std::nullptr_t>;
    using Row   = std::unordered_map<std::string, Value>;

  private:
    std::vector<Row>         rows;
    std::vector<std::string> columnNames;
    size_t                   currentRowIndex = 0;

  public:
    /**
     * @brief Construct an empty ResultSet
     */
    ResultSet() = default;

    /**
     * @brief Construct a ResultSet with column names
     * @param columns Names of the columns in the result set
     */
    explicit ResultSet(std::vector<std::string> columns);

    /**
     * @brief Add a row to the result set
     * @param row Row data as a map of column name to value
     */
    void addRow(const Row& row);

    /**
     * @brief Get the number of rows in the result set
     * @return Number of rows
     */
    [[nodiscard]] size_t size() const {
        return rows.size();
    }

    /**
     * @brief Check if the result set is empty
     * @return true if empty, false otherwise
     */
    [[nodiscard]] bool empty() const {
        return rows.empty();
    }

    /**
     * @brief Get the column names in the result set
     * @return Vector of column names
     */
    [[nodiscard]] const std::vector<std::string>& getColumnNames() const {
        return columnNames;
    }

    /**
     * @brief Get a row by index
     * @param index Index of the row to get
     * @return Row data as a map of column name to value
     * @throws std::out_of_range if index is out of bounds
     */
    [[nodiscard]] const Row& getRow(size_t index) const;

    /**
     * @brief Get a value by column name from the current row
     * @param columnName Name of the column
     * @return Value of the column
     * @throws std::out_of_range if column name not found
     */
    [[nodiscard]] const Value& getValue(const std::string& columnName) const;

    /**
     * @brief Get a value by column index from the current row
     * @param columnIndex Index of the column
     * @return Value of the column
     * @throws std::out_of_range if column index is out of bounds
     */
    [[nodiscard]] const Value& getValue(size_t columnIndex) const;

    /**
     * @brief Move to the next row
     * @return true if moved to a valid row, false if at the end
     */
    bool next();

    /**
     * @brief Reset the cursor to before the first row
     */
    void beforeFirst();

    /**
     * @brief Access operator for rows by index
     * @param index Index of the row to access
     * @return Row data as a map of column name to value
     */
    const Row& operator[](size_t index) const {
        return getRow(index);
    }
};
