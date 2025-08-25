#pragma once

#include "AbstractConnection.h"

#include <utility>
#include "DBConfig.h"
#include <libpq-fe.h> // PostgreSQL C API header
#include "PreparedStatement.h"
#include "ResultSet.h"
#include "PostgresSQLPreparedStatement.h"

class PostgresSQLConnection final : public AbstractConnection {
    DBConfig config;
    PGconn*  handle = nullptr; // Use proper PostgreSQL connection type

  public:
    explicit PostgresSQLConnection(DBConfig cfg) : config(std::move(cfg)) {}

    ~PostgresSQLConnection() override {
        disconnect();
    }

    bool connect() override {
        // Actually establish connection
        handle = PQconnectdb(config.connectionString().c_str());
        return PQstatus(handle) == CONNECTION_OK;
    }

    void disconnect() override {
        if (handle) {
            PQfinish(handle);
            handle = nullptr;
        }
    }

    ResultSet executeQuery(const std::string& query) override {
        ResultSet resultSet;
        if (!handle) {
            return resultSet; // Return empty result set if not connected
        }

        PGresult* pgResult = PQexec(handle, query.c_str());
        if (PQresultStatus(pgResult) != PGRES_TUPLES_OK) {
            PQclear(pgResult);
            return resultSet; // Return empty result set on error
        }

        // Process the result and populate the ResultSet
        int numRows = PQntuples(pgResult);
        int numCols = PQnfields(pgResult);

        // Get column names
        std::vector<std::string> columnNames;
        for (int i = 0; i < numCols; i++) {
            columnNames.push_back(PQfname(pgResult, i));
        }

        // Set column names in the result set
        resultSet = ResultSet(columnNames);

        // Process each row
        for (int row = 0; row < numRows; row++) {
            ResultSet::Row rowData;

            for (int col = 0; col < numCols; col++) {
                const char* value     = PQgetvalue(pgResult, row, col);
                int         fieldType = PQftype(pgResult, col);
                bool        isNull    = PQgetisnull(pgResult, row, col);

                if (isNull) {
                    rowData[columnNames[col]] = nullptr;
                } else {
                    // Convert based on PostgreSQL type OIDs
                    // This is a simplified version - in a real implementation you'd handle more types
                    switch (fieldType) {
                    case 23: // INT4OID
                        rowData[columnNames[col]] = std::stoi(value);
                        break;
                    case 1700: // NUMERICOID
                    case 701:  // FLOAT8OID
                        rowData[columnNames[col]] = std::stod(value);
                        break;
                    case 16: // BOOLOID
                        rowData[columnNames[col]] = (std::string(value) == "t");
                        break;
                    default:
                        rowData[columnNames[col]] = std::string(value);
                        break;
                    }
                }
            }

            resultSet.addRow(rowData);
        }

        PQclear(pgResult);
        return resultSet;
    }

    std::shared_ptr<PreparedStatement> prepareStatement(const std::string& query) override {
        // Create a PostgreSQL prepared statement
        return std::make_shared<PostgresSQLPreparedStatement>(query, handle);
    }

    bool execute(const std::string& query) override {
        if (!handle) {
            return false;
        }

        PGresult*      result = PQexec(handle, query.c_str());
        ExecStatusType status = PQresultStatus(result);
        PQclear(result);

        return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
    }

    void beginTransaction() override {
        if (handle) {
            PQexec(handle, "BEGIN");
        }
    }

    void commit() override {
        if (handle) {
            PQexec(handle, "COMMIT");
        }
    }

    void rollback() override {
        if (handle) {
            PQexec(handle, "ROLLBACK");
        }
    }

    [[nodiscard]] bool isConnected() const override {
        return handle != nullptr && PQstatus(handle) == CONNECTION_OK;
    }
};
