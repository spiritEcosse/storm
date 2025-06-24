#include "Statement.h"
#include "Connection.h"
#include "SQLExceptions.h"
#include <stdexcept>
#include <iostream>
#include <regex>

Statement::Statement(std::shared_ptr<Connection> conn, const std::string& sql) : conn(std::move(conn)), stmt(nullptr), sql_(sql) {
    if (!this->conn || !this->conn->get()) {
        throw std::runtime_error("Database connection is not valid.");
    }
    int rc = sqlite3_prepare_v2(this->conn->get(), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string errMsg = sqlite3_errmsg(this->conn->get());
        sqlite3_finalize(stmt); // Finalize stmt if prepare failed but allocated something
        stmt = nullptr; // Ensure stmt is null on failure
        
        // Check if this is a "no such column" error
        std::regex columnRegex("no such column: ([^ ]+)");
        std::smatch matches;
        if (std::regex_search(errMsg, matches, columnRegex) && matches.size() > 1) {
            std::string columnName = matches[1];
            throw orm::InvalidColumnException(columnName, sql);
        }
        
        throw std::runtime_error("Failed to prepare statement: " + sql + " | SQLite Error: " + errMsg);
    }
}

Statement::~Statement() {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

Statement::Statement(Statement&& other) noexcept : conn(other.conn), stmt(other.stmt) {
    other.stmt = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt) sqlite3_finalize(stmt);
        stmt = other.stmt;
        other.stmt = nullptr;
    }
    return *this;
}

void Statement::bind(int idx, int value) {
    sqlite3_bind_int(stmt, idx, value);
}

void Statement::bind(int idx, long long value) {
    sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(value));
}

void Statement::bind(int idx, double value) {
    sqlite3_bind_double(stmt, idx, value);
}

void Statement::bind(int idx, const std::string& value) {
    sqlite3_bind_text(stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

void Statement::bind(int idx, const char* value) {
    sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT);
}

void Statement::bind_null(int idx) {
    sqlite3_bind_null(stmt, idx);
}

bool Statement::execute() {
    if (!stmt) {
        throw std::runtime_error("Cannot execute a null statement. Preparation likely failed.");
    }
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return true; // Successfully executed (e.g., CREATE, INSERT, UPDATE, DELETE)
    }
    if (rc == SQLITE_ROW) {
        // This indicates it's a SELECT statement and there's a row of data.
        // For a simple execute(), this might be treated as success or an unexpected state
        // depending on design. For now, let's consider it a success for stepping.
        // The user should use execute_all() or similar for processing rows.
        return true; 
    }
    // If we reach here, an error occurred.
    std::string errMsg = sqlite3_errmsg(conn->get());
    sqlite3_reset(stmt); // Reset statement on error to allow potential reuse or proper finalization
    throw std::runtime_error("Failed to execute statement: " + errMsg);
}

void Statement::reset() {
    sqlite3_reset(stmt);
}

int Statement::column_int(int idx) const {
    return sqlite3_column_int(stmt, idx);
}

double Statement::column_double(int idx) const {
    return sqlite3_column_double(stmt, idx);
}

std::string Statement::column_text(int idx) const {
    const unsigned char* text = sqlite3_column_text(stmt, idx);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}


std::vector<Row> Statement::execute_all() {
    std::vector<Row> results;
    
    // Get column count
    int column_count = sqlite3_column_count(stmt);

    // Execute and collect all rows
    int step_result;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        results.emplace_back(stmt, column_count);
    }
    
    // Check for errors
    if (step_result != SQLITE_DONE) {
        throw std::runtime_error("Error executing statement: " + std::string(sqlite3_errmsg(conn->get())));
    }
    
    return results;
}

Row::Row(sqlite3_stmt* stmt, int column_count) {
    int_values.reserve(column_count);
    double_values.reserve(column_count);
    text_values.reserve(column_count);
    
    for (int i = 0; i < column_count; ++i) {
        int_values.push_back(sqlite3_column_int(stmt, i));
        double_values.push_back(sqlite3_column_double(stmt, i));
        
        const unsigned char* text = sqlite3_column_text(stmt, i);
        text_values.push_back(text ? reinterpret_cast<const char*>(text) : std::string());
    }
}
