module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (shared consteval SQL builder + to_sql_impl).

#include <meta>

export module storm_orm_statements_insert;

import std;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_orm_transaction;
import storm_db_concept;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::BulkSQLCache;
    using storm::orm::utilities::ConstexprString;
    using storm::orm::utilities::TransactionGuard;

    // Controls whether single INSERT returns the auto-generated ID
    enum class ReturnId : bool { No = false, Yes = true };

    // Configuration options for batch INSERT operations
    struct InsertOptions {
        std::optional<std::size_t> batch_size = std::nullopt; // nullopt = automatic (999/field_count)
    };

    // Statement class for ORM insert operations
    template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-compute placeholders for SQL VALUES clause (excluding PK for auto-increment)
        static consteval auto build_placeholders() {
            ConstexprString<utilities::buffer_size::SQL_SMALL> result;
            bool                                               first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
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
        static consteval auto calculate_insert_sql_size() -> std::size_t {
            using utilities::sql_len::INSERT_INTO;
            using utilities::sql_len::VALUES_OPEN;
            std::size_t size = 0;
            size += INSERT_INTO; // "INSERT INTO "

            // Table name length
            size += Base::table_name_.size();

            size += 2; // " ("

            // Field names length
            size += Base::calculate_field_names_size();

            size += VALUES_OPEN; // ") VALUES ("

            // Placeholders length
            size += placeholders_.len;

            size += 1; // ")"
            size += 1; // null terminator

            return size;
        }

        // Shared compile-time INSERT SQL builder for the RETURNING and non-RETURNING variants.
        // The +64 padding on the RETURNING path covers " RETURNING <pk_name>".
        template <bool WithReturning> static consteval auto build_insert_sql_array_impl() {
            constexpr std::size_t extra =
                    WithReturning ? (utilities::sql_len::XL_BUFFER + 64) : utilities::sql_len::XL_BUFFER;
            constexpr std::size_t     sql_size = calculate_insert_sql_size() + extra;
            ConstexprString<sql_size> result;

            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(Base::build_non_pk_field_names_list());
            result.append(") VALUES (");
            result.append(placeholders_);
            if constexpr (WithReturning) {
                result.append(") RETURNING ");
                result.append(Base::pk_name_);
            } else {
                result.append(")");
            }

            return result;
        }

        // Pre-computed INSERT ... RETURNING SQL (both SQLite 3.35+ and PostgreSQL)
        static constexpr auto           insert_returning_sql_array  = build_insert_sql_array_impl<true>();
        static inline const std::string insert_returning_sql_string = std::string(insert_returning_sql_array);

        // Pre-computed INSERT SQL without RETURNING (faster path when ID not needed)
        static constexpr auto           insert_sql_array  = build_insert_sql_array_impl<false>();
        static inline const std::string insert_sql_string = std::string(insert_sql_array);

      private:
        // Compile-time bulk INSERT prefix calculation
        static consteval auto calculate_bulk_insert_prefix_size() -> std::size_t {
            using utilities::sql_len::INSERT_INTO;
            using utilities::sql_len::VALUES_OPEN;
            std::size_t size = 0;
            size += INSERT_INTO; // "INSERT INTO "
            size += Base::table_name_.size();
            size += 2; // " ("
            size += Base::calculate_field_names_size();
            size += VALUES_OPEN; // ") VALUES "
            size += 1;           // null terminator
            return size;
        }

        // Build bulk INSERT prefix at compile-time using ConstexprString
        static consteval auto build_bulk_insert_prefix() {
            constexpr std::size_t prefix_size = calculate_bulk_insert_prefix_size() + utilities::sql_len::LARGE_BUFFER;
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
        static constexpr std::size_t    bulk_insert_prefix_size =
                calculate_bulk_insert_prefix_size() - 1; // Exclude null terminator

        // Pre-computed RETURNING suffix: " RETURNING <pk_name>"
        static constexpr auto returning_suffix_array = [] consteval {
            ConstexprString<64> result;
            result.append(" RETURNING ");
            result.append(Base::pk_name_);
            return result;
        }();
        static inline const std::string returning_suffix = std::string(returning_suffix_array);

        // Build bulk INSERT SQL body (shared by both returning and non-returning variants)
        static auto build_bulk_insert_body(std::size_t count) -> std::string {
            // Guard count == 0: (count - 1) below wraps to SIZE_MAX, and an empty
            // VALUES list is invalid SQL anyway. Return the bare prefix. See #359.
            if (count == 0) {
                return bulk_insert_prefix;
            }

            std::string value_template = "(";
            value_template += placeholders_;
            value_template += ")";

            const std::size_t value_size     = value_template.size();
            const std::size_t separator_size = 2; // ", "
            const std::size_t total_size =
                    bulk_insert_prefix_size + (value_size * count) + (separator_size * (count - 1));

            std::string sql;
            sql.reserve(total_size);

            sql = bulk_insert_prefix;
            for (std::size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ", ";
                }
                sql += value_template;
            }
            return sql;
        }

        // Generate bulk INSERT SQL with multiple value sets (with caching)
        // Returns const reference to avoid expensive string copy
        static auto get_bulk_insert_sql(std::size_t count) -> const std::string& {
            // Thread-local cache for common batch sizes
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (const auto* cached = cache.find(count)) {
                return *cached; // Return by reference - no copy
            }

            cache.insert(count, build_bulk_insert_body(count));
            return *cache.find(count); // Guaranteed to exist after insert
        }

        // Generate bulk INSERT ... RETURNING <pk> SQL (with separate cache)
        static auto get_bulk_insert_returning_sql(std::size_t count) -> const std::string& {
            static thread_local BulkSQLCache cache;

            if (const auto* cached = cache.find(count)) {
                return *cached;
            }

            std::string sql = build_bulk_insert_body(count);
            sql += returning_suffix;

            cache.insert(count, std::move(sql));
            return *cache.find(count);
        }

      public:
        explicit InsertStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        struct SingleQuery {
            InsertStatement    stmt;
            const T&           obj;
            [[nodiscard]] auto execute() -> std::expected<std::int64_t, Error> {
                return stmt.execute_single_optimized(obj, true);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(obj);
            }
            [[nodiscard]] static auto sql() -> std::string {
                return insert_returning_sql_string;
            }
        };

        struct VoidQuery {
            InsertStatement    stmt;
            const T&           obj;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute_single_void(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql_no_returning(obj);
            }
            [[nodiscard]] static auto sql() -> std::string {
                return insert_sql_string;
            }
        };

        struct BulkQuery {
            InsertStatement              stmt;
            std::span<const T>           objects;
            std::optional<InsertOptions> opts;
            [[nodiscard]] auto           execute() -> std::expected<void, Error> {
                return stmt.execute(objects, opts);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(objects);
            }
            [[nodiscard]] auto sql() -> std::string {
                return std::string(get_bulk_insert_sql(objects.size()));
            }
        };

        struct BulkReturningQuery {
            InsertStatement              stmt; // NOSONAR(cpp:S1659) — aggregate struct, same pattern as BulkQuery
            std::span<const T>           objects;
            std::optional<InsertOptions> opts;

            [[nodiscard]] auto execute() -> std::expected<std::vector<std::int64_t>, Error> { // NOSONAR(cpp:S1659)
                return stmt.execute_returning(objects, opts);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> { // NOSONAR(cpp:S1659)
                return stmt.to_sql_returning(objects);
            }
            [[nodiscard]] auto sql() -> std::string { // NOSONAR(cpp:S1659)
                return std::string(get_bulk_insert_returning_sql(objects.size()));
            }
        };

        template <ReturnId R = ReturnId::Yes> auto query(const T& obj [[clang::lifetimebound]]) {
            if constexpr (R == ReturnId::Yes) {
                return SingleQuery{std::move(*this), obj};
            } else {
                return VoidQuery{std::move(*this), obj};
            }
        }
        auto
        query(std::span<const T> objects [[clang::lifetimebound]], std::optional<InsertOptions> opts = std::nullopt)
                -> BulkQuery {
            return {std::move(*this), objects, opts};
        }
        template <ReturnId R>
        auto
        query(std::span<const T> objects [[clang::lifetimebound]], std::optional<InsertOptions> opts = std::nullopt) {
            if constexpr (R == ReturnId::Yes) {
                return BulkReturningQuery{std::move(*this), objects, opts};
            } else {
                return BulkQuery{std::move(*this), objects, opts};
            }
        }

        // Returns SQL string with bound parameters inlined for a single INSERT (for debugging)
        [[nodiscard]] auto to_sql(const T& obj) -> std::expected<std::string, Error> {
            return to_sql_impl(insert_returning_sql_string, [&](Statement& s) { return bind_single(s, obj); });
        }

        // Returns SQL string for INSERT without RETURNING (for debugging)
        [[nodiscard]] auto to_sql_no_returning(const T& obj) -> std::expected<std::string, Error> {
            return to_sql_impl(insert_sql_string, [&](Statement& s) { return bind_single(s, obj); });
        }

        // Returns SQL string with bound parameters inlined for a bulk INSERT (for debugging)
        [[nodiscard]] auto to_sql(std::span<const T> objects) -> std::expected<std::string, Error> {
            if (objects.empty()) {
                return std::string{};
            }
            return to_sql_impl(get_bulk_insert_sql(objects.size()), [&](Statement& s) {
                return bind_bulk(s, objects);
            });
        }

        // Returns SQL string with bound parameters inlined for a bulk INSERT ... RETURNING (for debugging)
        [[nodiscard]] auto to_sql_returning(std::span<const T> objects) -> std::expected<std::string, Error> {
            if (objects.empty()) {
                return std::string{};
            }
            return to_sql_impl(get_bulk_insert_returning_sql(objects.size()), [&](Statement& s) {
                return bind_bulk(s, objects);
            });
        }

        // Batch insert with RETURNING — returns all inserted IDs
        [[nodiscard]] auto // NOSONAR(cpp:S1659)
        execute_returning(std::span<const T> objects, std::optional<InsertOptions> opts = std::nullopt)
                -> std::expected<std::vector<std::int64_t>, Error> {
            if (objects.empty()) {
                return std::vector<std::int64_t>{};
            }

            InsertOptions const options = opts.value_or(InsertOptions{});

            constexpr std::size_t max_allowed          = Base::MAX_DB_VARIABLES / Base::field_count_;
            std::size_t           effective_batch_size = options.batch_size.value_or(max_allowed);
            // Clamp to [1, max_allowed]: a caller-supplied batch_size of 0 would otherwise
            // never advance the chunk loop (offset += 0) — see issue #359.
            effective_batch_size = std::max<std::size_t>(1, std::min(effective_batch_size, max_allowed));

            if (objects.size() <= effective_batch_size) {
                return execute_bulk_returning(objects);
            }
            return execute_chunked_bulk_returning(objects, effective_batch_size);
        }

        // Batch insert operation with optional configuration
        // NOTE: Returns void — use execute_returning() when IDs are needed
        [[nodiscard]] auto execute(std::span<const T> objects, std::optional<InsertOptions> opts = std::nullopt)
                -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Use default options if not provided
            InsertOptions const options = opts.value_or(InsertOptions{});

            // Calculate effective batch size
            constexpr std::size_t max_allowed          = Base::MAX_DB_VARIABLES / Base::field_count_;
            std::size_t           effective_batch_size = options.batch_size.value_or(max_allowed);
            // Clamp to [1, max_allowed]: 0 would never advance the chunk loop (offset += 0,
            // see issue #359); the upper bound caps at the SQLite parameter limit.
            effective_batch_size = std::max<std::size_t>(1, std::min(effective_batch_size, max_allowed));

            // Batch path with custom batch size
            if (objects.size() <= effective_batch_size) {
                // Fits in one bulk SQL
                return execute_bulk(objects);
            }
            // Need chunking with custom batch size
            return execute_chunked_bulk_custom(objects, effective_batch_size);
        }

        // Ultra-optimized single INSERT ... RETURNING <pk>
        [[nodiscard]] __attribute__((hot)) auto execute_single_optimized(const T& obj, bool return_id = true) noexcept
                -> std::expected<std::int64_t, Error> {
            // Both SQLite 3.35+ and PostgreSQL support RETURNING
            auto prepared = prepare_and_bind(insert_returning_sql_string, obj);
            if (!prepared) [[unlikely]] {
                return std::unexpected(prepared.error());
            }
            Statement* stmt = *prepared;

            // Execute — step_raw() avoids std::expected overhead on hot path
            const int rc = stmt->step_raw();
            if (rc == Statement::ROW_AVAILABLE) [[likely]] {
                // RETURNING produced a row — extract the ID
                std::int64_t id = return_id ? stmt->extract_int64(0) : 0;
                stmt->reset();
                return id;
            }
            stmt->reset();
            if (rc == Statement::NO_MORE_ROWS) [[unlikely]] {
                return 0; // No row returned (shouldn't happen with RETURNING)
            }
            return std::unexpected(Error{rc, stmt->get_error_message()});
        }

        // Optimized single INSERT without RETURNING — no ID extraction overhead
        [[nodiscard]] __attribute__((hot)) auto execute_single_void(const T& obj) noexcept
                -> std::expected<void, Error> {
            auto prepared = prepare_and_bind(insert_sql_string, obj);
            if (!prepared) [[unlikely]] {
                return std::unexpected(prepared.error());
            }
            Statement* stmt = *prepared;

            const int rc = stmt->step_raw();
            stmt->reset();
            if (rc == Statement::NO_MORE_ROWS) [[likely]] {
                return {};
            }
            return std::unexpected(Error{rc, stmt->get_error_message()});
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
        [[nodiscard]] __attribute__((hot)) auto execute_bulk(std::span<const T> objects) -> std::expected<void, Error> {
            const auto& sql = get_bulk_insert_sql(objects.size());

            // Use prepare_cached to reuse prepared statements across iterations
            return conn_->prepare_cached(sql).and_then([this, objects](Statement* stmt) -> std::expected<void, Error> {
                return Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                               *stmt, objects, typename Base::field_indices_t()
                )
                        .and_then([stmt]() -> decltype(auto) { return stmt->execute(); });
            });
        }

        // Execute CHUNKED bulk inserts with custom batch size
        // NOTE: Transaction wrapper required - multiple INSERT statements need atomicity
        [[nodiscard]] auto execute_chunked_bulk_custom(std::span<const T> objects, std::size_t custom_bulk_size)
                -> std::expected<void, Error> {
            auto txn = TransactionGuard<ConnType>::begin(conn_);
            if (!txn) {
                return std::unexpected(txn.error());
            }

            // Process in chunks of custom_bulk_size
            for (std::size_t offset = 0; offset < objects.size(); offset += custom_bulk_size) {
                std::size_t chunk_size = std::min(custom_bulk_size, objects.size() - offset);
                auto        chunk      = objects.subspan(offset, chunk_size);

                const auto& sql = get_bulk_insert_sql(chunk.size());

                auto chunk_result = conn_->prepare_cached(sql).and_then(
                        [this, chunk](Statement* stmt) -> std::expected<void, Error> {
                            return Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                                           *stmt, chunk, typename Base::field_indices_t()
                            )
                                    .and_then([stmt]() -> decltype(auto) { return stmt->execute(); });
                        }
                );

                if (!chunk_result) {
                    return std::unexpected(chunk_result.error()); // ~TransactionGuard auto-rollbacks
                }
            }

            return txn->commit();
        }

        // Execute bulk INSERT ... RETURNING — single chunk, returns all IDs
        [[nodiscard]] auto execute_bulk_returning(std::span<const T> objects)
                -> std::expected<std::vector<std::int64_t>, Error> {
            const auto& sql      = get_bulk_insert_returning_sql(objects.size());
            auto        stmt_res = conn_->prepare_cached(sql);
            if (!stmt_res) {
                return std::unexpected(stmt_res.error());
            }
            auto* stmt = *stmt_res;

            auto bind_result = Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                    *stmt, objects, typename Base::field_indices_t()
            );
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            std::vector<std::int64_t> ids;
            ids.reserve(objects.size());

            int rc = 0;
            while ((rc = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                ids.push_back(stmt->extract_int64(0));
            }
            stmt->reset();

            if (rc != Statement::NO_MORE_ROWS) {
                return std::unexpected(Error{rc, stmt->get_error_message()});
            }
            return ids;
        }

        // Execute chunked bulk INSERT ... RETURNING — multiple chunks with transaction
        [[nodiscard]] auto execute_chunked_bulk_returning(std::span<const T> objects, std::size_t custom_bulk_size)
                -> std::expected<std::vector<std::int64_t>, Error> {
            auto txn = TransactionGuard<ConnType>::begin(conn_);
            if (!txn) {
                return std::unexpected(txn.error());
            }

            std::vector<std::int64_t> all_ids;
            all_ids.reserve(objects.size());

            for (std::size_t offset = 0; offset < objects.size(); offset += custom_bulk_size) {
                std::size_t chunk_size = std::min(custom_bulk_size, objects.size() - offset);
                auto        chunk      = objects.subspan(offset, chunk_size);

                auto chunk_result = execute_bulk_returning(chunk);
                if (!chunk_result) {
                    return std::unexpected(chunk_result.error());
                }
                all_ids.insert(all_ids.end(), chunk_result.value().begin(), chunk_result.value().end());
            }

            auto commit_result = txn->commit();
            if (!commit_result) {
                return std::unexpected(commit_result.error());
            }
            return all_ids;
        }

      private:
        // Prepare (L3-cached) the given INSERT SQL and bind the object's non-PK
        // fields. Both single-row execute paths share this so the prepare +
        // bind + error-check sequence lives in one place.
        [[nodiscard]] __attribute__((always_inline)) auto
        prepare_and_bind(const std::string& sql, const T& obj) noexcept -> std::expected<Statement*, Error> {
            auto stmt_result = conn_->prepare_cached(sql);
            if (!stmt_result) [[unlikely]] {
                return std::unexpected(stmt_result.error());
            }
            Statement* stmt        = *stmt_result;
            auto       bind_result = Base::template bind_non_pk_fields_impl<ConnType, Statement>(
                    *stmt, obj, typename Base::field_indices_t()
            );
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }
            return stmt;
        }

        // Shared body of the four to_sql* debug helpers: prepare the given SQL,
        // delegate binding to the caller, then return the expanded SQL string.
        template <typename BindFn>
        [[nodiscard]] auto to_sql_impl(const std::string& sql, BindFn&& bind_fn) -> std::expected<std::string, Error> {
            auto stmt_result = conn_->prepare_cached(sql);
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt        = *stmt_result;
            auto  bind_result = std::forward<BindFn>(bind_fn)(*stmt);
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }
            return stmt->expanded_sql();
        }

        // Binders shared between the single-row and bulk to_sql variants.
        [[nodiscard]] static auto bind_single(Statement& stmt, const T& obj) -> std::expected<void, Error> {
            return Base::template bind_non_pk_fields_impl<ConnType, Statement>(
                    stmt, obj, typename Base::field_indices_t()
            );
        }

        [[nodiscard]] static auto bind_bulk(Statement& stmt, std::span<const T> objects) -> std::expected<void, Error> {
            return Base::template bind_non_pk_objects_bulk_impl<ConnType, Statement>(
                    stmt, objects, typename Base::field_indices_t()
            );
        }

        std::shared_ptr<ConnType> conn_;
    };

} // namespace storm::orm::statements
