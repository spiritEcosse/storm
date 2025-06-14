#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include "Connection.h"
#include <atomic>
#include <memory> // Required for std::shared_ptr

class Row {
    public:
        Row(sqlite3_stmt* stmt, int column_count);
        
        int get_int(int idx) const { return int_values[idx]; }
        double get_double(int idx) const { return double_values[idx]; }
        const std::string& get_text(int idx) const { return text_values[idx]; }
        
    private:
        std::vector<int> int_values;
        std::vector<double> double_values;
        std::vector<std::string> text_values;
};

class Statement {
    friend std::ostream& operator<<(std::ostream& os, const Statement& stmt);
public:
    Statement(std::shared_ptr<Connection> conn, const std::string& sql);
    const std::string& get_sql() const { return sql_; }

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

public:
    std::string to_string() const { return sql_; }

private:
    std::shared_ptr<Connection> conn;
    sqlite3_stmt* stmt;
    std::string sql_;

};

inline std::ostream& operator<<(std::ostream& os, const Statement& stmt) {
    os << stmt.get_sql();
    return os;
}
