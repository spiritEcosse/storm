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
import <memory>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::BulkSQLCache;
    using storm::orm::utilities::ConstexprString;
    using storm::orm::utilities::TransactionGuard;

    // Statement class for ORM remove operations
    template <typename T, storm::db::DatabaseConnection ConnType> class RemoveStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Compile-time DELETE SQL size calculation
        static consteval size_t calculate_delete_sql_size() {
            size_t size = 0;
            size += 12; // "DELETE FROM "
            size += Base::table_name_.size();
            size += 7; // " WHERE "
            size += Base::pk_name_.size();
            size += 4; // " = ?"
            size += 1; // null terminator
            return size;
        }

        // Build DELETE SQL at compile-time using ConstexprString
        static consteval auto build_delete_sql_array() {
            constexpr size_t          sql_size = calculate_delete_sql_size() + 50; // Add buffer for safety
            ConstexprString<sql_size> result;

            result.append("DELETE FROM ");
            result.append(Base::table_name_);
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" = ?");

            return result;
        }

        // Pre-computed DELETE SQL generated at compile-time
        static constexpr auto           delete_sql_array  = build_delete_sql_array();
        static inline const std::string delete_sql_string = std::string(delete_sql_array);

      public:
        // Public access to DELETE SQL for QuerySet optimization
        static const std::string& get_delete_sql_static() {
            return delete_sql_string;
        }

      private:
        // Compile-time bulk DELETE prefix calculation
        static consteval size_t calculate_bulk_delete_prefix_size() {
            size_t size = 0;
            size += 12; // "DELETE FROM "
            size += Base::table_name_.size();
            size += 7; // " WHERE "
            size += Base::pk_name_.size();
            size += 5; // " IN ("
            size += 1; // null terminator
            return size;
        }

        // Build bulk DELETE prefix at compile-time using ConstexprString
        static consteval auto build_bulk_delete_prefix() {
            constexpr size_t prefix_size = calculate_bulk_delete_prefix_size() + 50; // Add buffer for safety
            ConstexprString<prefix_size> result;

            result.append("DELETE FROM ");
            result.append(Base::table_name_);
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" IN (");

            return result;
        }

        // Pre-computed bulk DELETE prefix generated at compile-time
        static constexpr auto           bulk_delete_prefix_array = build_bulk_delete_prefix();
        static inline const std::string bulk_delete_prefix       = std::string(bulk_delete_prefix_array);
        static constexpr size_t         bulk_delete_prefix_size =
                calculate_bulk_delete_prefix_size() - 1; // Exclude null terminator

        // Generate DELETE SQL string (compile-time computed, runtime accessible)
        static const std::string& get_delete_sql() {
            return delete_sql_string;
        }

        // Generate bulk DELETE SQL string for IN clause (size-specific, with caching)
        static std::string get_bulk_delete_sql(size_t count) {
            if (count == 1) {
                return get_delete_sql();
            }

            // Thread-local cache for common batch sizes
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (const auto* cached = cache.find(count)) {
                return *cached;
            }

            // Cache miss - generate SQL with optimized string building
            // Calculate exact size needed using pre-computed prefix size
            const size_t placeholder_size = 1; // "?"
            const size_t separator_size   = 1; // ","
            const size_t closing_paren    = 1; // ")"
            const size_t total_size       = bulk_delete_prefix_size + (placeholder_size * count) +
                                      (separator_size * (count - 1)) + closing_paren;

            // Reserve exact memory upfront
            std::string sql;
            sql.reserve(total_size);

            // Build SQL with minimal allocations using pre-computed prefix
            sql = bulk_delete_prefix;
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ",";
                }
                sql += "?";
            }
            sql += ")";

            // Cache the result for future use
            cache.insert(count, sql);

            return sql;
        }

      public:
        explicit RemoveStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Fast path for single delete - bypass all batch machinery
            if (objects.size() == 1) {
                return execute_single_optimized(objects[0]);
            }

            // Strategy 2: Bulk IN clause (2-799 rows) - no transaction needed
            if (objects.size() <= MAX_CHUNK_SIZE) {
                return execute_bulk(objects);
            }

            // Strategy 3: Chunked IN clause (800+ rows) - RAII transaction guard
            auto txn = TransactionGuard<ConnType>::begin(*conn_);
            if (!txn) return std::unexpected(txn.error());

            auto result = execute_individual_batch(objects);
            if (!result) return result;  // ~TransactionGuard will ROLLBACK

            return txn->commit();
        }

        // Ultra-optimized single DELETE - pre-cached statement, inlined execution
        [[nodiscard]] __attribute__((hot)) auto execute_single_optimized(const T& obj) noexcept
                -> std::expected<void, Error> {
            // Get or cache the prepared statement
            if (!cached_delete_stmt_) {
                auto stmt_result = conn_->prepare_cached(get_delete_sql());
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_delete_stmt_ = *stmt_result;
            }

            // Direct inline bind and execute - minimal overhead
            auto pk_value = obj.[:Base::primary_key_:];

            // Inline bind without template dispatch
            std::expected<void, Error> bind_result;
            if constexpr (std::is_same_v<decltype(pk_value), int>) {
                bind_result = cached_delete_stmt_->bind_int(1, pk_value);
            } else if constexpr (std::is_convertible_v<decltype(pk_value), std::string_view>) {
                bind_result = cached_delete_stmt_->bind_text(1, std::string_view{pk_value});
            }

            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            // Execute and reset for next use
            auto exec_result = cached_delete_stmt_->execute();
            if (!exec_result) {
                cached_delete_stmt_->reset();
                return std::unexpected(exec_result.error());
            }

            cached_delete_stmt_->reset();
            return {};
        }

        // Bulk execute using IN clause for better performance on small batches
        // Uses prepare_cached for statement reuse across iterations (critical for benchmark perf)
        [[nodiscard]] __attribute__((hot)) auto execute_bulk(std::span<const T> objects) noexcept
                -> std::expected<void, Error> {
            const auto& bulk_sql = get_bulk_delete_sql(objects.size());

            // Use prepare_cached to reuse prepared statements across iterations
            return conn_->prepare_cached(bulk_sql).and_then(
                    [this, objects](Statement* stmt) -> std::expected<void, Error> {
                        return bind_and_execute_bulk(*stmt, objects);
                    }
            );
        }

      protected: // Changed to protected so BaseStatement can access
        // Maximum chunk size for IN clause (80% of SQLite limit for safety)
        static constexpr size_t MAX_CHUNK_SIZE = (Base::MAX_SQLITE_VARIABLES * 4) / 5; // 799

        // Execute large batches using chunked IN clauses
        // NOTE: Transaction is handled by caller (execute_with_transaction in base.cppm)
        // Flat code for maximum performance - no nested lambdas
        [[nodiscard]] __attribute__((hot)) auto execute_individual_batch(std::span<const T> objects) noexcept
                -> std::expected<void, Error> {
            // Process in chunks using IN clause
            size_t offset = 0;
            while (offset < objects.size()) {
                const size_t remaining  = objects.size() - offset;
                const size_t chunk_size = std::min(MAX_CHUNK_SIZE, remaining);
                auto         chunk      = objects.subspan(offset, chunk_size);

                // Get cached SQL for this chunk size
                const auto& bulk_sql    = get_bulk_delete_sql(chunk_size);
                auto        stmt_result = conn_->prepare_cached(bulk_sql);
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }

                // Bind and execute chunk
                if (auto result = bind_and_execute_bulk(*stmt_result.value(), chunk); !result) {
                    return std::unexpected(result.error());
                }

                offset += chunk_size;
            }

            return {};
        }

        // Bind and execute bulk operation (single statement, multiple parameters)
        // Note: Takes reference since we use prepare_cached which returns a pointer to cached statement
        [[nodiscard]] auto bind_and_execute_bulk(Statement& stmt, std::span<const T> objects) noexcept
                -> std::expected<void, Error> {
            // Reset before binding - required for cached statement reuse
            stmt.reset();

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

            return {};
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

        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_delete_stmt_ = nullptr; // Cached statement for optimized single DELETE
    };

} // namespace storm::orm::statements