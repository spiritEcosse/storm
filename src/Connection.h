#pragma once
#include <sqlite3.h>
#include <string>
#include <iostream>
#include <cstdint>

class Connection {
public:
    Connection(const std::string& db_name);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    sqlite3* get() const;
    bool is_open() const;
    std::int64_t last_insert_id() const;
private:
    sqlite3* db;

public:
    template<typename N>
    static std::string get_type_name() {
        if constexpr (std::is_same_v<N, std::string>) {
            return "VARCHAR";
        } else if constexpr (std::is_same_v<N, int>) {
            return "INTEGER";
        } else if constexpr (std::is_same_v<N, bool>) {
            return "BOOLEAN";
        } else if constexpr (std::is_same_v<N, float>) {
            return "REAL";
        } else if constexpr (std::is_same_v<N, double>) {
            return "DOUBLE PRECISION";
        } else {
            return "UNKNOWN";
        }
    }
};
