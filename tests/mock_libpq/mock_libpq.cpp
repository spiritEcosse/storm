/**
 * @file mock_libpq.cpp
 * @brief Mock libpq library implementation for testing PostgreSQL error paths
 */

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,readability-implicit-bool-conversion) // NOSONAR(cpp:S125)
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-owning-memory) // NOSONAR(cpp:S125)
// NOLINTBEGIN(readability-braces-around-statements) // NOSONAR(cpp:S125)
// NOLINTBEGIN(bugprone-easily-swappable-parameters) // mock signatures must match real libpq API // NOSONAR(cpp:S125)

#include "mock_libpq.h"
#include <cstring>

// ============================================================================
// Internal Mock State
// ============================================================================

namespace {

    // Fake handles - store state for opaque PGconn/PGresult pointers
    // Sentinel poison value written into a FakePGconn by PQfinish. It is NOT a
    // real ConnStatusType; reading it back means production code touched the
    // connection after PQfinish (the Issue #351 use-after-free).
    constexpr int CONNECTION_BAD_AFTER_FINISH = 99;

    struct FakePGconn {
        int         status = CONNECTION_OK;
        std::string error_message;
    };

    struct FakePGresult {
        int status  = PGRES_COMMAND_OK;
        int ntuples = 0;
    };

    // Cell key for column data lookups: row * 10000 + col
    auto cell_key(int row, int col) -> std::int64_t {
        return (static_cast<std::int64_t>(row) * 10000) + col;
    }

    // Global mock configuration
    struct PqMockConfig { // NOSONAR(cpp:S1820) - mock config needs all fields for full libpq API coverage
        // Connection
        bool        connectdb_null = false;
        int         conn_status    = CONNECTION_OK;
        std::string conn_error_message;

        // Prepare
        bool prepare_null          = false;
        int  prepare_result_status = PGRES_COMMAND_OK;
        int  prepare_fail_on_call  = -1;

        // ExecPrepared
        int exec_prepared_result_status = PGRES_COMMAND_OK;
        int exec_prepared_ntuples       = 0;

        // Exec
        bool exec_null          = false;
        int  exec_result_status = PGRES_COMMAND_OK;
        int  exec_fail_on_call  = -1;

        // Column data
        std::unordered_map<std::int64_t, std::string> column_values;
        std::unordered_map<std::int64_t, bool>        column_nulls;
        std::unordered_map<std::int64_t, int>         column_lengths;

        // Call counters
        int connectdb_calls     = 0;
        int prepare_calls       = 0;
        int exec_calls          = 0;
        int exec_prepared_calls = 0;

        auto reset() -> void {
            connectdb_null     = false;
            conn_status        = CONNECTION_OK;
            conn_error_message = "";

            prepare_null          = false;
            prepare_result_status = PGRES_COMMAND_OK;
            prepare_fail_on_call  = -1;

            exec_prepared_result_status = PGRES_COMMAND_OK;
            exec_prepared_ntuples       = 0;

            exec_null          = false;
            exec_result_status = PGRES_COMMAND_OK;
            exec_fail_on_call  = -1;

            column_values.clear();
            column_nulls.clear();
            column_lengths.clear();

            connectdb_calls     = 0;
            prepare_calls       = 0;
            exec_calls          = 0;
            exec_prepared_calls = 0;
        }
    };

    // Thread-local mock config for thread-safe testing
    thread_local PqMockConfig g_mock_config; // NOSONAR(cpp:S5421) - mutable mock state, cannot be const

    // Simple memory management for fake handles
    thread_local std::vector<std::unique_ptr<FakePGconn>>   g_fake_conns;   // NOSONAR(cpp:S5421) - mutable mock state
    thread_local std::vector<std::unique_ptr<FakePGresult>> g_fake_results; // NOSONAR(cpp:S5421) - mutable mock state

    // Default empty string for PQgetvalue when no column data is configured
    thread_local std::string g_empty_string; // NOSONAR(cpp:S5421) - mutable mock state

} // anonymous namespace

// ============================================================================
// Mock Configuration API Implementation
// ============================================================================

namespace storm::test {

    MockPqConfig MockPqConfig::instance_;

    auto MockPqConfig::reset() -> void {
        g_mock_config.reset();
    }

    // Connection configuration
    auto MockPqConfig::connectdb_returns_null() -> MockPqConfig& {
        g_mock_config.connectdb_null = true;
        return instance_;
    }

    auto MockPqConfig::status_returns(int status) -> MockPqConfig& {
        g_mock_config.conn_status = status;
        return instance_;
    }

    auto MockPqConfig::error_message(std::string_view msg) -> MockPqConfig& {
        g_mock_config.conn_error_message = msg;
        return instance_;
    }

    // Prepare configuration
    auto MockPqConfig::prepare_returns_null() -> MockPqConfig& {
        g_mock_config.prepare_null = true;
        return instance_;
    }

    auto MockPqConfig::prepare_status(int status) -> MockPqConfig& {
        g_mock_config.prepare_result_status = status;
        return instance_;
    }

    auto MockPqConfig::prepare_fails_on_call(int call_number) -> MockPqConfig& {
        g_mock_config.prepare_fail_on_call = call_number;
        return instance_;
    }

    // ExecPrepared configuration
    auto MockPqConfig::exec_prepared_status(int status) -> MockPqConfig& {
        g_mock_config.exec_prepared_result_status = status;
        return instance_;
    }

    auto MockPqConfig::exec_prepared_ntuples(int ntuples) -> MockPqConfig& {
        g_mock_config.exec_prepared_ntuples = ntuples;
        return instance_;
    }

    // Exec configuration
    auto MockPqConfig::exec_returns_null() -> MockPqConfig& {
        g_mock_config.exec_null = true;
        return instance_;
    }

    auto MockPqConfig::exec_status(int status) -> MockPqConfig& {
        g_mock_config.exec_result_status = status;
        return instance_;
    }

    auto MockPqConfig::exec_fails_on_call(int call_number) -> MockPqConfig& {
        g_mock_config.exec_fail_on_call = call_number;
        return instance_;
    }

    // Column data configuration
    auto MockPqConfig::set_column_value(int row, int col, std::string value) -> MockPqConfig& {
        g_mock_config.column_values[cell_key(row, col)] = std::move(value);
        return instance_;
    }

    auto MockPqConfig::set_column_null(int row, int col) -> MockPqConfig& {
        g_mock_config.column_nulls[cell_key(row, col)] = true;
        return instance_;
    }

    auto MockPqConfig::set_column_length(int row, int col, int length) -> MockPqConfig& {
        g_mock_config.column_lengths[cell_key(row, col)] = length;
        return instance_;
    }

    // Call count tracking
    auto MockPqConfig::get_connectdb_call_count() -> int {
        return g_mock_config.connectdb_calls;
    }

    auto MockPqConfig::get_prepare_call_count() -> int {
        return g_mock_config.prepare_calls;
    }

    auto MockPqConfig::get_exec_call_count() -> int {
        return g_mock_config.exec_calls;
    }

    auto MockPqConfig::get_exec_prepared_call_count() -> int {
        return g_mock_config.exec_prepared_calls;
    }

} // namespace storm::test

// ============================================================================
// libpq API Mock Implementations
// ============================================================================

extern "C" {

auto PQconnectdb(const char* conninfo) -> PGconn* {
    (void)conninfo;
    g_mock_config.connectdb_calls++;

    if (g_mock_config.connectdb_null)
        return nullptr;

    auto fake_conn           = std::make_unique<FakePGconn>();
    fake_conn->status        = g_mock_config.conn_status;
    fake_conn->error_message = g_mock_config.conn_error_message;
    auto* result             = reinterpret_cast<PGconn*>(fake_conn.get()); // NOSONAR(cpp:S3630)
    g_fake_conns.push_back(std::move(fake_conn));
    return result;
}

auto PQstatus(const PGconn* conn) -> ConnStatusType {
    if (const auto* fake = reinterpret_cast<const FakePGconn*>(conn); fake) // NOSONAR(cpp:S3630)
        return fake->status;
    return CONNECTION_BAD;
}

auto PQfinish(const PGconn* conn) -> void {
    // Memory stays owned by g_fake_conns, but poison the status so any
    // PQstatus(conn) read AFTER PQfinish (a use-after-free in real libpq,
    // Issue #351) is observable: it no longer returns the pre-finish value.
    // The underlying FakePGconn is genuinely mutable (owned by g_fake_conns).
    auto* mutable_conn = const_cast<PGconn*>(conn); // NOSONAR(cpp:S3630) NOLINT(cppcoreguidelines-pro-type-const-cast)
    if (auto* fake = reinterpret_cast<FakePGconn*>(mutable_conn); fake)
        fake->status = CONNECTION_BAD_AFTER_FINISH;
}

auto PQerrorMessage(const PGconn* conn) -> char* {
    if (const auto* fake = reinterpret_cast<const FakePGconn*>(conn); fake) // NOSONAR(cpp:S3630)
        return const_cast<char*>(fake->error_message.c_str()); // NOSONAR NOLINT(cppcoreguidelines-pro-type-const-cast)
    return const_cast<char*>("");                              // NOSONAR NOLINT(cppcoreguidelines-pro-type-const-cast)
}

auto PQprepare(const PGconn* conn, const char* stmtName, const char* query, int nParams, const Oid* paramTypes)
        -> PGresult* {
    (void)conn;
    (void)stmtName;
    (void)query;
    (void)nParams;
    (void)paramTypes;
    g_mock_config.prepare_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.prepare_fail_on_call == g_mock_config.prepare_calls) {
        // Return null to simulate allocation failure
        return nullptr;
    }

    if (g_mock_config.prepare_null)
        return nullptr;

    auto fake_result    = std::make_unique<FakePGresult>();
    fake_result->status = g_mock_config.prepare_result_status;
    auto* result        = reinterpret_cast<PGresult*>(fake_result.get()); // NOSONAR(cpp:S3630)
    g_fake_results.push_back(std::move(fake_result));
    return result;
}

auto PQexecPrepared(
        const PGconn*      conn,
        const char*        stmtName,
        int                nParams,
        const char* const* paramValues,
        const int*         paramLengths,
        const int*         paramFormats,
        int                resultFormat
) -> PGresult* {
    (void)conn;
    (void)stmtName;
    (void)nParams;
    (void)paramValues;
    (void)paramLengths;
    (void)paramFormats;
    (void)resultFormat;
    g_mock_config.exec_prepared_calls++;

    auto fake_result     = std::make_unique<FakePGresult>();
    fake_result->status  = g_mock_config.exec_prepared_result_status;
    fake_result->ntuples = g_mock_config.exec_prepared_ntuples;
    auto* result         = reinterpret_cast<PGresult*>(fake_result.get()); // NOSONAR(cpp:S3630)
    g_fake_results.push_back(std::move(fake_result));
    return result;
}

auto PQexec(const PGconn* conn, const char* query) -> PGresult* {
    (void)conn;
    (void)query;
    g_mock_config.exec_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.exec_fail_on_call == g_mock_config.exec_calls) {
        if (g_mock_config.exec_null)
            return nullptr;
        auto fake_result    = std::make_unique<FakePGresult>();
        fake_result->status = PGRES_FATAL_ERROR;
        auto* result        = reinterpret_cast<PGresult*>(fake_result.get()); // NOSONAR(cpp:S3630)
        g_fake_results.push_back(std::move(fake_result));
        return result;
    }

    if (g_mock_config.exec_null)
        return nullptr;

    auto fake_result    = std::make_unique<FakePGresult>();
    fake_result->status = g_mock_config.exec_result_status;
    auto* result        = reinterpret_cast<PGresult*>(fake_result.get()); // NOSONAR(cpp:S3630)
    g_fake_results.push_back(std::move(fake_result));
    return result;
}

auto PQresultStatus(const PGresult* res) -> ExecStatusType {
    if (const auto* fake = reinterpret_cast<const FakePGresult*>(res); fake) // NOSONAR(cpp:S3630)
        return fake->status;
    return PGRES_FATAL_ERROR;
}

auto PQclear(const PGresult* res) -> void {
    (void)res;
    // Memory managed by g_fake_results vector
}

auto PQntuples(const PGresult* res) -> int {
    if (const auto* fake = reinterpret_cast<const FakePGresult*>(res); fake) // NOSONAR(cpp:S3630)
        return fake->ntuples;
    return 0;
}

auto PQgetvalue(const PGresult* res, int tup_num, int field_num) -> char* {
    (void)res;
    const auto key = cell_key(tup_num, field_num);
    if (auto it = g_mock_config.column_values.find(key); it != g_mock_config.column_values.end())
        return const_cast<char*>(it->second.c_str()); // NOSONAR NOLINT(cppcoreguidelines-pro-type-const-cast)
    g_empty_string = "";
    return const_cast<char*>(g_empty_string.c_str()); // NOSONAR NOLINT(cppcoreguidelines-pro-type-const-cast)
}

auto PQgetlength(const PGresult* res, int tup_num, int field_num) -> int {
    (void)res;
    const auto key = cell_key(tup_num, field_num);

    // Check explicit length override first
    if (auto len_it = g_mock_config.column_lengths.find(key); len_it != g_mock_config.column_lengths.end())
        return len_it->second;

    // Fall back to actual value length
    if (auto val_it = g_mock_config.column_values.find(key); val_it != g_mock_config.column_values.end())
        return static_cast<int>(val_it->second.size());

    return 0;
}

auto PQgetisnull(const PGresult* res, int tup_num, int field_num) -> int {
    (void)res;
    const auto key = cell_key(tup_num, field_num);
    if (auto it = g_mock_config.column_nulls.find(key); it != g_mock_config.column_nulls.end())
        return it->second ? 1 : 0;
    return 0; // Not null by default
}

} // extern "C"

// NOLINTEND(bugprone-easily-swappable-parameters) // NOSONAR(cpp:S125)
// NOLINTEND(readability-braces-around-statements) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-owning-memory) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,readability-implicit-bool-conversion) // NOSONAR(cpp:S125)
