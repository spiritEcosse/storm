#pragma once

#include "PreparedStatement.h"
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

/**
 * @brief PostgreSQL implementation of PreparedStatement
 * 
 * This class provides PostgreSQL-specific implementation of the PreparedStatement interface
 */
class PostgresSQLPreparedStatement : public PreparedStatement {
private:
    PGconn* conn;
    std::string statementName;
    std::vector<std::string> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    bool prepared = false;
    
    // Generate a unique statement name
    static std::string generateStatementName() {
        static int counter = 0;
        return "stmt_" + std::to_string(++counter);
    }
    
    // Ensure the statement is prepared
    bool ensurePrepared() {
        if (prepared) return true;
        
        PGresult* result = PQprepare(
            conn,
            statementName.c_str(),
            sql.c_str(),
            0,  // We'll bind parameters later
            nullptr
        );
        
        bool success = PQresultStatus(result) == PGRES_COMMAND_OK;
        PQclear(result);
        
        if (success) {
            prepared = true;
        }
        
        return prepared;
    }
    
public:
    /**
     * @brief Construct a new PostgreSQL Prepared Statement
     * 
     * @param sqlQuery The SQL query with placeholders
     * @param connection PostgreSQL connection handle
     */
    PostgresSQLPreparedStatement(const std::string& sqlQuery, PGconn* connection)
        : PreparedStatement(sqlQuery), conn(connection), statementName(generateStatementName()) {
        // Initialize with empty parameters
        paramValues.clear();
    }
    
    ~PostgresSQLPreparedStatement() override {
        // Clean up the prepared statement if needed
        if (prepared && conn) {
            std::string deallocSql = "DEALLOCATE " + statementName;
            PGresult* result = PQexec(conn, deallocSql.c_str());
            PQclear(result);
        }
    }
    
    void setString(int index, const std::string& value) override {
        // Ensure we have enough space in our parameter vectors
        while (paramValues.size() < static_cast<size_t>(index)) {
            paramValues.push_back("");
        }
        
        // PostgreSQL uses 0-based indexing, but our API uses 1-based indexing
        paramValues[index - 1] = value;
    }
    
    void setInt(int index, int value) override {
        setString(index, std::to_string(value));
    }
    
    void setDouble(int index, double value) override {
        setString(index, std::to_string(value));
    }
    
    void setBool(int index, bool value) override {
        setString(index, value ? "TRUE" : "FALSE");
    }
    
    void setNull(int index) override {
        // Ensure we have enough space in our parameter vectors
        while (paramValues.size() < static_cast<size_t>(index)) {
            paramValues.push_back("");
        }
        
        // PostgreSQL uses 0-based indexing, but our API uses 1-based indexing
        paramValues[index - 1] = ""; // Empty string will be treated as NULL
    }
    
    void setParameter(int index, const Parameter& value) override {
        std::visit([this, index](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                setString(index, arg);
            } else if constexpr (std::is_same_v<T, int>) {
                setInt(index, arg);
            } else if constexpr (std::is_same_v<T, double>) {
                setDouble(index, arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                setBool(index, arg);
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                setNull(index);
            }
        }, value);
    }
    
    bool execute() override {
        if (!conn || !ensurePrepared()) return false;
        
        // Convert our parameters to the format PQexecPrepared expects
        std::vector<const char*> paramPtrs;
        for (const auto& param : paramValues) {
            paramPtrs.push_back(param.empty() ? nullptr : param.c_str());
        }
        
        PGresult* result = PQexecPrepared(
            conn,
            statementName.c_str(),
            static_cast<int>(paramValues.size()),
            paramPtrs.data(),
            nullptr,  // paramLengths - null for text format
            nullptr,  // paramFormats - null for text format
            0         // resultFormat - 0 for text
        );
        
        ExecStatusType status = PQresultStatus(result);
        PQclear(result);
        
        return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
    }
    
    ResultSet executeQuery() override {
        ResultSet resultSet;
        if (!conn || !ensurePrepared()) return resultSet;
        
        // Convert our parameters to the format PQexecPrepared expects
        std::vector<const char*> paramPtrs;
        for (const auto& param : paramValues) {
            paramPtrs.push_back(param.empty() ? nullptr : param.c_str());
        }
        
        PGresult* pgResult = PQexecPrepared(
            conn,
            statementName.c_str(),
            static_cast<int>(paramValues.size()),
            paramPtrs.data(),
            nullptr,  // paramLengths - null for text format
            nullptr,  // paramFormats - null for text format
            0         // resultFormat - 0 for text
        );
        
        if (PQresultStatus(pgResult) != PGRES_TUPLES_OK) {
            PQclear(pgResult);
            return resultSet;
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
                const char* value = PQgetvalue(pgResult, row, col);
                int fieldType = PQftype(pgResult, col);
                bool isNull = PQgetisnull(pgResult, row, col);
                
                if (isNull) {
                    rowData[columnNames[col]] = nullptr;
                } else {
                    // Convert based on PostgreSQL type OIDs
                    switch (fieldType) {
                        case 23: // INT4OID
                            rowData[columnNames[col]] = std::stoi(value);
                            break;
                        case 1700: // NUMERICOID
                        case 701: // FLOAT8OID
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
    
    int executeUpdate() override {
        if (!conn || !ensurePrepared()) return -1;
        
        // Convert our parameters to the format PQexecPrepared expects
        std::vector<const char*> paramPtrs;
        for (const auto& param : paramValues) {
            paramPtrs.push_back(param.empty() ? nullptr : param.c_str());
        }
        
        PGresult* result = PQexecPrepared(
            conn,
            statementName.c_str(),
            static_cast<int>(paramValues.size()),
            paramPtrs.data(),
            nullptr,  // paramLengths - null for text format
            nullptr,  // paramFormats - null for text format
            0         // resultFormat - 0 for text
        );
        
        ExecStatusType status = PQresultStatus(result);
        int affectedRows = -1;
        
        if (status == PGRES_COMMAND_OK) {
            // Get the number of affected rows
            const char* cmdTuples = PQcmdTuples(result);
            if (cmdTuples && *cmdTuples) {
                affectedRows = std::stoi(cmdTuples);
            }
        }
        
        PQclear(result);
        return affectedRows;
    }
};
