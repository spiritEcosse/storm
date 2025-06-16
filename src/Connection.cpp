#include "Connection.h"

Connection::Connection(const std::string& db_name) : db(nullptr) {
    int rc = sqlite3_open(db_name.c_str(), &db);
    if (rc) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        db = nullptr;
    } else {
        std::cout << "Opened database successfully: " << db_name << std::endl;
    }
}

Connection::~Connection() {
    if (db) {
        sqlite3_close(db);
        std::cout << "Closed database connection." << std::endl;
    }
}

Connection::Connection(Connection&& other) noexcept : db(other.db) {
    other.db = nullptr;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (db) sqlite3_close(db);
        db = other.db;
        other.db = nullptr;
    }
    return *this;
}

sqlite3* Connection::get() const {
    return db;
}

bool Connection::is_open() const {
    return db != nullptr;
}

std::int64_t Connection::last_insert_id() const {
    if (!is_open()) {
        return -1; // Error value
    }
    return static_cast<std::int64_t>(sqlite3_last_insert_rowid(db));
}
