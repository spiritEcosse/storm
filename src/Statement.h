#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include "Connection.h"
#include <atomic>
#include <memory> // Required for std::shared_ptr

// Forward declarations for exception classes
namespace storm {
    class SQLQueryException;
    class InvalidColumnException;

class Row {
    public:
        Row(sqlite3_stmt* stmt, int column_count);
        
        int get_int(int idx) const { return int_values[idx]; }
        double get_double(int idx) const { return double_values[idx]; }
        const std::string& get_text(int idx) const { return text_values[idx]; }
        int get_column_type(int idx) const { return column_types[idx]; }
        const std::string& get_column_name(int idx) const { return column_names[idx]; }
        int get_column_count() const { return static_cast<int>(column_types.size()); }
        
    private:
        std::vector<int> int_values;
        std::vector<double> double_values;
        std::vector<std::string> text_values;
        std::vector<std::string> column_names; // Stores column names
        std::vector<int> column_types; // Stores SQLite column types (SQLITE_INTEGER, SQLITE_FLOAT, etc.)
};

class Statement {
    friend std::ostream& operator<<(std::ostream& os, const Statement& stmt);
public:
    Statement(std::shared_ptr<Connection> conn, const std::string& sql);
    // Returns the raw SQL query string
    std::string sql() const { return sql_; }

    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bind(int idx, int value);
    void bind(int idx, long long value);
    void bind(int idx, double value);
    void bind(int idx, const std::string& value);
    void bind(int idx, const char* value);
    void bind_null(int idx);

    bool execute();
    void reset();
    std::vector<Row> execute_all();

    int column_int(int idx) const;
    double column_double(int idx) const;
    std::string column_text(int idx) const;
    
    // Get the raw SQLite statement
    sqlite3_stmt* get_stmt() const { return stmt; }
    
    // Get the parameter index for a named parameter
    [[nodiscard]] int get_parameter_index(const std::string_view param_name) const {
        return sqlite3_bind_parameter_index(stmt, (":" + std::string(param_name)).c_str());
    }

private:
    std::shared_ptr<Connection> conn;
    sqlite3_stmt* stmt;
    std::string sql_;

};

inline std::ostream& operator<<(std::ostream& os, const Statement& stmt) {
    os << stmt.sql();
    return os;
}
}
