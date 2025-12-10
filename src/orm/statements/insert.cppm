module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_insert;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_db_concept;
import storm_db_sqlite;

import <expected>;
import <string>;
import <string_view>;
import <concepts>;
import <format>;
import <meta>;
import <array>;
import <span>;
import <vector>;
import <type_traits>;
import <memory>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::BulkSQLCache;
    using storm::orm::utilities::ConstexprString;

    // Configuration options for INSERT operations
    struct InsertOptions {
        std::optional<size_t> batch_size = std::nullopt; // nullopt = automatic (999/field_count)
        bool                  return_ids = true;         // true = return generated IDs (backward compatible default)
    };

    // Statement class for ORM insert operations
    template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-compute placeholders for SQL VALUES clause (excluding PK for auto-increment)
        static consteval std::string build_placeholders() {
            std::string result;
            bool        first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                // Skip primary key
                if (Base::all_members_[i] == Base::primary_key_) {
                    continue;
                }
                if (!first) {
                    result += ", ";
                }
                result += "?";
                first = false;
            }
            return result;
        }

        // Pre-computed placeholders (excludes PK for auto-increment)
        static constexpr auto placeholders_ = build_placeholders();

        // Compile-time SQL size calculation
        static consteval size_t calculate_insert_sql_size() {
            size_t size = 0;
            size += 12; // "INSERT INTO "

            // Table name length
            size += Base::table_name_.size();

            size += 2; // " ("

            // Field names length
            size += Base::calculate_field_names_size();

            size += 10; // ") VALUES ("

            // Placeholders length
            size += placeholders_.size();

            size += 1; // ")"
            size += 1; // null terminator

            return size;
        }

        // Build INSERT SQL at compile-time using ConstexprString
        static consteval auto build_insert_sql_array() {
            constexpr size_t          sql_size = calculate_insert_sql_size() + 100; // Add buffer for safety
            ConstexprString<sql_size> result;

            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(Base::build_non_pk_field_names_list());
            result.append(") VALUES (");
            result.append(placeholders_);
            result.append(")");

            return result;
        }

        // Pre-computed INSERT SQL generated at compile-time
        static constexpr auto           insert_sql_array  = build_insert_sql_array();
        static inline const std::string insert_sql_string = std::string(insert_sql_array);

      public:
        // Public access to INSERT SQL for QuerySet optimization
        static const std::string& get_insert_sql_static() {
            return insert_sql_string;
        }

      private:
        // Compile-time bulk INSERT prefix calculation
        static consteval size_t calculate_bulk_insert_prefix_size() {
            size_t size = 0;
            size += 12; // "INSERT INTO "
            size += Base::table_name_.size();
            size += 2; // " ("
            size += Base::calculate_field_names_size();
            size += 10; // ") VALUES "
            size += 1;  // null terminator
            return size;
        }

        // Build bulk INSERT prefix at compile-time using ConstexprString
        static consteval auto build_bulk_insert_prefix() {
            constexpr size_t prefix_size = calculate_bulk_insert_prefix_size() + 50; // Add buffer for safety
            ConstexprString<prefix_size> result;

            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(Base::build_non_pk_field_names_list());
            result.append(") VALUES ");

            return result;
        }

        // Pre-computed bulk INSERT prefix generated at compile-time
        static constexpr auto           bulk_insert_prefix_array = build_bulk_insert_prefix();
        static inline const std::string bulk_insert_prefix       = std::string(bulk_insert_prefix_array);
        static constexpr size_t         bulk_insert_prefix_size =
                calculate_bulk_insert_prefix_size() - 1; // Exclude null terminator

        // Generate INSERT SQL string (compile-time computed, runtime accessible)
        static const std::string& get_insert_sql() {
            return insert_sql_string;
        }

        // Generate bulk INSERT SQL with multiple value sets (with caching)
        // Returns const reference to avoid expensive string copy
        static const std::string& get_bulk_insert_sql(size_t count) {
            // Thread-local cache for common batch sizes
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (const auto* cached = cache.find(count)) {
                return *cached; // Return by reference - no copy
            }

            // Build optimized SQL with pre-allocation
            // Pre-compute the value template once
            std::string value_template = "(";
            value_template += placeholders_;
            value_template += ")";

            // Calculate exact size needed using pre-computed prefix size
            const size_t value_size     = value_template.size();
            const size_t separator_size = 2; // ", "
            const size_t total_size = bulk_insert_prefix_size + (value_size * count) + (separator_size * (count - 1));

            // Reserve exact memory upfront
            std::string sql;
            sql.reserve(total_size);

            // Build SQL with minimal allocations using pre-computed prefix
            sql = bulk_insert_prefix;
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ", ";
                }
                sql += value_template;
            }

            // Cache the result and return reference to it
            cache.insert(count, std::move(sql));
            return *cache.find(count); // Guaranteed to exist after insert
        }

      public:
        explicit InsertStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Batch insert operation with optional configuration
        [[nodiscard]] auto
        execute(std::span<const T> objects, std::optional<InsertOptions> opts = std::nullopt) noexcept
                -> std::expected<std::vector<int64_t>, Error> {
            if (objects.empty()) {
                return std::vector<int64_t>{};
            }

            // Use default options if not provided
            InsertOptions options = opts.value_or(InsertOptions{});

            // Single object - use optimized path (no bulk SQL overhead)
            if (objects.size() == 1) {
                auto result = execute_single_optimized(objects[0], options.return_ids);
                if (!result) {
                    return std::unexpected(result.error());
                }
                return std::vector<int64_t>{result.value()};
            }

            // Calculate effective batch size
            constexpr size_t max_allowed          = Base::MAX_SQLITE_VARIABLES / Base::field_count_;
            size_t           effective_batch_size = options.batch_size.value_or(max_allowed);
            effective_batch_size                  = std::min(effective_batch_size, max_allowed); // Cap at SQLite max

            // Batch path with custom batch size
            if (objects.size() <= effective_batch_size) {
                // Fits in one bulk SQL
                return execute_bulk(objects, options.return_ids);
            }
            // Need chunking with custom batch size
            return execute_chunked_bulk_custom(objects, effective_batch_size, options.return_ids);
        }

        // Ultra-optimized single INSERT - simple imperative style
        [[nodiscard]] __attribute__((hot)) auto execute_single_optimized(const T& obj, bool return_id = true) noexcept
                -> std::expected<int64_t, Error> {
            // Get or cache the prepared statement
            // SAFETY: Connection reserves capacity (32) to prevent rehashing and dangling pointers
            if (!cached_insert_stmt_) [[unlikely]] {
                auto stmt_result = conn_->prepare_cached(get_insert_sql());
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_insert_stmt_ = *stmt_result;
            }

            // Bind non-PK fields
            auto bind_result = Base::template bind_non_pk_fields_impl<ConnType, Statement>(
                    *cached_insert_stmt_, obj, typename Base::field_indices_t()
            );
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }

            // Execute
            auto exec_result = cached_insert_stmt_->execute();
            if (!exec_result) [[unlikely]] {
                cached_insert_stmt_->reset();
                return std::unexpected(exec_result.error());
            }

            // Get ID if requested, otherwise return 0
            int64_t id = return_id ? conn_->last_insert_rowid() : 0;
            cached_insert_stmt_->reset();

            return id;
        }

      protected: // Changed to protected so BaseStatement can access
        // Bind non-PK fields for INSERT (skips primary key for auto-increment)
        [[nodiscard]] auto bind_all_fields(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            return Base::template bind_non_pk_fields_impl<ConnType, Statement>(
                    stmt, obj, typename Base::field_indices_t()
            );
        }

        // Execute bulk INSERT with multiple VALUES clauses
        // NOTE: No transaction wrapper needed - single INSERT statement is already atomic
        [[nodiscard]] __attribute__((hot)) auto
        execute_bulk(std::span<const T> objects, bool return_ids = true) noexcept
                -> std::expected<std::vector<int64_t>, Error> {
            const auto& sql = get_bulk_insert_sql(objects.size());

            // Use prepare_cached to reuse prepared statements across iterations
            return conn_->prepare_cached(sql).and_then(
                    [this, objects, return_ids](Statement* stmt) -> std::expected<std::vector<int64_t>, Error> {
                        return Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                                       *stmt, objects, typename Base::field_indices_t()
                        )
                                .and_then([stmt]() { return stmt->execute(); })
                                .transform([this, objects, return_ids]() {
                                    if (!return_ids) {
                                        return std::vector<int64_t>{}; // Return empty vector
                                    }

                                    // Get the last inserted row ID
                                    // For bulk INSERT with multiple VALUES, last_insert_rowid() returns the
                                    // ID of the LAST row We need to calculate the first ID by subtracting
                                    // the count
                                    int64_t last_id  = conn_->last_insert_rowid();
                                    int64_t first_id = last_id - static_cast<int64_t>(objects.size()) + 1;

                                    // Generate consecutive IDs for bulk insert
                                    std::vector<int64_t> ids(objects.size());
                                    for (size_t i = 0; i < objects.size(); ++i) {
                                        ids[i] = first_id + static_cast<int64_t>(i);
                                    }

                                    return ids;
                                });
                    }
            );
        }

        // Execute CHUNKED bulk inserts with custom batch size
        [[nodiscard]] auto execute_chunked_bulk_custom(
                std::span<const T> objects, size_t custom_bulk_size, bool return_ids = true
        ) noexcept -> std::expected<std::vector<int64_t>, Error> {
            std::vector<int64_t> all_ids;
            if (return_ids) {
                all_ids.reserve(objects.size());
            }

            // Process in chunks of custom_bulk_size
            for (size_t offset = 0; offset < objects.size(); offset += custom_bulk_size) {
                size_t chunk_size = std::min(custom_bulk_size, objects.size() - offset);
                auto   chunk      = objects.subspan(offset, chunk_size);

                const auto& sql = get_bulk_insert_sql(chunk.size());

                auto chunk_result = conn_->prepare_cached(sql).and_then(
                        [this, chunk, return_ids](Statement* stmt) -> std::expected<std::vector<int64_t>, Error> {
                            return Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                                           *stmt, chunk, typename Base::field_indices_t()
                            )
                                    .and_then([stmt]() { return stmt->execute(); })
                                    .transform([this, chunk, return_ids]() {
                                        if (!return_ids) {
                                            return std::vector<int64_t>{};
                                        }

                                        int64_t last_id  = conn_->last_insert_rowid();
                                        int64_t first_id = last_id - static_cast<int64_t>(chunk.size()) + 1;

                                        std::vector<int64_t> ids(chunk.size());
                                        for (size_t i = 0; i < chunk.size(); ++i) {
                                            ids[i] = first_id + static_cast<int64_t>(i);
                                        }

                                        return ids;
                                    });
                        }
                );

                if (!chunk_result) {
                    return std::unexpected(chunk_result.error());
                }

                if (return_ids) {
                    for (const auto& id : *chunk_result) {
                        all_ids.push_back(id);
                    }
                }
            }

            return all_ids;
        }

      private:
        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_insert_stmt_ = nullptr;
        // SAFETY: Safe to cache raw pointer because Connection reserves capacity (32)
        // to prevent rehashing. Typical ORM usage won't exceed 32 unique statements.
    };

} // namespace storm::orm::statements