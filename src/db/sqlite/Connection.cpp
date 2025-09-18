module;

// Module global fragment
#include <sqlite3.h>

// Module implementation unit
module storm.connection;

import <string>;
import <iostream>;
import <stdexcept>;
import <cstdint>;

// Import required modules
import storm.sql_exceptions;

// Constructor implementation
Connection::Connection(const std::string& db_name) : db(nullptr), transaction_active(false) {
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
        // Auto-rollback any active transaction when connection is closed
        if (transaction_active) {
            try {
                rollback();
            } catch (const std::exception& e) {
                std::cerr << "Error rolling back transaction during connection close: " << e.what() << std::endl;
            }
        }

        sqlite3_close(db);
        std::cout << "Closed database connection." << std::endl;
    }
}

Connection::Connection(Connection&& other) noexcept : db(other.db), transaction_active(other.transaction_active) {
    other.db                 = nullptr;
    other.transaction_active = false;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        // Auto-rollback any active transaction when connection is moved
        if (db && transaction_active) {
            try {
                rollback();
            } catch (const std::exception& e) {
                std::cerr << "Error rolling back transaction during move assignment: " << e.what() << std::endl;
            }
        }

        if (db)
            sqlite3_close(db);
        db                       = other.db;
        transaction_active       = other.transaction_active;
        other.db                 = nullptr;
        other.transaction_active = false;
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

int Connection::get_affected_rows() const {
    if (!is_open()) {
        return -1; // Error value
    }
    return sqlite3_changes(db);
}

void Connection::begin_transaction(TransactionLevel level) {
    if (!is_open()) {
        throw storm::ConnectionNotOpenException();
    }

    if (transaction_active) {
        throw storm::TransactionAlreadyActiveException();
    }

    std::string transaction_type;
    switch (level) {
    case TransactionLevel::DEFERRED:
        transaction_type = "DEFERRED";
        break;
    case TransactionLevel::IMMEDIATE:
        transaction_type = "IMMEDIATE";
        break;
    case TransactionLevel::EXCLUSIVE:
        transaction_type = "EXCLUSIVE";
        break;
    }

    std::string sql       = "BEGIN " + transaction_type + " TRANSACTION";
    char*       error_msg = nullptr;
    int         rc        = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_msg);

    if (rc != SQLITE_OK) {
        std::string error(error_msg);
        sqlite3_free(error_msg);
        throw storm::TransactionStartFailedException(error);
    }

    transaction_active = true;
}

void Connection::commit() {
    if (!is_open()) {
        throw storm::ConnectionNotOpenException();
    }

    if (!transaction_active) {
        throw storm::TransactionNotActiveException();
    }

    char* error_msg = nullptr;
    int   rc        = sqlite3_exec(db, "COMMIT", nullptr, nullptr, &error_msg);

    if (rc != SQLITE_OK) {
        std::string error(error_msg);
        sqlite3_free(error_msg);
        throw storm::TransactionCommitFailedException(error);
    }

    transaction_active = false;
}

void Connection::rollback() {
    if (!is_open()) {
        throw storm::ConnectionNotOpenException();
    }

    if (!transaction_active) {
        throw storm::TransactionNotActiveException();
    }

    char* error_msg = nullptr;
    int   rc        = sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, &error_msg);

    if (rc != SQLITE_OK) {
        std::string error(error_msg);
        sqlite3_free(error_msg);
        throw storm::TransactionRollbackFailedException(error);
    }

    transaction_active = false;
}

bool Connection::in_transaction() const {
    return transaction_active;
}
