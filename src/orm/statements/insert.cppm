module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (shared consteval SQL builder + to_sql_impl).

#include <meta>

export module storm_orm_statements_insert;

import std;

import storm_orm_statements_base;
import storm_orm_statements_field_names;
import storm_orm_statements_upsert_grammar;
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

    // A composite PK is never DB-generated (#502): AUTOINCREMENT and
    // GENERATED ... AS IDENTITY are single-integer-column features, so every key
    // part is caller-supplied and RETURNING could only echo the input back. Plain
    // insert() therefore defaults to the void/no-RETURNING path on a composite
    // model, and keeps returning the generated id on a single-PK model.
    template <typename T> consteval auto default_return_id() -> ReturnId {
        return BaseStatement<T>::has_composite_pk_ ? ReturnId::No : ReturnId::Yes;
    }

    // An explicit ReturnId::Yes on a composite model is rejected at the call
    // site: unconstrained it would emit "RETURNING <first part>" — silently
    // lossy. ReturnId::No stays valid on every model shape, so generic code that
    // spells it out keeps compiling. (Named concept per the #472/#477/#478
    // precedent; the #413 auto-DEFAULT caveat was checked and does not fire —
    // that DEFAULT is recovered from a C++ default-member-initializer, a value
    // already in the caller's object.)
    template <typename T, ReturnId R>
    concept ReturnIdSupported = (R == ReturnId::No) || !BaseStatement<T>::has_composite_pk_;

    template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement;

    // Upsert proxy chain (#205): lifted OUT of InsertStatement so their sql()/execute()/to_sql()
    // methods don't count toward InsertStatement's method total (cpp:S1448 — mirrors the
    // UpdateStatement proxy split in #438). on_conflict<Target...>() returns a ConflictTarget,
    // whose update<SetCols...>()/nothing() are the two terminal upsert proxies.
    //
    // ConflictTargetTag<Target...> carries the conflict-target columns as a TYPE (not a pack)
    // so UpdateUpsertQuery can be a class template with exactly one variadic NTTP pack
    // (SetCols...). A class template cannot declare two `std::meta::info...` packs — both
    // would need independent deduction/specification, which the language does not support —
    // so Target... must be wrapped before it reaches a second pack's scope.
    template <std::meta::info... Target> struct ConflictTargetTag {};

    namespace detail {

        // DO UPDATE terminal proxy. TargetTag is a ConflictTargetTag<Target...> instantiation.
        template <typename T, storm::db::DatabaseConnection ConnType, typename TargetTag, std::meta::info... SetCols>
        struct UpdateUpsertQuery;

        template <
                typename T,
                storm::db::DatabaseConnection ConnType,
                std::meta::info... Target,
                std::meta::info... SetCols>
        struct UpdateUpsertQuery<T, ConnType, ConflictTargetTag<Target...>, SetCols...> {
            using Error = typename ConnType::Error;
            InsertStatement<T, ConnType> stmt;
            const T&                     obj;
            [[nodiscard]] auto           execute() -> std::expected<std::int64_t, Error> {
                return stmt.template upsert_update_runner<Target...>().template run<SetCols...>(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                const std::string sql = UpsertGrammar<T>::template update_sql<Target...>(
                        UpsertGrammar<T>::template build_excluded_set_clause<SetCols...>()
                );
                return stmt.template to_sql_with<SetCols...>(sql, obj);
            }
            [[nodiscard]] auto sql() -> std::string {
                return UpsertGrammar<T>::template update_sql<Target...>(
                        UpsertGrammar<T>::template build_excluded_set_clause<SetCols...>()
                );
            }
        };

        // DO NOTHING terminal proxy.
        template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... Target>
        struct NothingUpsertQuery {
            using Error = typename ConnType::Error;
            InsertStatement<T, ConnType> stmt;
            const T&                     obj;
            [[nodiscard]] auto execute() -> std::expected<std::optional<std::int64_t>, Error> { // NOSONAR(cpp:S1659)
                return stmt.template execute_upsert_nothing<Target...>(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.template to_sql_with<>(UpsertGrammar<T>::template nothing_sql<Target...>(), obj);
            }
            [[nodiscard]] auto sql() -> std::string {
                return UpsertGrammar<T>::template nothing_sql<Target...>();
            }
        };

        // Intermediate: carries the conflict target, offers update<>/nothing().
        template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... Target> struct ConflictTarget {
            InsertStatement<T, ConnType> stmt;
            const T&                     obj;
            template <std::meta::info... SetCols>
                requires UpsertSettable<T, SetCols...>
            [[nodiscard]] auto update() -> UpdateUpsertQuery<T, ConnType, ConflictTargetTag<Target...>, SetCols...> {
                return {std::move(stmt), obj};
            }
            [[nodiscard]] auto nothing() -> NothingUpsertQuery<T, ConnType, Target...> {
                return {std::move(stmt), obj};
            }
        };

        // Shared upsert entry point (#205) for the single-row query proxies (SingleQuery/
        // VoidQuery): INSERT ... ON CONFLICT (Target...) DO {UPDATE|NOTHING}. CRTP base so
        // both proxies offer on_conflict() without duplicating the method body — the derived
        // proxy supplies `stmt`/`obj` via Derived's own members.
        template <typename T, storm::db::DatabaseConnection ConnType, typename Derived> struct OnConflictMixin {
            template <std::meta::info... Target>
                requires ConflictTargetUnique<T, Target...>
            [[nodiscard]] auto on_conflict() -> ConflictTarget<T, ConnType, Target...> {
                auto* self = static_cast<Derived*>(this);
                return {std::move(self->stmt), self->obj};
            }
        };

    } // namespace detail

    // Statement class for ORM insert operations
    template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Grammar    = UpsertGrammar<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-computed placeholders (excludes PK for auto-increment). Shared with
        // UpsertGrammar via FieldNameGrammar<Base>::build_placeholders() (#205).
        static constexpr auto placeholders_ = FieldNameGrammar<Base>::build_placeholders();

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
            size += FieldNameGrammar<Base>::calculate_field_names_size();

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
            result.append(FieldNameGrammar<Base>::build_non_pk_field_names_list());
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
            size += FieldNameGrammar<Base>::calculate_field_names_size();
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
            result.append(FieldNameGrammar<Base>::build_non_pk_field_names_list());
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

        struct SingleQuery : detail::OnConflictMixin<T, ConnType, SingleQuery> {
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

        struct VoidQuery : detail::OnConflictMixin<T, ConnType, VoidQuery> {
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

        template <ReturnId R = default_return_id<T>()>
            requires ReturnIdSupported<T, R>
        [[nodiscard]] auto query(const T& obj [[clang::lifetimebound]]) {
            if constexpr (R == ReturnId::Yes) {
                return SingleQuery{{}, std::move(*this), obj};
            } else {
                return VoidQuery{{}, std::move(*this), obj};
            }
        }
        // LIFETIME CONTRACT (issue #357, finding B): the returned BulkQuery holds a
        // std::span<const T> aliasing the caller's container. `[[clang::lifetimebound]]`
        // (+ -Werror=dangling) catches the inline-temporary case at compile time, but a
        // span built from a container that dies before .execute()/.to_sql() runs still
        // dangles silently at runtime. Treat the proxy as single-expression-use: keep the
        // backing container alive until the terminal call completes. Same for the
        // ReturnId-templated overload below.
        [[nodiscard]] auto
        query(std::span<const T> objects [[clang::lifetimebound]], std::optional<InsertOptions> opts = std::nullopt)
                -> BulkQuery {
            return {std::move(*this), objects, opts};
        }
        template <ReturnId R>
            requires ReturnIdSupported<T, R>
        [[nodiscard]] auto
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

        // Returns SQL string with bound parameters inlined for an upsert (DO UPDATE/DO NOTHING)
        // statement (for debugging). Generalizes to_sql_impl to bind the non-PK VALUES fields
        // plus the auto_update now() tail (#209) for any auto_update column not in SetCols —
        // empty SetCols (the DO NOTHING proxy) binds just the non-PK fields.
        template <std::meta::info... SetCols>
        [[nodiscard]] auto to_sql_with(const std::string& sql, const T& obj) -> std::expected<std::string, Error> {
            return to_sql_impl(sql, [&](Statement& s) -> std::expected<void, Error> {
                if (auto bound = bind_single(s, obj); !bound) {
                    return std::unexpected(bound.error());
                }
                return bind_upsert_auto_updates<SetCols...>(&s);
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

        // The number of VALUES placeholders in a plain INSERT: every field except a
        // DB-generated single-column PK; a composite key is caller data, so all
        // fields (#502). Used by the DO UPDATE upsert path to find where the
        // auto_update now() tail starts binding (right after the VALUES params).
        static consteval auto placeholders_count() -> std::size_t {
            return Base::has_composite_pk_ ? Base::field_count_ : Base::field_count_ - 1;
        }

        // Upsert DO NOTHING — RETURNING yields the new id, or no row when skipped.
        template <std::meta::info... Target>
        [[nodiscard]] auto execute_upsert_nothing(const T& obj) -> std::expected<std::optional<std::int64_t>, Error> {
            const std::string& sql      = Grammar::template nothing_sql<Target...>();
            auto               prepared = prepare_and_bind(sql, obj);
            if (!prepared) {
                return std::unexpected(prepared.error());
            }
            Statement*                  stmt = *prepared;
            const int                   rc   = stmt->step_raw();
            std::optional<std::int64_t> out;
            if (rc == Statement::ROW_AVAILABLE) {
                out = stmt->extract_int64(0);
            }
            stmt->reset();
            if (rc == Statement::ROW_AVAILABLE || rc == Statement::NO_MORE_ROWS) {
                return out; // value() set when a row came back, nullopt when skipped
            }
            return std::unexpected(Error{rc, stmt->get_error_message()});
        }

        // Upsert DO UPDATE — always touches a row, so RETURNING always yields the id.
        // The SET clause is "col=excluded.col, ..." for SetCols, plus an auto_update
        // tail (#209) for any auto_update column not already listed.
        //
        // NOTE (#205, found while wiring the on_conflict() proxy in Task 5): a SINGLE
        // function template cannot take two variadic NTTP packs — `template <info...
        // Target, info... SetCols>` compiles, but ALL explicitly-supplied arguments bind
        // to the first pack and the second is silently always empty (verified: GCC/Clang
        // both do this — no diagnostic). The original two-pack signature here was never
        // caught because Task 4 shipped it with zero call sites. Splitting into Target
        // (this function's pack) + SetCols (the nested run<>() pack) mirrors the proven
        // ConflictTarget<Target...>::update<SetCols...>() shape and resolves both packs
        // unambiguously.
        template <std::meta::info... Target> struct UpsertUpdateRunner {
            InsertStatement* self;
            template <std::meta::info... SetCols>
            [[nodiscard]] auto run(const T& obj) -> std::expected<std::int64_t, Error> {
                const std::string sql = Grammar::template update_sql<Target...>(
                        Grammar::template build_excluded_set_clause<SetCols...>()
                );
                auto stmt_result = self->conn_->prepare_cached(sql);
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                Statement* stmt = *stmt_result;
                // (1) bind VALUES params (non-PK fields, same as plain INSERT).
                if (auto bind_result = self->bind_all_fields(*stmt, obj); !bind_result) {
                    return std::unexpected(bind_result.error());
                }
                // (2) bind the trailing auto_update now() params (excluded.col targets bind nothing).
                if (auto bind_result = self->template bind_upsert_auto_updates<SetCols...>(stmt); !bind_result) {
                    return std::unexpected(bind_result.error());
                }
                const int rc = stmt->step_raw();
                if (rc == Statement::ROW_AVAILABLE) {
                    std::int64_t id = stmt->extract_int64(0);
                    stmt->reset();
                    return id;
                }
                stmt->reset();
                return std::unexpected(Error{rc, stmt->get_error_message()});
            }
        };

        // Entry point: fixes Target... via UpsertUpdateRunner; callers then supply
        // SetCols... via run<>(). Used by the detail::UpdateUpsertQuery proxy.
        template <std::meta::info... Target>
        [[nodiscard]] auto upsert_update_runner() -> UpsertUpdateRunner<Target...> {
            return {this};
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
        // Bind now() for each unlisted auto_update column (#209), in declaration order,
        // starting right after the VALUES placeholders (excluded.col SET targets need
        // no params; only the trailing "col=?" auto_update tail does). Mirrors
        // UpdateStatement::bind_unlisted_auto_updates (update.cppm).
        template <std::meta::info... SetCols>
        [[nodiscard]] auto bind_upsert_auto_updates(Statement* stmt) noexcept -> std::expected<void, Error> {
            int        param_index = static_cast<int>(placeholders_count()) + 1; // 1-based, after VALUES
            const auto now         = Base::batch_now();
            return bind_upsert_auto_updates_impl<SetCols...>(stmt, param_index, now, typename Base::field_indices_t{});
        }

        template <std::meta::info... SetCols, std::size_t... Is>
        [[nodiscard]] auto bind_upsert_auto_updates_impl(
                Statement*                            stmt,
                int&                                  param_index,
                std::chrono::system_clock::time_point now,
                std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, Error> {
            std::expected<void, Error> result{};
            (
                    [&] {
                        if constexpr (Grammar::template is_unlisted_auto_update<SetCols...>(Base::all_members_[Is])) {
                            if (result.has_value()) {
                                result = Base::template bind_one<ConnType>(stmt, param_index, now);
                            }
                        }
                    }(),
                    ...
            );
            return result;
        }

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
