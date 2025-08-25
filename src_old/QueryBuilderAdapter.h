#pragma once

#include "QueryBuilder.h"
#include "WhereClause.h"
#include "QuerySet.h"
#include "AbstractConnection.h"
#include "ResultSet.h"
#include "PreparedStatement.h"
#include <memory>
#include <string>

namespace orm {

    /**
     * @brief Adapter class to bridge between QuerySet/WhereClause and QueryBuilder
     *
     * This class provides integration between the existing query building system
     * (QuerySet, WhereClause) and the new database execution components.
     */
    class QueryBuilderAdapter {
      private:
        std::shared_ptr<AbstractConnection> connection;

      public:
        /**
         * @brief Construct a new Query Builder Adapter
         * @param conn Database connection to use
         */
        explicit QueryBuilderAdapter(std::shared_ptr<AbstractConnection> conn) : connection(std::move(conn)) {}

        /**
         * @brief Execute a QuerySet and return results
         * @tparam T The model type for the QuerySet
         * @param querySet QuerySet to execute
         * @return ResultSet containing query results
         */
        template <typename T> ResultSet execute(const BaseQuerySet<T>& querySet) {
            // Convert QuerySet to SQL
            std::string sql = querySet.buildSelect();

            // Execute the query
            return connection->executeQuery(sql);
        }

        /**
         * @brief Create a QueryBuilder from a WhereClause
         * @param whereClause WhereClause to convert
         * @return QueryBuilder with the where clause applied
         */
        QueryBuilder createFromWhereClause(const WhereClause& whereClause) {
            QueryBuilder builder(connection);

            // Start with a basic select
            builder.select("*");

            // Apply the where clause
            builder.where(whereClause.serialize());

            return builder;
        }

        /**
         * @brief Create a QueryBuilder from a QuerySet
         * @tparam T The model type for the QuerySet
         * @param querySet QuerySet to convert
         * @return QueryBuilder with the QuerySet parameters applied
         */
        template <typename T> QueryBuilder createFromQuerySet(const BaseQuerySet<T>& querySet) {
            QueryBuilder builder(connection);

            // Start with the fields from the QuerySet
            if (!querySet.onlyFields.empty()) {
                std::string fields;
                for (const auto& field : querySet.onlyFields) {
                    if (!fields.empty()) {
                        fields += ", ";
                    }
                    fields += field.field->getFullFieldName();
                    if (!field.alias.empty()) {
                        fields += " AS " + field.alias;
                    }
                }
                builder.select(fields);
            } else {
                builder.select("*");
            }

            // Add the table name
            builder.from(querySet.getAlias());

            // Add where clauses
            if (!querySet.filters.empty()) {
                std::string whereClause = querySet.filters[0].serialize();
                builder.where(whereClause);

                // Add additional filters
                for (size_t i = 1; i < querySet.filters.size(); ++i) {
                    builder.andWhere(querySet.filters[i].serialize());
                }
            }

            // Add order by
            if (!querySet.orderFields.empty()) {
                std::string orderBy;
                for (const auto& [field, asc] : querySet.orderFields) {
                    if (!orderBy.empty()) {
                        orderBy += ", ";
                    }

                    std::string fieldName = std::visit(
                            [](const auto& value) -> std::string { return detail::getFullFieldName(value); }, field
                    );

                    orderBy += fieldName + (asc ? " ASC" : " DESC");
                }

                if (!orderBy.empty()) {
                    builder.orderBy(orderBy);
                }
            }

            // Add limit
            if (querySet._limit > 0) {
                builder.limit(querySet._limit);
            }

            // Add offset
            if (!querySet._offset.empty()) {
                builder.offset(std::stoi(querySet._offset));
            }

            return builder;
        }
    };

} // namespace orm
