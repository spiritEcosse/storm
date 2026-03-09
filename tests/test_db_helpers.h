#pragma once

/**
 * @file test_db_helpers.h
 * @brief Backend abstraction helpers for Storm ORM TYPED_TEST infrastructure
 *
 * Provides template helpers keyed on ConnType (SQLite or PostgreSQL) so that
 * all ORM tests can run against both backends from a single binary.
 *
 * PostgreSQL test isolation uses per-process schemas (test_<pid>), giving each
 * CTest process its own namespace. This enables fully parallel execution with
 * zero lock contention. Per-test isolation uses DROP SCHEMA + CREATE SCHEMA in
 * TearDown to reset state. The schema is created once per process and reused
 * (never cleared between tests).
 *
 * PostgreSQL tests auto-skip when STORM_PG_CONNSTR env var is not set.
 *
 * Usage in test files:
 *   #include "test_db_helpers.h"
 *   template <typename ConnType>
 *   class MyTest : public ::testing::Test { ... };
 *   TYPED_TEST_SUITE(MyTest, DatabaseTypes);
 */

#include <cstdlib>
#include <format>
#include <gtest/gtest.h>
#include <libpq-fe.h>
#include <string>
#include <type_traits>

// Forward-declare connection types (actual import in .cpp files via `import storm;`)
// These are used in std::is_same_v checks within if constexpr branches.
namespace storm::db::sqlite {
class Connection;
}
namespace storm::db::postgresql {
class Connection;
}

// Type list for TYPED_TEST_SUITE — runs each test against both backends
using DatabaseTypes = ::testing::Types<storm::db::sqlite::Connection, storm::db::postgresql::Connection>;

namespace storm::test {

namespace detail {
// Tracks the current per-process test schema name.
// Empty means no schema has been created yet for this process.
inline std::string
    current_test_schema; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) // NOSONAR(cpp:S5421)
} // namespace detail

// Check if the backend is available (PG requires STORM_PG_CONNSTR set and server reachable;
// SQLite is always available)
template <typename ConnType> bool backend_available() {
    if constexpr (std::is_same_v<ConnType, storm::db::postgresql::Connection>) {
        const char *connstr = std::getenv("STORM_PG_CONNSTR");
        if (!connstr)
            return false;
        PGconn *conn = PQconnectdb(connstr);
        bool ok = PQstatus(conn) == CONNECTION_OK;
        PQfinish(conn);
        return ok;
    }
    return true;
}

// Get the connection string for the current backend
template <typename ConnType> auto get_connection_string() -> std::string {
    if constexpr (std::is_same_v<ConnType, storm::db::postgresql::Connection>) {
        const char *connstr = std::getenv("STORM_PG_CONNSTR");
        return connstr ? std::string(connstr) : "";
    } else {
        return ":memory:";
    }
}

// Non-template overload for backward compatibility with existing code
inline auto get_connection_string() -> std::string { return ":memory:"; }

// Initialise the per-process PostgreSQL test schema (one-time per process).
// No-op for SQLite. Uses conn->execute for PG admin DDL (CREATE/DROP SCHEMA,
// SET search_path) — there is no ORM equivalent for these admin operations.
template <typename ConnType> auto pg_schema_init(auto &conn) -> void {
    if constexpr (std::is_same_v<ConnType, storm::db::postgresql::Connection>) {
        if (detail::current_test_schema.empty()) {
            detail::current_test_schema = std::format("test_{}", getpid());
            (void)conn->execute(std::format("DROP SCHEMA IF EXISTS {} CASCADE", detail::current_test_schema));
            (void)conn->execute(std::format("CREATE SCHEMA {}", detail::current_test_schema));
        }
        // Always set search_path — each test gets a fresh connection
        (void)conn->execute(std::format("SET search_path TO {}", detail::current_test_schema));
    }
}

// End test isolation. For PG: drops the per-process schema (instant cleanup).
// For SQLite: no-op (:memory: is destroyed with the connection).
template <typename ConnType> void rollback_test_txn(auto &conn) {
    if constexpr (std::is_same_v<ConnType, storm::db::postgresql::Connection>) {
        if (!detail::current_test_schema.empty()) {
            (void)conn->execute(std::format("DROP SCHEMA IF EXISTS {} CASCADE", detail::current_test_schema));
            (void)conn->execute(std::format("CREATE SCHEMA {}", detail::current_test_schema));
            // DON'T clear current_test_schema — schema is recreated and ready for next test.
            // pg_schema_init will skip CREATE (schema exists) and just SET search_path.
        }
    }
    (void)conn;
}

// Check if we're running against PostgreSQL
template <typename ConnType> constexpr bool is_postgresql() {
    return std::is_same_v<ConnType, storm::db::postgresql::Connection>;
}

// Check if we're running against SQLite
template <typename ConnType> constexpr bool is_sqlite() {
    return std::is_same_v<ConnType, storm::db::sqlite::Connection>;
}

} // namespace storm::test
