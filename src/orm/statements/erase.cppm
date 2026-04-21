module;

#include <meta>

export module storm_orm_statements_erase;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_orm_transaction;
import storm_db_concept;

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

    // Statement class for ORM erase operations
    template <typename T, storm::db::DatabaseConnection ConnType> class EraseStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Compile-time single DELETE SQL size calculation
        static consteval auto calculate_single_delete_sql_size() -> size_t {
            using utilities::sql_len::DELETE_FROM;
            using utilities::sql_len::WHERE;
            size_t size = 0;
            size += DELETE_FROM; // "DELETE FROM "
            size += Base::table_name_.size();
            size += WHERE; // " WHERE "
            size += Base::pk_name_.size();
            size += 4; // " = ?"
            size += 1; // null terminator
            return size;
        }

        // Build single DELETE SQL at compile-time using ConstexprString
        static consteval auto build_single_delete_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_single_delete_sql_size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<sql_size> result;

            result.append("DELETE FROM ");
            result.append(Base::table_name_);
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" = ?");

            return result;
        }

        // Pre-computed single DELETE SQL generated at compile-time
        static inline const std::string single_delete_sql_string = std::string(build_single_delete_sql_array());

        // Compile-time all-rows DELETE SQL (no WHERE clause)
        static consteval auto build_delete_all_sql_array() {
            using utilities::sql_len::DELETE_FROM;
            constexpr size_t sql_size = DELETE_FROM + Base::table_name_.size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<sql_size> result;
            result.append("DELETE FROM ");
            result.append(Base::table_name_);
            return result;
        }

        // Pre-computed all-rows DELETE SQL
        static inline const std::string delete_all_sql_string = std::string(build_delete_all_sql_array());

      private:
        // Compile-time bulk DELETE prefix calculation
        static consteval auto calculate_bulk_delete_prefix_size() -> size_t {
            using utilities::sql_len::DELETE_FROM;
            using utilities::sql_len::IN_OPEN;
            using utilities::sql_len::WHERE;
            size_t size = 0;
            size += DELETE_FROM; // "DELETE FROM "
            size += Base::table_name_.size();
            size += WHERE; // " WHERE "
            size += Base::pk_name_.size();
            size += IN_OPEN; // " IN ("
            size += 1;       // null terminator
            return size;
        }

        // Build bulk DELETE prefix at compile-time using ConstexprString
        static consteval auto build_bulk_delete_prefix() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t prefix_size = calculate_bulk_delete_prefix_size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<prefix_size> result;

            result.append("DELETE FROM ");
            result.append(Base::table_name_);
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" IN (");

            return result;
        }

        // Pre-computed bulk DELETE prefix generated at compile-time
        static inline const std::string bulk_delete_prefix = std::string(build_bulk_delete_prefix());
        static constexpr size_t         bulk_delete_prefix_size =
                calculate_bulk_delete_prefix_size() - 1; // Exclude null terminator

        // Maximum chunk size for IN clause (80% of SQLite limit for safety)
        // Defined here so it can be used in compile-time SQL generation
        static constexpr size_t MAX_CHUNK_SIZE = (Base::MAX_DB_VARIABLES * 4) / 5; // 799

        // Compile-time max bulk DELETE SQL size calculation
        static consteval auto calculate_max_bulk_delete_sql_size() -> size_t {
            // prefix + (MAX_CHUNK_SIZE placeholders) + (MAX_CHUNK_SIZE-1 commas) + closing paren + null
            return bulk_delete_prefix_size + MAX_CHUNK_SIZE + (MAX_CHUNK_SIZE - 1) + 1 + 1;
        }

        // Build max bulk DELETE SQL at compile-time (799 placeholders)
        static consteval auto build_max_bulk_delete_sql() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_max_bulk_delete_sql_size() + 50; // Safety buffer
            ConstexprString<sql_size> result;

            // Reuse bulk delete prefix
            result.append(build_bulk_delete_prefix());

            // Append 799 placeholders with commas
            for (size_t i = 0; i < MAX_CHUNK_SIZE; ++i) {
                if (i > 0) {
                    result.append(",");
                }
                result.append("?");
            }
            result.append(")");

            return result;
        }

        // Pre-computed max bulk DELETE SQL (799 placeholders) generated at compile-time
        static inline const std::string max_bulk_delete_sql = std::string(build_max_bulk_delete_sql());

        // Generate single DELETE SQL string (compile-time computed, runtime accessible)
        static auto get_single_delete_sql() -> const std::string& {
            return single_delete_sql_string;
        }

        // Generate bulk DELETE SQL string for IN clause (with thread-local caching)
        // Returns const reference to avoid expensive string copy
        static auto get_bulk_delete_sql(size_t count) -> const std::string& {
            if (count == 1) {
                return get_single_delete_sql();
            }

            // Thread-local cache for common batch sizes (same pattern as INSERT)
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (const auto* cached = cache.find(count)) {
                return *cached; // Return by reference - no copy
            }

            // Calculate exact size needed
            const size_t total_size = bulk_delete_prefix_size + count + (count - 1) + 1; // prefix + ?s + commas + )

            std::string sql;
            sql.reserve(total_size);
            sql = bulk_delete_prefix;

            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ",";
                }
                sql += "?";
            }
            sql += ")";

            // Cache the result and return reference to it
            cache.insert(count, std::move(sql));
            return *cache.find(count); // Guaranteed to exist after insert
        }

      public:
        explicit EraseStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        struct SingleQuery {
            EraseStatement&    stmt;
            const T&           obj;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute_one(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(obj);
            }
            [[nodiscard]] static auto sql() -> std::string {
                return single_delete_sql_string;
            }
        };

        struct BulkQuery {
            EraseStatement&    stmt;
            std::span<const T> objects;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute(objects);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(objects);
            }
            [[nodiscard]] auto sql() -> std::string {
                return std::string(get_bulk_delete_sql(objects.size()));
            }
        };

        struct DeleteAllQuery {
            EraseStatement&    stmt;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute_all();
            }
            [[nodiscard]] auto to_sql() -> std::string {
                return delete_all_sql_string;
            }
            [[nodiscard]] static auto sql() -> std::string {
                return delete_all_sql_string;
            }
        };

        auto query(const T& obj) -> SingleQuery {
            return {*this, obj};
        }
        auto query(std::span<const T> objects) -> BulkQuery {
            return {*this, objects};
        }
        auto query_all() -> DeleteAllQuery {
            return {*this};
        }

        // Returns SQL string with bound parameters inlined for a single DELETE (for debugging)
        [[nodiscard]] auto to_sql(const T& obj) -> std::expected<std::string, Error> {
            auto stmt_result = conn_->prepare_cached(get_single_delete_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt = *stmt_result;
            stmt->reset();
            if (auto bind_result = bind_pk_at(*stmt, obj, 1); !bind_result) {
                return std::unexpected(bind_result.error());
            }
            return stmt->expanded_sql();
        }

        // Returns SQL string with bound parameters inlined for a bulk DELETE (for debugging)
        [[nodiscard]] auto to_sql(std::span<const T> objects) -> std::expected<std::string, Error> {
            if (objects.empty()) {
                return std::string{};
            }
            const auto& bulk_sql = get_bulk_delete_sql(objects.size());
            auto        stmt_res = conn_->prepare_cached(bulk_sql);
            if (!stmt_res) {
                return std::unexpected(stmt_res.error());
            }
            auto* stmt = *stmt_res;
            stmt->reset();
            int param_index = 1;
            for (const auto& obj : objects) {
                if (auto result = bind_pk_at(*stmt, obj, param_index); !result) {
                    return std::unexpected(result.error());
                }
                ++param_index;
            }
            return stmt->expanded_sql();
        }

        [[nodiscard]] auto execute(std::span<const T> objects) -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Fast path for single delete - bypass all batch machinery
            if (objects.size() == 1) {
                return execute_one(objects[0]);
            }

            // Strategy 2: Bulk IN clause (2-799 rows) - no transaction needed
            if (objects.size() <= MAX_CHUNK_SIZE) {
                return execute_bulk(objects);
            }

            // Strategy 3: Chunked IN clause (800+ rows) - RAII transaction guard
            auto txn = TransactionGuard<ConnType>::begin(conn_);
            if (!txn) {
                return std::unexpected(txn.error());
            }

            auto result = execute_chunked(objects);
            if (!result) {
                return result; // ~TransactionGuard will ROLLBACK
            }

            return txn->commit();
        }

        // Delete all rows — executes DELETE FROM <table> with no WHERE clause
        [[nodiscard]] auto execute_all() -> std::expected<void, Error> {
            return conn_->execute(delete_all_sql_string);
        }

        // Single DELETE - pre-cached statement, inlined execution
        [[nodiscard]] __attribute__((hot)) auto execute_one(const T& obj) noexcept -> std::expected<void, Error> {
            // Get or cache the prepared statement
            if (cached_single_stmt_ == nullptr) {
                auto stmt_result = conn_->prepare_cached(get_single_delete_sql());
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_single_stmt_ = *stmt_result;
            }

            // Reset, bind PK, and execute
            cached_single_stmt_->reset();

            if (auto bind_result = bind_pk_at(*cached_single_stmt_, obj, 1); !bind_result) {
                return std::unexpected(bind_result.error());
            }

            auto exec_result = cached_single_stmt_->execute();
            if (!exec_result) {
                return std::unexpected(exec_result.error());
            }

            return {};
        }

        // Bulk execute using IN clause for better performance on small batches
        // Flat code pattern for hot path performance (avoids lambda overhead)
        [[nodiscard]] __attribute__((hot)) auto execute_bulk(std::span<const T> objects) -> std::expected<void, Error> {
            const auto& bulk_sql = get_bulk_delete_sql(objects.size());

            // Prepare statement (cached internally by connection)
            auto stmt_result = conn_->prepare_cached(bulk_sql);
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }

            return bind_pks_and_execute(**stmt_result, objects);
        }

      protected: // Changed to protected so BaseStatement can access
        // Execute large batches using chunked IN clauses
        // NOTE: Transaction is handled by caller (execute_with_transaction in base.cppm)
        // Optimized: pre-cache max bulk statement, only lookup remainder once
        [[nodiscard]] __attribute__((hot)) auto execute_chunked(std::span<const T> objects)
                -> std::expected<void, Error> {
            // Cache max bulk statement pointer (compile-time SQL, no hash lookup after first call)
            if (cached_max_bulk_stmt_ == nullptr) {
                auto stmt_result = conn_->prepare_cached(max_bulk_delete_sql);
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_max_bulk_stmt_ = *stmt_result;
            }

            // Calculate remainder size upfront
            const size_t remainder_size = objects.size() % MAX_CHUNK_SIZE;
            Statement*   remainder_stmt = nullptr;

            // Cache remainder statement if needed (only one hash lookup per batch)
            if (remainder_size > 0) {
                const auto& remainder_sql = get_bulk_delete_sql(remainder_size);
                auto        stmt_result   = conn_->prepare_cached(remainder_sql);
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                remainder_stmt = *stmt_result;
            }

            // Process full chunks using cached pointer (no hash lookups in loop)
            size_t offset = 0;
            while (offset + MAX_CHUNK_SIZE <= objects.size()) {
                auto chunk = objects.subspan(offset, MAX_CHUNK_SIZE);
                if (auto result = bind_pks_and_execute(*cached_max_bulk_stmt_, chunk); !result) {
                    return std::unexpected(result.error());
                }
                offset += MAX_CHUNK_SIZE;
            }

            // Process remainder with pre-cached statement
            if (remainder_size > 0) {
                auto chunk = objects.subspan(offset, remainder_size);
                if (auto result = bind_pks_and_execute(*remainder_stmt, chunk); !result) {
                    return std::unexpected(result.error());
                }
            }

            return {};
        }

        // Bind and execute bulk operation (single statement, multiple parameters)
        // Note: Takes reference since we use prepare_cached which returns a pointer to cached statement
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_pks_and_execute(Statement& stmt, std::span<const T> objects) noexcept -> std::expected<void, Error> {
            // Reset before binding - required for cached statement reuse
            stmt.reset();

            // Bind all primary key values
            int param_index = 1;
            for (const auto& obj : objects) {
                if (auto result = bind_pk_at(stmt, obj, param_index); !result) {
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
        [[nodiscard]] __attribute__((always_inline)) auto bind_pk_at(Statement& stmt, const T& obj, int index) noexcept
                -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:Base::primary_key_:];

            // Use shared binding utility
            return Base::template bind_value_by_type<ConnType>(stmt, index, pk_value);
        }

      private:
        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_single_stmt_   = nullptr; // Cached statement for single DELETE
        mutable Statement*        cached_max_bulk_stmt_ = nullptr; // Cached statement for max bulk (799) DELETE
    };

} // namespace storm::orm::statements