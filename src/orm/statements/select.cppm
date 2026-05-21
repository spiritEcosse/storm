module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (build_sql() shared by to_sql/prepare_statement/rows_generator + collapsed
// coroutine step-loop).

#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_statements_select;

import storm_orm_generator;
import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
import storm_orm_utilities;
import storm_orm_where;
import storm_db_concept;

import <coroutine>;
import <expected>;
import <string>;
import <vector>;
import <type_traits>;
import <optional>;
import <memory>;
import <cstdint>;
import <utility>;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::ConstexprString;

    // Statement class for ORM select operations
    template <typename T, storm::db::DatabaseConnection ConnType> class SelectStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Compile-time SQL size calculation for SELECT statement
        static consteval auto calculate_select_sql_size() -> size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT;
            size_t size = 0;
            size += SELECT; // "SELECT "
            size += Base::calculate_field_names_size();
            size += FROM; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT SQL at compile-time using ConstexprString
        static consteval auto build_select_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_select_sql_size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<sql_size> result;

            result.append("SELECT ");
            result.append(Base::build_all_field_names_list());
            result.append(" FROM ");
            result.append(Base::table_name_);

            return result;
        }

        // Pre-computed SELECT SQL generated at compile-time
        static constexpr auto           select_sql_array  = build_select_sql_array();
        static inline const std::string select_sql_string = std::string(select_sql_array);

        // Pre-computed SELECT ... LIMIT 1 SQL for first() fast path
        static inline const std::string select_limit1_sql_string = std::string(select_sql_array) + " LIMIT 1";

        // Pre-computed SELECT ... LIMIT 2 SQL for get() fast path
        static inline const std::string select_limit2_sql_string = std::string(select_sql_array) + " LIMIT 2";

      public:
        // Generate SELECT SQL string (compile-time computed, runtime accessible)
        static auto get_select_sql() -> const std::string& {
            return select_sql_string;
        }

        explicit SelectStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Build SQL string without preparing or binding (for testing/debugging).
        // Shared by to_sql(), prepare_statement() (dynamic path), and rows_generator().
        // Marked always_inline so the hot path retains the same codegen as the
        // previously-inlined builder (see #264 Phase 2 finding on call-overhead).
        [[nodiscard]] __attribute__((always_inline)) static auto build_sql(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> std::string {
            std::string sql = join_wrapper ? join_wrapper->get_complete_sql() : std::string(get_select_sql());
            if (where_expr) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr);
            }
            Base::template append_order_by<ConnType>(sql, order_by_wrapper);
            Base::template append_limit_offset<ConnType>(sql, limit, offset);
            return sql;
        }

        // Returns the SQL that would be executed by select()
        [[nodiscard]] auto
        to_sql(std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
               const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
               const std::optional<int>&               limit            = std::nullopt,
               const std::optional<int>&               offset           = std::nullopt,
               const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt)
                -> std::expected<std::string, Error> {
            std::string sql = build_sql(join_wrapper, where_expr, limit, offset, order_by_wrapper);

            auto stmt_result = conn_->prepare_cached(sql);
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt_ptr = *stmt_result;
            stmt_ptr->reset();
            if (where_expr) {
                auto bind_result = Base::template bind_where_params<Statement, Error>(stmt_ptr, where_expr);
                if (!bind_result) {
                    return std::unexpected(bind_result.error());
                }
            }
            return stmt_ptr->expanded_sql();
        }

        // Returns the SQL that would be executed by first() (with LIMIT 1)
        [[nodiscard]] auto to_sql_first(
                std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) -> std::expected<std::string, Error> {
            std::optional<int> const limit_one = 1;
            return to_sql(std::move(join_wrapper), where_expr, limit_one, offset, order_by_wrapper);
        }

        // Returns the SQL that would be executed by get() (with LIMIT 2)
        [[nodiscard]] auto to_sql_get(
                std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
        ) -> std::expected<std::string, Error> {
            std::optional<int> const limit_two = 2;
            return to_sql(std::move(join_wrapper), where_expr, limit_two, offset, order_by_wrapper);
        }

        // Proxy types returned by factory methods (used by QuerySet one-liner delegations).
        // QueryBase::forward() funnels the five stored fields into any callable, so the
        // derived Query/FirstQuery/GetQuery don't each spell the field list out — that
        // five-line forwarding block used to repeat four times.
        struct QueryBase {
            SelectStatement&                    stmt;
            std::optional<JoinStatementWrapper> join_wrapper;
            orm::where::ExpressionVariantPtr    where_expr;
            std::optional<int>                  limit_value;
            std::optional<int>                  offset_value;
            std::optional<OrderByWrapper>       order_by_wrapper;

            template <typename Fn> __attribute__((always_inline)) auto forward(Fn&& callback) -> decltype(auto) {
                return std::forward<Fn>(callback)(
                        this->join_wrapper,
                        this->where_expr,
                        this->limit_value,
                        this->offset_value,
                        this->order_by_wrapper
                );
            }
        };

        struct Query : QueryBase {
            [[nodiscard]] auto execute() -> std::expected<plf::hive<T>, Error> {
                return this->forward([this](auto&... args) { return this->stmt.execute(args...); });
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->forward([this](auto&... args) { return this->stmt.to_sql(args...); });
            }
            [[nodiscard]] auto sql() -> std::string {
                return this->forward([](auto&... args) { return build_sql(args...); });
            }
        };

        struct FirstQuery : QueryBase {
            bool               fast_path;
            [[nodiscard]] auto execute() -> std::expected<std::optional<T>, Error> {
                if (fast_path) {
                    return this->stmt.execute_one_fast();
                }
                return this->forward([this](auto&... args) { return this->stmt.execute_one(args...); });
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->stmt
                        .to_sql_first(this->join_wrapper, this->where_expr, this->offset_value, this->order_by_wrapper);
            }
            [[nodiscard]] auto sql() -> std::string {
                std::optional<int> const limit_one = 1;
                return build_sql(
                        this->join_wrapper, this->where_expr, limit_one, this->offset_value, this->order_by_wrapper
                );
            }
        };

        struct GetQuery : QueryBase {
            bool               fast_path;
            [[nodiscard]] auto execute() -> std::expected<T, Error> {
                if (fast_path) {
                    return this->stmt.execute_get_fast();
                }
                return this->forward([this](auto&... args) { return this->stmt.execute_get(args...); });
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->stmt
                        .to_sql_get(this->join_wrapper, this->where_expr, this->offset_value, this->order_by_wrapper);
            }
            [[nodiscard]] auto sql() -> std::string {
                std::optional<int> const limit_two = 2;
                return build_sql(
                        this->join_wrapper, this->where_expr, limit_two, this->offset_value, this->order_by_wrapper
                );
            }
        };

        auto
        query(std::optional<JoinStatementWrapper>     jw,
              const orm::where::ExpressionVariantPtr& we,
              const std::optional<int>&               lv,
              const std::optional<int>&               ov,
              const std::optional<OrderByWrapper>&    ob) -> Query {
            return {*this, jw, we, lv, ov, ob};
        }

        // Single templated factory used by both query_first / query_get.
        // The previous wrappers had identical 5-line parameter blocks; this
        // collapses them into one factory parametrised on Proxy.
        template <typename Proxy>
        __attribute__((always_inline)) auto make_first_or_get(
                std::optional<JoinStatementWrapper>     jw,
                const orm::where::ExpressionVariantPtr& we,
                const std::optional<int>&               lv,
                const std::optional<int>&               ov,
                const std::optional<OrderByWrapper>&    ob,
                bool                                    fast
        ) -> Proxy {
            return {{*this, std::move(jw), we, lv, ov, ob}, fast};
        }

        // clang-format off
        auto query_first(std::optional<JoinStatementWrapper> jw, const orm::where::ExpressionVariantPtr& we, const std::optional<int>& lv, const std::optional<int>& ov, const std::optional<OrderByWrapper>& ob, bool fast) -> FirstQuery { return make_first_or_get<FirstQuery>(std::move(jw), we, lv, ov, ob, fast); }
        auto query_get  (std::optional<JoinStatementWrapper> jw, const orm::where::ExpressionVariantPtr& we, const std::optional<int>& lv, const std::optional<int>& ov, const std::optional<OrderByWrapper>& ob, bool fast) -> GetQuery   { return make_first_or_get<GetQuery>  (std::move(jw), we, lv, ov, ob, fast); }
        // clang-format on

        // Drop every cached Statement pointer and dynamic-SQL marker.
        // Call before Connection::clear_statement_cache() — without this the
        // next execute() would step a freed prepared statement. Reached from
        // QuerySet::reset() and QuerySet::invalidate_cache(). Issue #215.
        auto invalidate_cache() noexcept -> void {
            cached_simple_stmt_ = nullptr;
            cached_first_stmt_  = nullptr;
            cached_get_stmt_    = nullptr;
            cached_stmt_        = nullptr;
            cached_where_addr_  = nullptr;
            cached_sql_.clear();
        }

        // Unified SELECT execution - handles all combinations of JOIN and WHERE
        // NOTE: The if/else branch is OUTSIDE the loop intentionally - checking inside would
        // add overhead per row (10k checks vs 1 check). Keep separate execute_query_loop calls.
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute(std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt)
                -> std::expected<plf::hive<T>, Error> {
            auto prepare_result = prepare_statement(join_wrapper, where_expr, limit, offset, order_by_wrapper);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt_ptr = *prepare_result;
            if (join_wrapper) {
                return execute_query_loop(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) {
                    join_wrapper->extract_row(stmt, &obj);
                });
            }
            return execute_query_loop(stmt_ptr, [](Statement* stmt, T& obj) { Base::extract_all_columns(stmt, obj); });
        }

        // Zero-parameter fast path for first() — no checks, no parameter passing overhead
        // Called by QuerySet::first() when no WHERE/JOIN/ORDER BY/OFFSET modifiers are set
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_one_fast()
                -> std::expected<std::optional<T>, Error> {
            if (cached_first_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(select_limit1_sql_string);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_first_stmt_ = *prepare_result;
            }
            return execute_single_row(cached_first_stmt_, [](Statement* stmt, T& obj) {
                Base::extract_all_columns(stmt, obj);
            });
        }

        // first() with modifiers (WHERE/JOIN/ORDER BY/OFFSET) — applies LIMIT 1.
        // The `limit` parameter is accepted for API symmetry with execute()/execute_get()
        // and is intentionally overridden to 1; `[[maybe_unused]]` marks it as such.
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_one(
                std::optional<JoinStatementWrapper>        join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr&    where_expr       = nullptr,
                [[maybe_unused]] const std::optional<int>& limit            = std::nullopt,
                const std::optional<int>&                  offset           = std::nullopt,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt
        ) -> std::expected<std::optional<T>, Error> {
            std::optional<int> const limit_one = 1;
            auto prepare_result = prepare_statement(join_wrapper, where_expr, limit_one, offset, order_by_wrapper);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt_ptr = *prepare_result;
            if (join_wrapper) {
                return execute_single_row(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) {
                    join_wrapper->extract_row(stmt, &obj);
                });
            }
            return execute_single_row(stmt_ptr, [](Statement* stmt, T& obj) { Base::extract_all_columns(stmt, obj); });
        }

        // Zero-parameter fast path for get() — no checks, no parameter passing overhead
        // Called by QuerySet::get() when no WHERE/JOIN/ORDER BY/OFFSET modifiers are set
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_get_fast() -> std::expected<T, Error> {
            if (cached_get_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(select_limit2_sql_string);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_get_stmt_ = *prepare_result;
            }
            return execute_exact_one(cached_get_stmt_, [](Statement* stmt, T& obj) {
                Base::extract_all_columns(stmt, obj);
            });
        }

        // get() with modifiers — applies LIMIT 2 (limit parameter ignored, see execute_one).
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_get(
                std::optional<JoinStatementWrapper>        join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr&    where_expr       = nullptr,
                [[maybe_unused]] const std::optional<int>& limit            = std::nullopt,
                const std::optional<int>&                  offset           = std::nullopt,
                const std::optional<OrderByWrapper>&       order_by_wrapper = std::nullopt
        ) -> std::expected<T, Error> {
            std::optional<int> const limit_two = 2;
            auto prepare_result = prepare_statement(join_wrapper, where_expr, limit_two, offset, order_by_wrapper);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt_ptr = *prepare_result;
            if (join_wrapper) {
                return execute_exact_one(stmt_ptr, [&join_wrapper](Statement* stmt, T& obj) {
                    join_wrapper->extract_row(stmt, &obj);
                });
            }
            return execute_exact_one(stmt_ptr, [](Statement* stmt, T& obj) { Base::extract_all_columns(stmt, obj); });
        }

        // Lazy row-by-row iteration via coroutine — yields std::expected<T, Error> per row
        // Uses a DEDICATED (non-cached) statement to avoid conflicts with cached queries
        // The statement lives in the coroutine frame and is finalized on generator destruction
        auto rows_generator(
                std::shared_ptr<ConnType>           conn,
                std::optional<JoinStatementWrapper> join_wrapper     = std::nullopt,
                orm::where::ExpressionVariantPtr    where_expr       = nullptr,
                std::optional<int>                  limit            = std::nullopt,
                std::optional<int>                  offset           = std::nullopt,
                std::optional<OrderByWrapper>       order_by_wrapper = std::nullopt
        ) -> storm::generator<std::expected<T, Error>&&> {
            std::string sql = build_sql(join_wrapper, where_expr, limit, offset, order_by_wrapper);

            auto stmt_result = conn->prepare(sql);
            if (!stmt_result) {
                co_yield std::unexpected(stmt_result.error());
                co_return;
            }
            auto& stmt = stmt_result.value();

            if (where_expr) {
                auto bind_result = Base::template bind_where_params<Statement, Error>(&stmt, where_expr);
                if (!bind_result) {
                    co_yield std::unexpected(bind_result.error());
                    co_return;
                }
            }

            // Single step/yield loop — extraction differs only in one line between
            // the join and non-join cases. co_yield must stay in the coroutine
            // body (not in a lambda), so we branch only on the per-row extract.
            int step_result = 0;
            while ((step_result = stmt.step_raw()) == Statement::ROW_AVAILABLE) {
                T obj;
                if (join_wrapper) {
                    join_wrapper->extract_row(&stmt, &obj);
                } else {
                    Base::extract_all_columns(&stmt, obj);
                }
                co_yield std::move(obj);
            }
            if (step_result != Statement::NO_MORE_ROWS) {
                co_yield std::unexpected(Error{step_result, stmt.get_error_message()});
            }
        }

      private:
        // =====================================================================
        // STATEMENT PREPARATION - Unified caching for all query types
        // =====================================================================

        // Prepare and cache statement based on query type
        // Unified approach: simple SELECT uses dedicated cache, all others use string-based cache
        // OPTIMIZATION: WHERE expression address caching - skips SQL building AND param binding
        // when the same expression is used repeatedly (sqlite3_reset preserves bindings)
        // Bind WHERE parameters to cached_stmt_ and return any binding error in the
        // shape callers expect. Shared between rebind_where_only (PG fast path) and
        // prepare_and_bind (dynamic path).
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_where_or_propagate(const orm::where::ExpressionVariantPtr& where_expr) -> std::expected<void, Error> {
            auto bind_result = Base::template bind_where_params<Statement, Error>(cached_stmt_, where_expr);
            if (!bind_result) [[unlikely]] {
                return std::unexpected(bind_result.error());
            }
            return {};
        }

        // Fast path: simple SELECT (no WHERE / JOIN / modifiers). Prepares the static
        // SQL once and caches the Statement* on the first call.
        [[nodiscard]] __attribute__((always_inline)) auto prepare_simple_path() -> std::expected<Statement*, Error> {
            if (cached_simple_stmt_ == nullptr) {
                auto prepare_result = conn_->prepare_cached(get_select_sql());
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_simple_stmt_ = *prepare_result;
            }
            return cached_simple_stmt_;
        }

        // Fast path: same WHERE expression as the last call — skip SQL building.
        // Rebinds parameters only if the backend clears bindings on reset (PostgreSQL).
        [[nodiscard]] __attribute__((always_inline)) auto
        rebind_where_only(const orm::where::ExpressionVariantPtr& where_expr) -> std::expected<Statement*, Error> {
            if constexpr (!Statement::preserves_bindings) { // LCOV_EXCL_START — if constexpr: only instantiated for
                                                            // PostgreSQL; fast-path return below untested for
                                                            // execute_one
                if (auto r = bind_where_or_propagate(where_expr); !r) {
                    return std::unexpected(r.error());
                }
            }
            return cached_stmt_;
        } // LCOV_EXCL_STOP

        // Dynamic path: prepare-or-reuse statement keyed by SQL string, then bind WHERE
        // params if any. Always_inline so the call site keeps the same codegen as the
        // previously-inlined builder (see #264 Phase 2 finding).
        [[nodiscard]] __attribute__((always_inline)) auto
        prepare_and_bind(std::string sql, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<Statement*, Error> {
            if (cached_stmt_ == nullptr || cached_sql_ != sql) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(Error{-1, "Failed to prepare statement"});
                }
                cached_stmt_ = *prepare_result;
                cached_sql_  = std::move(sql);
            }
            if (where_expr) {
                if (auto r = bind_where_or_propagate(where_expr); !r) {
                    return std::unexpected(r.error());
                }
            }
            return cached_stmt_;
        }

        // Each of the three stage-selectors lives in its own consteval-friendly
        // inline helper. Pulling the boolean expressions out of prepare_statement
        // keeps its cyclomatic complexity to one branch per dispatch arm.
        [[nodiscard]] __attribute__((always_inline)) static auto is_simple_select(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> bool {
            const bool has_modifiers = order_by_wrapper.has_value() || limit.has_value() || offset.has_value();
            const bool has_filters   = where_expr || join_wrapper.has_value();
            return !has_filters && !has_modifiers;
        }

        [[nodiscard]] __attribute__((always_inline)) auto can_use_addr_fast_path(const void* where_addr) const -> bool {
            const bool have_cache = cached_stmt_ != nullptr && cached_where_addr_ != nullptr;
            return have_cache && where_addr == cached_where_addr_;
        }

        [[nodiscard]] __attribute__((always_inline)) auto prepare_statement(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> std::expected<Statement*, Error> {
            if (is_simple_select(join_wrapper, where_expr, limit, offset, order_by_wrapper)) {
                return prepare_simple_path();
            }
            const void* where_addr = where_expr ? static_cast<const void*>(&(*where_expr)) : nullptr;
            if (can_use_addr_fast_path(where_addr)) {
                return rebind_where_only(where_expr);
            }
            // NOTE: Do NOT add sql.reserve() here - benchmarks show ~2% regression due to
            // extra function call overhead outweighing reallocation savings for typical SQL sizes
            auto stmt =
                    prepare_and_bind(build_sql(join_wrapper, where_expr, limit, offset, order_by_wrapper), where_expr);
            if (stmt) {
                cached_where_addr_ = where_addr;
            }
            return stmt;
        }

        // =====================================================================
        // QUERY LOOPS
        // =====================================================================

        // Unified query loop template - handles both direct and JOIN extraction
        // DATABASE-AGNOSTIC: Uses Statement methods with templates for cross-module inlining
        // Extractor signature: void(Statement* stmt, T& obj)
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto
        execute_query_loop(Statement* stmt, const Extractor& extractor) noexcept -> std::expected<plf::hive<T>, Error> {
            plf::hive<T> results;
            int          step_result = 0;

            // DATABASE-AGNOSTIC: Template step_raw() enables cross-module inlining
            while ((step_result = stmt->step_raw()) == Statement::ROW_AVAILABLE) {
                T obj;
                extractor(stmt, obj);
                results.insert(std::move(obj));
            }

            if (step_result != Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
            }

            stmt->reset();
            return results;
        }

        // =====================================================================
        // SINGLE-ROW QUERY LOOPS
        // =====================================================================

        // Step once and classify the result.
        //   true  → ROW_AVAILABLE (caller extracts)
        //   false → NO_MORE_ROWS (caller decides: nullopt vs "no row found" error)
        //   error → driver/binding error (statement already reset)
        // Shared by execute_single_row and execute_exact_one — both used to spell out
        // the same NO_MORE_ROWS / ROW_AVAILABLE / error triage inline.
        [[nodiscard]] __attribute__((always_inline)) static auto step_first_row(Statement* stmt) noexcept
                -> std::expected<bool, Error> {
            int const step_result = stmt->step_raw();
            if (step_result == Statement::ROW_AVAILABLE) {
                return true;
            }
            if (step_result == Statement::NO_MORE_ROWS) {
                stmt->reset();
                return false;
            }
            stmt->reset();
            return std::unexpected(Error{step_result, stmt->get_error_message()});
        }

        // Single-row query execution - fetches at most one row, returns optional
        // DATABASE-AGNOSTIC: Uses Statement methods with templates for cross-module inlining
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) auto execute_single_row(Statement* stmt, const Extractor& extractor) noexcept
                -> std::expected<std::optional<T>, Error> {
            auto step = step_first_row(stmt);
            if (!step) [[unlikely]] {
                return std::unexpected(step.error());
            }
            if (!*step) {
                return std::optional<T>{std::nullopt};
            }

            T obj;
            extractor(stmt, obj);
            stmt->reset();

            return std::optional<T>{std::move(obj)};
        }

        // Exact-one query execution for get() - returns T or error on 0/>1 rows
        // Avoids plf::hive allocation — just steps up to 2 rows inline
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) auto execute_exact_one(Statement* stmt, const Extractor& extractor) noexcept
                -> std::expected<T, Error> {
            auto step = step_first_row(stmt);
            if (!step) [[unlikely]] {
                return std::unexpected(step.error());
            }
            if (!*step) {
                return std::unexpected(Error{-1, "No row found matching query"});
            }

            T obj;
            extractor(stmt, obj);

            // Check for second row (LIMIT 2 ensures at most 2 steps)
            int const second_step = stmt->step_raw();
            stmt->reset();

            if (second_step == Statement::ROW_AVAILABLE) [[unlikely]] {
                return std::unexpected(Error{-1, "Multiple rows found matching query"});
            }

            return std::move(obj);
        }

        // SQL clause helpers are inherited from BaseStatement (append_order_by, append_limit_offset, bind_where_params)

        std::shared_ptr<ConnType> conn_;

        // Cache for simple SELECT (static SQL, compile-time constant)
        mutable Statement* cached_simple_stmt_ = nullptr;

        // Cache for first() fast path (SELECT ... LIMIT 1, pre-built static SQL)
        mutable Statement* cached_first_stmt_ = nullptr;

        // Cache for get() fast path (SELECT ... LIMIT 2, pre-built static SQL)
        mutable Statement* cached_get_stmt_ = nullptr;

        // Cache for dynamic queries (WHERE, JOIN, modifiers) - validated by SQL comparison
        mutable Statement*  cached_stmt_       = nullptr;
        mutable const void* cached_where_addr_ = nullptr; // WHERE expression address cache
        mutable std::string cached_sql_;
    };

} // namespace storm::orm::statements