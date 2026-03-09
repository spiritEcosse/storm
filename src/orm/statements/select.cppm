module;

#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_statements_select;

import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
import storm_orm_utilities;
import storm_orm_where;
import storm_db_concept;

import <expected>;
import <string>;
import <vector>;
import <type_traits>;
import <optional>;
import <memory>;
import <cstdint>;

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

        // Generate SELECT SQL string (compile-time computed, runtime accessible)
        static auto get_select_sql() -> const std::string& {
            return select_sql_string;
        }

      public:
        explicit SelectStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Returns the SQL that would be executed by select()
        [[nodiscard]] auto
        to_sql(std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
               const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
               const std::optional<int>&               limit            = std::nullopt,
               const std::optional<int>&               offset           = std::nullopt,
               const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt)
                -> std::expected<std::string, Error> {
            std::string sql = join_wrapper ? join_wrapper->get_complete_sql() : std::string(get_select_sql());
            if (where_expr) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr);
            }
            Base::template append_order_by<ConnType>(sql, order_by_wrapper);
            Base::template append_limit_offset<ConnType>(sql, limit, offset);

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

        // Proxy types returned by factory methods (used by QuerySet one-liner delegations)
        struct QueryBase {
            SelectStatement&                    stmt;
            std::optional<JoinStatementWrapper> join_wrapper;
            orm::where::ExpressionVariantPtr    where_expr;
            std::optional<int>                  limit_value;
            std::optional<int>                  offset_value;
            std::optional<OrderByWrapper>       order_by_wrapper;
        };

        struct Query : QueryBase {
            [[nodiscard]] auto execute() -> std::expected<plf::hive<T>, Error> {
                return this->stmt.execute(
                        this->join_wrapper,
                        this->where_expr,
                        this->limit_value,
                        this->offset_value,
                        this->order_by_wrapper
                );
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->stmt
                        .to_sql(this->join_wrapper,
                                this->where_expr,
                                this->limit_value,
                                this->offset_value,
                                this->order_by_wrapper);
            }
        };

        struct FirstQuery : QueryBase {
            bool               fast_path;
            [[nodiscard]] auto execute() -> std::expected<std::optional<T>, Error> {
                if (fast_path) {
                    return this->stmt.execute_one_fast();
                }
                return this->stmt.execute_one(
                        this->join_wrapper,
                        this->where_expr,
                        this->limit_value,
                        this->offset_value,
                        this->order_by_wrapper
                );
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->stmt
                        .to_sql_first(this->join_wrapper, this->where_expr, this->offset_value, this->order_by_wrapper);
            }
        };

        struct GetQuery : QueryBase {
            bool               fast_path;
            [[nodiscard]] auto execute() -> std::expected<T, Error> {
                if (fast_path) {
                    return this->stmt.execute_get_fast();
                }
                return this->stmt.execute_get(
                        this->join_wrapper,
                        this->where_expr,
                        this->limit_value,
                        this->offset_value,
                        this->order_by_wrapper
                );
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return this->stmt
                        .to_sql_get(this->join_wrapper, this->where_expr, this->offset_value, this->order_by_wrapper);
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

        auto query_first(
                std::optional<JoinStatementWrapper>     jw,
                const orm::where::ExpressionVariantPtr& we,
                const std::optional<int>&               lv,
                const std::optional<int>&               ov,
                const std::optional<OrderByWrapper>&    ob,
                bool                                    fast
        ) -> FirstQuery {
            return {*this, jw, we, lv, ov, ob, fast};
        }

        auto query_get(
                std::optional<JoinStatementWrapper>     jw,
                const orm::where::ExpressionVariantPtr& we,
                const std::optional<int>&               lv,
                const std::optional<int>&               ov,
                const std::optional<OrderByWrapper>&    ob,
                bool                                    fast
        ) -> GetQuery {
            return {*this, jw, we, lv, ov, ob, fast};
        }

        // Invalidate dynamic query cache
        // Call this when QuerySet::reset() is invoked to ensure fresh query on next execute
        auto invalidate_cache() noexcept -> void {
            cached_stmt_       = nullptr;
            cached_first_stmt_ = nullptr;
            cached_get_stmt_   = nullptr;
            cached_where_addr_ = nullptr;
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

        // first() with modifiers (WHERE/JOIN/ORDER BY/OFFSET) — applies LIMIT 1
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_one(
                std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
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
            // LCOV_EXCL_START — tested via first() tests; C++26 module coverage gap
            return execute_single_row(stmt_ptr, [](Statement* stmt, T& obj) { Base::extract_all_columns(stmt, obj); });
            // LCOV_EXCL_STOP
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

        // get() with modifiers (WHERE/JOIN/ORDER BY/OFFSET) — applies LIMIT 2
        [[nodiscard]] __attribute__((hot)) __attribute__((flatten)) auto execute_get(
                std::optional<JoinStatementWrapper>     join_wrapper     = std::nullopt,
                const orm::where::ExpressionVariantPtr& where_expr       = nullptr,
                const std::optional<int>&               limit            = std::nullopt,
                const std::optional<int>&               offset           = std::nullopt,
                const std::optional<OrderByWrapper>&    order_by_wrapper = std::nullopt
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

      private:
        // =====================================================================
        // STATEMENT PREPARATION - Unified caching for all query types
        // =====================================================================

        // Prepare and cache statement based on query type
        // Unified approach: simple SELECT uses dedicated cache, all others use string-based cache
        // OPTIMIZATION: WHERE expression address caching - skips SQL building AND param binding
        // when the same expression is used repeatedly (sqlite3_reset preserves bindings)
        [[nodiscard]] __attribute__((always_inline)) auto prepare_statement(
                const std::optional<JoinStatementWrapper>& join_wrapper,
                const orm::where::ExpressionVariantPtr&    where_expr,
                const std::optional<int>&                  limit,
                const std::optional<int>&                  offset,
                const std::optional<OrderByWrapper>&       order_by_wrapper
        ) -> std::expected<Statement*, Error> {
            const bool has_modifiers = order_by_wrapper.has_value() || limit.has_value() || offset.has_value();

            // Fast path: simple SELECT (static SQL, no building needed)
            if (!where_expr && !join_wrapper && !has_modifiers) {
                if (cached_simple_stmt_ == nullptr) {
                    auto prepare_result = conn_->prepare_cached(get_select_sql());
                    if (!prepare_result) [[unlikely]] {
                        return std::unexpected(prepare_result.error());
                    }
                    cached_simple_stmt_ = *prepare_result;
                }
                return cached_simple_stmt_;
            }

            // Fast path: WHERE expression address unchanged — skip SQL building entirely
            const void* where_addr = where_expr ? static_cast<const void*>(&(*where_expr)) : nullptr;
            if (cached_stmt_ != nullptr && where_addr == cached_where_addr_ && where_addr != nullptr) {
                // Rebind only if backend clears params on reset (PostgreSQL does, SQLite doesn't)
                if constexpr (!Statement::preserves_bindings) { // LCOV_EXCL_START — if constexpr: only instantiated for
                                                                // PostgreSQL; fast-path return below untested for
                                                                // execute_one
                    auto bind_result = Base::template bind_where_params<Statement, Error>(cached_stmt_, where_expr);
                    if (!bind_result) [[unlikely]] {
                        return std::unexpected(bind_result.error());
                    }
                }
                return cached_stmt_;
            } // LCOV_EXCL_STOP

            // Dynamic path: build SQL and cache by string comparison
            // NOTE: Do NOT add sql.reserve() here - benchmarks show ~2% regression due to
            // extra function call overhead outweighing reallocation savings for typical SQL sizes
            std::string sql = join_wrapper ? join_wrapper->get_complete_sql() : std::string(get_select_sql());
            if (where_expr) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr);
            }
            Base::template append_order_by<ConnType>(sql, order_by_wrapper);
            Base::template append_limit_offset<ConnType>(sql, limit, offset);

            // Get or prepare cached statement
            if (cached_stmt_ == nullptr || cached_sql_ != sql) {
                auto prepare_result = conn_->prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(Error{-1, "Failed to prepare statement"});
                }
                cached_stmt_ = *prepare_result;
                cached_sql_  = std::move(sql);
            }

            // Bind WHERE params if needed
            if (where_expr) {
                auto bind_result = Base::template bind_where_params<Statement, Error>(cached_stmt_, where_expr);
                if (!bind_result) [[unlikely]] {
                    return std::unexpected(bind_result.error());
                }
            }

            // Cache the WHERE expression address for fast-path on next call
            cached_where_addr_ = where_addr;

            return cached_stmt_;
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

        // Single-row query execution - fetches at most one row, returns optional
        // DATABASE-AGNOSTIC: Uses Statement methods with templates for cross-module inlining
        template <typename Extractor>
        [[nodiscard]] __attribute__((hot)) auto execute_single_row(Statement* stmt, const Extractor& extractor) noexcept
                -> std::expected<std::optional<T>, Error> {
            int const step_result = stmt->step_raw();

            // LCOV_EXCL_START — tested via first_empty/first_where_no_match; C++26 module coverage gap
            if (step_result == Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::optional<T>{std::nullopt};
            }
            // LCOV_EXCL_STOP

            if (step_result != Statement::ROW_AVAILABLE) [[unlikely]] {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
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
            int const step_result = stmt->step_raw();

            if (step_result == Statement::NO_MORE_ROWS) {
                stmt->reset();
                return std::unexpected(Error{-1, "No row found matching query"});
            }

            if (step_result != Statement::ROW_AVAILABLE) [[unlikely]] {
                stmt->reset();
                return std::unexpected(Error{step_result, stmt->get_error_message()});
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