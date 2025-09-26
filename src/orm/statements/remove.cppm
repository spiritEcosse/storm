module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_remove;

import storm_orm_statements_base;
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

    // Statement class for ORM remove operations
    template <typename T, storm::db::DatabaseConnection ConnType>
    class RemoveStatement : private BaseStatement<T> {
        using Base = BaseStatement<T>;
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

            std::string placeholders = "?";
            for (size_t i = 1; i < count; ++i) {
                placeholders += ",?";
            }

            return std::format("DELETE FROM {} WHERE {} IN ({})", Base::table_name_, Base::pk_name_, placeholders);
        }

      public:
        explicit RemoveStatement(Connection& conn) : conn_(conn) {}

        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            if (objects.empty()) {
                return true;
            }

            // For small batches, use bulk IN clause for better performance
            if (objects.size() <= 50) {  // SQLite has SQLITE_MAX_VARIABLE_NUMBER limit (default 999)
                return execute_bulk(objects);
            }

            // For large batches, use individual statements with transactions
            // Use cached prepared statement for better performance
            if constexpr (requires { conn_.prepare_cached(get_delete_sql()); }) {
                return conn_.prepare_cached(get_delete_sql())
                        .and_then([this, objects](Statement* stmt) -> std::expected<bool, Error> {
                            return bind_and_execute_cached(*stmt, objects);
                        });
            } else {
                // Fallback to regular prepare for non-cached connections
                return conn_.prepare(get_delete_sql())
                        .and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                            return bind_and_execute(std::move(stmt), objects);
                        });
            }
        }

        // Bulk execute using IN clause for better performance on small batches
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            auto bulk_sql = get_bulk_delete_sql(objects.size());

            return conn_.prepare(bulk_sql)
                    .and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                        return bind_and_execute_bulk(std::move(stmt), objects);
                    });
        }

      private:
        // Bind primary keys and execute for all objects (with transaction wrapping for batches)
        [[nodiscard]] auto bind_and_execute(Statement stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Use transaction for batch operations to improve performance
            bool use_transaction = Base::should_use_transaction(objects);

            if (use_transaction) {
                if (auto result = Base::begin_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            for (const auto& obj : objects) {
                // Reset statement for reuse
                stmt.reset();

                // Bind primary key value using meta reflection
                if (auto result = bind_primary_key(stmt, obj); !result) {
                    if (use_transaction) {
                        Base::rollback_transaction(conn_);
                    }
                    return std::unexpected(result.error());
                }

                // Execute statement
                if (auto result = stmt.execute(); !result) {
                    if (use_transaction) {
                        Base::rollback_transaction(conn_);
                    }
                    return std::unexpected(result.error());
                }
            }

            if (use_transaction) {
                if (auto result = Base::commit_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            return true;
        }

        // Optimized version for cached statements
        [[nodiscard]] auto bind_and_execute_cached(Statement& stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Use transaction for batch operations to improve performance
            bool use_transaction = Base::should_use_transaction(objects);

            if (use_transaction) {
                if (auto result = Base::begin_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            for (const auto& obj : objects) {
                // Reset statement for reuse
                stmt.reset();

                // Bind primary key value using meta reflection
                if (auto result = bind_primary_key(stmt, obj); !result) {
                    if (use_transaction) {
                        Base::rollback_transaction(conn_);
                    }
                    return std::unexpected(result.error());
                }

                // Execute statement
                if (auto result = stmt.execute(); !result) {
                    if (use_transaction) {
                        Base::rollback_transaction(conn_);
                    }
                    return std::unexpected(result.error());
                }
            }

            if (use_transaction) {
                if (auto result = Base::commit_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            return true;
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