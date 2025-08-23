#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include "AbstractConnection.h"
#include "ResultSet.h"
#include "PreparedStatement.h"

/**
 * @brief Fluent interface for building SQL queries
 * 
 * This class provides a fluent interface for building SQL queries
 * with proper parameter binding for security.
 */
class QueryBuilder {
private:
    std::shared_ptr<AbstractConnection> connection;
    std::ostringstream query;
    std::vector<PreparedStatement::Parameter> parameters;
    
    enum class QueryType {
        SELECT,
        INSERT,
        UPDATE,
        DELETE,
        NONE
    };
    
    QueryType queryType = QueryType::NONE;
    bool whereAdded = false;
    
public:
    /**
     * @brief Construct a new Query Builder object
     * @param conn AbstractConnection to use for executing queries
     */
    explicit QueryBuilder(std::shared_ptr<AbstractConnection> conn) : connection(std::move(conn)) {}
    
    /**
     * @brief Start a SELECT query
     * @param columns Columns to select, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& select(const std::string& columns) {
        queryType = QueryType::SELECT;
        query << "SELECT " << columns;
        return *this;
    }
    
    /**
     * @brief Start an INSERT query
     * @param table Table to insert into
     * @param columns Columns to insert into, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& insertInto(const std::string& table, const std::string& columns) {
        queryType = QueryType::INSERT;
        query << "INSERT INTO " << table << " (" << columns << ")";
        return *this;
    }
    
    /**
     * @brief Start an UPDATE query
     * @param table Table to update
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& update(const std::string& table) {
        queryType = QueryType::UPDATE;
        query << "UPDATE " << table;
        return *this;
    }
    
    /**
     * @brief Start a DELETE query
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& deleteFrom(const std::string& table) {
        queryType = QueryType::DELETE;
        query << "DELETE FROM " << table;
        return *this;
    }
    
    /**
     * @brief Add FROM clause to a SELECT query
     * @param tables Tables to select from, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& from(const std::string& tables) {
        if (queryType == QueryType::SELECT) {
            query << " FROM " << tables;
        }
        return *this;
    }
    
    /**
     * @brief Add WHERE clause to a query
     * @param condition WHERE condition
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& where(const std::string& condition) {
        if (queryType != QueryType::NONE) {
            query << " WHERE " << condition;
            whereAdded = true;
        }
        return *this;
    }
    
    /**
     * @brief Add WHERE clause with parameter binding
     * @param condition WHERE condition with ? placeholders
     * @param param Parameter value to bind
     * @return Reference to this builder for method chaining
     */
    template<typename T>
    QueryBuilder& where(const std::string& condition, const T& param) {
        if (queryType != QueryType::NONE) {
            query << " WHERE " << condition;
            parameters.push_back(param);
            whereAdded = true;
        }
        return *this;
    }
    
    /**
     * @brief Add AND condition to a WHERE clause
     * @param condition AND condition
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& andWhere(const std::string& condition) {
        if (whereAdded) {
            query << " AND " << condition;
        }
        return *this;
    }
    
    /**
     * @brief Add AND condition with parameter binding
     * @param condition AND condition with ? placeholders
     * @param param Parameter value to bind
     * @return Reference to this builder for method chaining
     */
    template<typename T>
    QueryBuilder& andWhere(const std::string& condition, const T& param) {
        if (whereAdded) {
            query << " AND " << condition;
            parameters.push_back(param);
        }
        return *this;
    }
    
    /**
     * @brief Add OR condition to a WHERE clause
     * @param condition OR condition
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& orWhere(const std::string& condition) {
        if (whereAdded) {
            query << " OR " << condition;
        }
        return *this;
    }
    
    /**
     * @brief Add OR condition with parameter binding
     * @param condition OR condition with ? placeholders
     * @param param Parameter value to bind
     * @return Reference to this builder for method chaining
     */
    template<typename T>
    QueryBuilder& orWhere(const std::string& condition, const T& param) {
        if (whereAdded) {
            query << " OR " << condition;
            parameters.push_back(param);
        }
        return *this;
    }
    
    /**
     * @brief Add ORDER BY clause to a query
     * @param columns Columns to order by, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& orderBy(const std::string& columns) {
        if (queryType != QueryType::NONE) {
            query << " ORDER BY " << columns;
        }
        return *this;
    }
    
    /**
     * @brief Add GROUP BY clause to a query
     * @param columns Columns to group by, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& groupBy(const std::string& columns) {
        if (queryType != QueryType::NONE) {
            query << " GROUP BY " << columns;
        }
        return *this;
    }
    
    /**
     * @brief Add HAVING clause to a query
     * @param condition HAVING condition
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& having(const std::string& condition) {
        if (queryType != QueryType::NONE) {
            query << " HAVING " << condition;
        }
        return *this;
    }
    
    /**
     * @brief Add LIMIT clause to a query
     * @param limit Maximum number of rows to return
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& limit(int limit) {
        if (queryType != QueryType::NONE) {
            query << " LIMIT " << limit;
        }
        return *this;
    }
    
    /**
     * @brief Add OFFSET clause to a query
     * @param offset Number of rows to skip
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& offset(int offset) {
        if (queryType != QueryType::NONE) {
            query << " OFFSET " << offset;
        }
        return *this;
    }
    
    /**
     * @brief Add VALUES clause to an INSERT query
     * @param values Values to insert, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& values(const std::string& values) {
        if (queryType == QueryType::INSERT) {
            query << " VALUES (" << values << ")";
        }
        return *this;
    }
    
    /**
     * @brief Add SET clause to an UPDATE query
     * @param assignments Column assignments, comma-separated
     * @return Reference to this builder for method chaining
     */
    QueryBuilder& set(const std::string& assignments) {
        if (queryType == QueryType::UPDATE) {
            query << " SET " << assignments;
        }
        return *this;
    }
    
    /**
     * @brief Execute the query and return results
     * @return ResultSet containing query results
     */
    ResultSet execute() {
        auto stmt = connection->prepareStatement(query.str());
        
        // Bind parameters
        for (size_t i = 0; i < parameters.size(); ++i) {
            stmt->setParameter(i + 1, parameters[i]);
        }
        
        // Execute based on query type
        if (queryType == QueryType::SELECT) {
            return stmt->executeQuery();
        } else {
            stmt->executeUpdate();
            return ResultSet();
        }
    }
    
    /**
     * @brief Get the constructed SQL query string
     * @return SQL query string
     */
    [[nodiscard]] std::string getSql() const {
        return query.str();
    }
};
