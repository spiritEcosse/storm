module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (prepare_stmt + reset_bind_execute helpers; QueryBase::sql() shared by
// SingleQuery/BulkQuery proxies).

#include <meta>

export module storm_orm_statements_update;

import std;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_orm_transaction;
import storm_orm_where;
import storm_db_concept;

export namespace storm::orm::statements {

    // Import utilities for code convenience
    using storm::orm::utilities::ConstexprString;
    using storm::orm::utilities::TransactionGuard;

    // Statement class for ORM update operations
    template <typename T, storm::db::DatabaseConnection ConnType> class UpdateStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Helper to get non-primary-key field count
        static consteval auto get_updatable_field_count() -> std::size_t {
            std::size_t count = 0;
            for (const auto& member : Base::all_members_) {
                if (member != Base::primary_key_) {
                    ++count;
                }
            }
            return count;
        }

        static constexpr std::size_t updatable_field_count_ = get_updatable_field_count();

        // Helper to build field assignments string for UPDATE SQL
        static consteval auto build_field_assignments() {
            // Get all members directly
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
            auto pk      = Base::primary_key_;

            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;

            for (const auto& member : members) {
                if (member != pk) {
                    if (!first) {
                        result.append(", ");
                    }
                    // Check if this is a FK field
                    auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                    if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                        // FK field: use field_name_id
                        result.append(std::meta::identifier_of(member));
                        result.append("_id=?");
                    } else {
                        result.append(std::meta::identifier_of(member));
                        result.append("=?");
                    }
                    first = false;
                }
            }

            return result;
        }

        static constexpr auto field_assignments_ = build_field_assignments();

        // --- Conditional bulk UPDATE (#403): SET-clause built from explicit member NTTPs ---

        // Index of a member info within Base::all_members_ (compile-time).
        static consteval auto index_of_member(std::meta::info member) -> std::size_t {
            for (std::size_t i = 0; i < Base::all_members_.size(); ++i) {
                if (Base::all_members_[i] == member) {
                    return i;
                }
            }
            std::unreachable(); // guarded by is_settable_member() at the call site
        }

        // Each SET target must be a non-static data member of T and not the primary key.
        template <std::meta::info Member> static consteval auto is_settable_member() -> bool {
            return std::meta::is_nonstatic_data_member(Member) && Member != Base::primary_key_;
        }

        // Append "<name>=?" (or "<name>_id=?" for FK fields) for one member.
        template <typename Buf> static consteval auto append_one_assignment(Buf& buf, std::meta::info member) -> void {
            buf.append(std::meta::identifier_of(member));
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                buf.append("_id=?");
            } else {
                buf.append("=?");
            }
        }

        // Is `member` an auto_update field NOT already present in the explicit pack?
        template <std::meta::info... Members>
        static consteval auto is_unlisted_auto_update(std::meta::info member) -> bool {
            return Base::is_auto_update_field(member) && ((member != Members) && ...);
        }

        // Build the SET clause for the explicit Members... pack, then append any
        // auto_update field of T that the caller did not list. The column ORDER
        // here is the canonical bind order used by bind_conditional_set().
        template <std::meta::info... Members> static consteval auto build_conditional_set_clause() {
            ConstexprString<utilities::buffer_size::SQL_MEDIUM> result;
            bool                                                first = true;
            // (1) explicit members, in the given order
            (
                    [&] {
                        if (!first) {
                            result.append(", ");
                        }
                        append_one_assignment(result, Members);
                        first = false;
                    }(),
                    ...
            );
            // (2) auto_update fields not already listed (stamped now() at bind time)
            for (const auto& member : Base::all_members_) {
                if (is_unlisted_auto_update<Members...>(member)) {
                    if (!first) {
                        result.append(", ");
                    }
                    result.append(std::meta::identifier_of(member));
                    result.append("=?");
                    first = false;
                }
            }
            return result;
        }

        // Compile-time UPDATE SQL size calculation
        static consteval auto calculate_update_sql_size() -> std::size_t {
            using utilities::sql_len::SET;
            using utilities::sql_len::UPDATE;
            using utilities::sql_len::WHERE;
            std::size_t size = 0;
            size += UPDATE; // "UPDATE "
            size += Base::table_name_.size();
            size += SET; // " SET "
            size += field_assignments_.len;
            size += WHERE; // " WHERE "
            size += Base::pk_name_.size();
            size += 4; // " = ?"
            size += 1; // null terminator
            return size;
        }

        // Build UPDATE SQL at compile-time using ConstexprString
        static consteval auto build_update_sql_array() {
            constexpr std::size_t     sql_size = calculate_update_sql_size() + utilities::sql_len::LARGE_BUFFER;
            ConstexprString<sql_size> result;

            result.append("UPDATE ");
            result.append(Base::table_name_);
            result.append(" SET ");
            result.append(std::string_view(field_assignments_.data.data(), field_assignments_.len));
            result.append(" WHERE ");
            result.append(Base::pk_name_);
            result.append(" = ?");

            return result;
        }

        // Pre-computed UPDATE SQL generated at compile-time
        static constexpr auto           update_sql_array  = build_update_sql_array();
        static inline const std::string update_sql_string = std::string(update_sql_array);

      private:
        // Generate UPDATE SQL string (compile-time computed, runtime accessible)
        static auto get_update_sql() -> const std::string& {
            return update_sql_string;
        }

        // std::unexpected returned when update<...>() is called with no WHERE filter,
        // refusing to write the whole table by accident. -1 is the backend-agnostic
        // "client-side validation" code (same shape as the conditional DELETE refusal).
        static auto empty_where_error() -> std::unexpected<Error> {
            return std::unexpected(Error{-1, "update() requires a WHERE clause; use update_all() to update all rows"});
        }

        // Conditional UPDATE (#403): build "UPDATE <table> SET <set_clause> WHERE <expr>".
        // The SET clause is compile-time (keyed on the Members... pack); the WHERE body is
        // dynamic (runtime to_sql), so the full string is assembled at runtime like SELECT's
        // dynamic path — not a compile-time ConstexprString.
        template <std::meta::info... Members>
        static auto build_conditional_update_sql(const orm::where::ExpressionVariant& expr) -> std::string {
            static const std::string set_clause = std::string(build_conditional_set_clause<Members...>());
            std::string              sql;
            sql.reserve(Base::table_name_.size() + set_clause.size() + 32);
            sql += "UPDATE ";
            sql += Base::table_name_;
            sql += " SET ";
            sql += set_clause;
            sql += " WHERE ";
            sql += orm::where::to_sql(expr);
            return sql;
        }

        // Full-table UPDATE (#409): build "UPDATE <table> SET <set_clause>" with NO WHERE.
        // Same SET clause as the conditional path (Members... + unlisted auto_update fields);
        // it just omits the WHERE — the explicit escape hatch mirroring DELETE's delete_all_sql.
        template <std::meta::info... Members> static auto build_update_all_sql() -> std::string {
            static const std::string set_clause = std::string(build_conditional_set_clause<Members...>());
            std::string              sql;
            sql.reserve(Base::table_name_.size() + set_clause.size() + 16);
            sql += "UPDATE ";
            sql += Base::table_name_;
            sql += " SET ";
            sql += set_clause;
            return sql;
        }

      public:
        explicit UpdateStatement(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        // Both proxies' static `sql()` returned `update_sql_string`. Keep it
        // in one place so SingleQuery / BulkQuery only carry the
        // execute/to_sql shapes that genuinely differ.
        struct QueryBase {
            [[nodiscard]] static auto sql() -> std::string {
                return update_sql_string;
            }
        };

        struct SingleQuery : QueryBase {
            UpdateStatement    stmt;
            const T&           obj;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute_single_optimized(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(obj);
            }
        };

        struct BulkQuery : QueryBase {
            UpdateStatement    stmt;
            std::span<const T> objects;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute(objects);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(objects);
            }
        };

        auto query(const T& obj [[clang::lifetimebound]]) -> SingleQuery {
            return {{}, std::move(*this), obj};
        }
        // LIFETIME CONTRACT (issue #357, finding B): the returned BulkQuery holds a
        // std::span<const T> aliasing the caller's container. `[[clang::lifetimebound]]`
        // catches the inline-temporary case at compile time, but a span built from a
        // container that dies before .execute()/.to_sql() runs still dangles silently at
        // runtime. Treat the proxy as single-expression-use: keep the backing container
        // alive until the terminal call completes.
        auto query(std::span<const T> objects [[clang::lifetimebound]]) -> BulkQuery {
            return {{}, std::move(*this), objects};
        }

        // Conditional bulk UPDATE (#403): UPDATE <table> SET <set> WHERE <where_expr>.
        // SET columns are the Members... pack; values come from `proto`. A null where_expr
        // is refused at execute()/to_sql() time so a dropped where() cannot write the whole
        // table (update_all() is the explicit full-table path — see UpdateAllQuery, #409).
        template <std::meta::info... Members> struct ConditionalUpdateQuery {
            UpdateStatement                  stmt;
            const T&                         proto;
            orm::where::ExpressionVariantPtr where_expr;
            [[nodiscard]] auto               execute() -> std::expected<void, Error> {
                return stmt.template execute_where<Members...>(proto, where_expr);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.template to_sql_where<Members...>(proto, where_expr);
            }
        };

        template <std::meta::info... Members>
            requires(sizeof...(Members) > 0 && (is_settable_member<Members>() && ...))
        auto query_where(const T& proto [[clang::lifetimebound]], orm::where::ExpressionVariantPtr where_expr)
                -> ConditionalUpdateQuery<Members...> {
            return {std::move(*this), proto, std::move(where_expr)};
        }

        // Full-table UPDATE (#409): UPDATE <table> SET <set> with NO WHERE. The explicit
        // escape hatch named by the empty-WHERE refusal, mirroring DeleteAllQuery.
        template <std::meta::info... Members> struct UpdateAllQuery {
            UpdateStatement    stmt;
            const T&           proto;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.template execute_all<Members...>(proto);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.template to_sql_all<Members...>(proto);
            }
        };

        template <std::meta::info... Members>
            requires(sizeof...(Members) > 0 && (is_settable_member<Members>() && ...))
        auto query_all(const T& proto [[clang::lifetimebound]]) -> UpdateAllQuery<Members...> {
            return {std::move(*this), proto};
        }

        // Returns SQL string with bound parameters inlined for a single UPDATE (for debugging)
        [[nodiscard]] auto to_sql(const T& obj) -> std::expected<std::string, Error> {
            auto stmt_result = conn_->prepare_cached(get_update_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt        = *stmt_result;
            auto  bind_result = inline_bind_all_fields(stmt, obj, typename Base::field_indices_t{});
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }
            return stmt->expanded_sql();
        }

        // Returns SQL strings for bulk UPDATE (one SQL per object, for debugging)
        [[nodiscard]] auto to_sql(std::span<const T> objects) -> std::expected<std::string, Error> {
            if (objects.empty()) {
                return std::string{};
            }
            // For bulk, just show the first UPDATE SQL as representative
            return to_sql(objects[0]);
        }

        // Prepare (L3-cached) the UPDATE statement. Both execute paths share
        // this so the prepare + error-check sequence lives in one place.
        [[nodiscard]] __attribute__((always_inline)) auto prepare_stmt() noexcept -> std::expected<Statement*, Error> {
            auto prepare_result = conn_->prepare_cached(get_update_sql());
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            return *prepare_result;
        }

        // reset → inline-bind → execute on the given statement. The batch
        // loop and execute_single_row used to repeat this body verbatim.
        [[nodiscard]] __attribute__((always_inline)) auto reset_bind_execute(Statement* stmt, const T& obj) noexcept
                -> std::expected<void, Error> {
            stmt->reset();
            auto bind_result = inline_bind_all_fields(stmt, obj, typename Base::field_indices_t{});
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }
            auto exec_result = stmt->execute();
            if (!exec_result) {
                return std::unexpected(exec_result.error());
            }
            return {};
        }

        // Optimized batch execute - flattened, no nested lambdas
        [[nodiscard]] __attribute__((hot)) auto execute(std::span<const T> objects) noexcept
                -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Prepare the statement pointer once (avoid hash lookup per row)
            auto prepared = prepare_stmt();
            if (!prepared) [[unlikely]] {
                return std::unexpected(prepared.error());
            }
            Statement* stmt = *prepared;

            // Single object - no transaction needed
            if (objects.size() == 1) {
                return execute_single_row(stmt, objects[0]);
            }

            // Multiple objects - use RAII transaction guard
            auto txn = TransactionGuard<ConnType>::begin(conn_);
            if (!txn) {
                return std::unexpected(txn.error());
            }

            for (const auto& obj : objects) {
                if (auto step = reset_bind_execute(stmt, obj); !step) {
                    return step;
                }
            }

            return txn->commit();
        }

        // Execute single row with the given statement (no transaction)
        [[nodiscard]] __attribute__((always_inline)) auto execute_single_row(Statement* stmt, const T& obj) noexcept
                -> std::expected<void, Error> {
            return reset_bind_execute(stmt, obj);
        }

        // Helper to unroll inline binding for all fields (skips PK, then binds PK last)
        template <std::size_t... Is>
        [[nodiscard]] __attribute__((always_inline)) static auto
        inline_bind_all_fields(Statement* stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, Error> {
            int param_index = 1;

            // Bind all non-PK fields at compile time using unified binder.
            // IsUpdate=true: auto_update stamps now(); auto_create binds the object's
            // stored value (preserving created_at). One now() read shared across fields (#209).
            std::expected<void, Error> result{};
            const auto                 now = Base::batch_now();
            ((result = Base::template bind_field_at_index<ConnType, Is, true, true>(stmt, obj, param_index, now),
              result.has_value()) &&
             ...);

            if (!result) {
                return result;
            }

            // Bind primary key last (PK is never a FK field by design)
            auto pk_value = obj.[:Base::primary_key_:];
            return Base::template bind_value_by_type<ConnType>(*stmt, param_index, pk_value);
        }

        // Ultra-optimized single UPDATE - pre-cached statement, fully inlined binding.
        // Diverges from execute_single_row in that the reset happens AFTER execute,
        // not before bind (one less statement reset on the steady-state path).
        [[nodiscard]] __attribute__((hot)) auto execute_single_optimized(const T& obj) noexcept
                -> std::expected<void, Error> {
            auto prepared = prepare_stmt();
            if (!prepared) [[unlikely]] {
                return std::unexpected(prepared.error());
            }
            Statement* stmt = *prepared;
            // FULLY INLINED BINDING - all compile-time dispatched, no function calls
            auto bind_result = inline_bind_all_fields(stmt, obj, typename Base::field_indices_t{});
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            // Execute and reset for next use
            auto exec_result = stmt->execute();
            if (!exec_result) {
                stmt->reset();
                return std::unexpected(exec_result.error());
            }

            stmt->reset();
            return {};
        }

        // --- Conditional bulk UPDATE (#403) binding + execution ---

        // Compile-time fold over field indices: for each index whose member is an
        // unlisted auto_update field, bind now() at param_index (declaration order).
        // `if constexpr` keeps consteval-only std::meta::info out of any runtime context.
        template <std::meta::info... Members, std::size_t... Is>
        [[nodiscard]] auto bind_unlisted_auto_updates(
                Statement*                            stmt,
                int&                                  param_index,
                std::chrono::system_clock::time_point now,
                std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, Error> {
            std::expected<void, Error> result{};
            (
                    [&] {
                        if constexpr (is_unlisted_auto_update<Members...>(Base::all_members_[Is])) {
                            if (result.has_value()) {
                                result = Base::template bind_one<ConnType>(stmt, param_index, now);
                            }
                        }
                    }(),
                    ...
            );
            return result;
        }

        // Bind the SET values in the SAME order build_conditional_set_clause emits them:
        // (1) explicit Members... from `proto`, (2) unlisted auto_update fields stamped now().
        // param_index advances; WHERE binding continues from where this leaves off.
        template <std::meta::info... Members>
        [[nodiscard]] auto bind_conditional_set(Statement* stmt, const T& proto, int& param_index) noexcept
                -> std::expected<void, Error> {
            const auto now = Base::batch_now(); // one clock read; compiles away if no timestamp field

            // (1) explicit members, in pack order. IsUpdate=true so an explicitly-listed
            //     auto_update member is stamped now() (matches single-row UPDATE), and a
            //     listed auto_create member binds the proto's stored value.
            std::expected<void, Error> result{};
            ((result = Base::template bind_field_at_index<ConnType, index_of_member(Members), false, true>(
                      stmt, proto, param_index, now
              ),
              result.has_value()) &&
             ...);
            if (!result) {
                return result;
            }

            // (2) unlisted auto_update fields — stamp now() in declaration order.
            return bind_unlisted_auto_updates<Members...>(stmt, param_index, now, typename Base::field_indices_t{});
        }

        // Prepare `sql`, reset, then bind the SET values — the prefix shared by the
        // conditional (WHERE, #403) and full-table (#409) paths. Conditional appends WHERE.
        template <std::meta::info... Members>
        [[nodiscard]] auto prepare_bind_set(const std::string& sql, const T& proto, int& param_index)
                -> std::expected<Statement*, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }
            Statement* stmt = *prepare_result;
            stmt->reset();
            param_index = 1;
            if (auto set_result = bind_conditional_set<Members...>(stmt, proto, param_index); !set_result) {
                return std::unexpected(set_result.error());
            }
            return stmt;
        }

        // Empty-WHERE check + prepare + SET-then-WHERE bind. Shared by execute/to_sql.
        template <std::meta::info... Members>
        [[nodiscard]] auto ready_conditional_update(const T& proto, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<Statement*, Error> {
            if (!where_expr) [[unlikely]] {
                return empty_where_error();
            }
            int  param_index = 0; // set by prepare_bind_set; WHERE bind continues from it
            auto stmt_result = prepare_bind_set<Members...>(
                    build_conditional_update_sql<Members...>(*where_expr), proto, param_index
            );
            if (!stmt_result) {
                return stmt_result;
            }
            Statement* stmt = *stmt_result;
            // WHERE params continue from param_index (same continuation as bind_having_params).
            if (auto where_result = Base::template bind_having_params<Statement, Error>(stmt, where_expr, param_index);
                !where_result) {
                return std::unexpected(where_result.error());
            }
            return stmt;
        }

        // Full-table UPDATE (#409): prepare + bind SET only (no WHERE). No empty-WHERE
        // guard — writing the whole table is the documented intent here.
        template <std::meta::info... Members>
        [[nodiscard]] auto ready_update_all(const T& proto) -> std::expected<Statement*, Error> {
            int param_index = 0; // out-param; no WHERE bind follows, so the final value is unused
            return prepare_bind_set<Members...>(build_update_all_sql<Members...>(), proto, param_index);
        }

        // Terminal shapes shared by the conditional and full-table paths: execute / expand.
        [[nodiscard]] static auto run_execute(std::expected<Statement*, Error> stmt_result)
                -> std::expected<void, Error> {
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            if (auto exec_result = (*stmt_result)->execute(); !exec_result) {
                return std::unexpected(exec_result.error());
            }
            return {};
        }
        [[nodiscard]] static auto run_to_sql(std::expected<Statement*, Error> stmt_result)
                -> std::expected<std::string, Error> {
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            return (*stmt_result)->expanded_sql();
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto execute_where(const T& proto, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            return run_execute(ready_conditional_update<Members...>(proto, where_expr));
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto to_sql_where(const T& proto, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<std::string, Error> {
            return run_to_sql(ready_conditional_update<Members...>(proto, where_expr));
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto execute_all(const T& proto) -> std::expected<void, Error> {
            return run_execute(ready_update_all<Members...>(proto));
        }

        template <std::meta::info... Members>
        [[nodiscard]] auto to_sql_all(const T& proto) -> std::expected<std::string, Error> {
            return run_to_sql(ready_update_all<Members...>(proto));
        }

      private:
        std::shared_ptr<ConnType> conn_;
    };

} // namespace storm::orm::statements
