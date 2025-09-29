module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_remove;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_db_concept;
import storm_db_sqlite;

import <expected>;
import <string>;
import <string_view>;
import <span>;
import <concepts>;
import <format>;
import <meta>;
import <type_traits>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::BulkSQLCache;

    // Statement class for ORM remove operations
    template <typename T, storm::db::DatabaseConnection ConnType> class RemoveStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-compute DELETE SQL string template at compile-time
        static consteval std::string_view get_delete_sql_template() {
            return "DELETE FROM {} WHERE {} = ?";
        }

        // Generate DELETE SQL string at runtime (cached)
        static const std::string& get_delete_sql() {
            static const std::string sql = std::format(get_delete_sql_template(), Base::table_name_, Base::pk_name_);
            return sql;
        }

        // Generate bulk DELETE SQL string for IN clause (size-specific)
        static std::string get_bulk_delete_sql(size_t count) {
            if (count == 1) {
                return get_delete_sql();
            }

            // Thread-local cache for common batch sizes
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (auto* cached = cache.find(count)) {
                return *cached;
            }

            // Cache miss - generate SQL with optimized string building
            std::string sql;

            // Pre-calculate size for efficient allocation
            const size_t base_size =
                    std::format("DELETE FROM {} WHERE {} IN ()", Base::table_name_, Base::pk_name_).size();
            const size_t placeholders_size = (count * 1) + ((count - 1) * 1); // count * "?" + (count-1) * ","
            sql.reserve(base_size + placeholders_size);

            // Build SQL efficiently
            sql = std::format("DELETE FROM {} WHERE {} IN (", Base::table_name_, Base::pk_name_);
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ",";
                }
                sql += "?";
            }
            sql += ")";

            // Store in cache
            cache.insert(count, sql);
            return sql;
        }

      public:
        explicit RemoveStatement(Connection& conn) : conn_(conn) {}

        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            if (objects.empty()) {
                return true;
            }

            // Use BaseStatement's generic batch execution
            return Base::execute_standard_batch(*this, objects, 1); // 1 variable per object (primary key)
        }

        // Bulk execute using IN clause for better performance on small batches
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            auto bulk_sql = get_bulk_delete_sql(objects.size());

            return conn_.prepare(bulk_sql).and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                return bind_and_execute_bulk(std::move(stmt), objects);
            });
        }

      protected: // Changed to protected so BaseStatement can access
        // Execute individual deletes for large batches (with transaction)
        [[nodiscard]] auto execute_individual_batch(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            return Base::template execute_with_transaction<ConnType>(
                    conn_, Base::should_use_transaction(objects), [this, objects]() -> std::expected<bool, Error> {
                        return Base::template execute_with_statement<ConnType>(
                                conn_, get_delete_sql(), [this, objects](auto& stmt) -> std::expected<bool, Error> {
                                    for (const auto& obj : objects) {
                                        // Monadic composition: reset → bind → execute
                                        if (auto result = Base::reset_bind_and_execute(
                                                    stmt, [this, &obj](auto& s) { return bind_primary_key(s, obj); }
                                            );
                                            !result) {
                                            return std::unexpected(result.error());
                                        }
                                    }
                                    return true;
                                }
                        );
                    }
            );
        }

        // Bind and execute bulk operation (single statement, multiple parameters)
        [[nodiscard]] auto bind_and_execute_bulk(Statement stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Bind all primary key values
            int param_index = 1;
            for (const auto& obj : objects) {
                if (auto result = bind_primary_key_at_index(stmt, obj, param_index); !result) {
                    return std::unexpected(result.error());
                }
                ++param_index;
            }

            // Execute once with all parameters bound
            if (auto result = stmt.execute(); !result) {
                return std::unexpected(result.error());
            }

            return true;
        }

        // Bind primary key value at specific parameter index
        [[nodiscard]] auto bind_primary_key_at_index(Statement& stmt, const T& obj, int index) noexcept
                -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:Base::primary_key_:];

            // Use shared binding utility
            return Base::template bind_value_by_type<ConnType>(stmt, index, pk_value);
        }

        // Bind primary key value using pre-computed reflection data
        [[nodiscard]] auto bind_primary_key(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:Base::primary_key_:];

            // Use shared binding utility
            return Base::template bind_value_by_type<ConnType>(stmt, 1, pk_value);
        }

        Connection& conn_;
    };

} // namespace storm::orm::statements