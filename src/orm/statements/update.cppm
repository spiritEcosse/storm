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

      public:
        // Public access to UPDATE SQL for QuerySet optimization
        static auto get_update_sql_static() -> const std::string& {
            return update_sql_string;
        }

      private:
        // Generate UPDATE SQL string (compile-time computed, runtime accessible)
        static auto get_update_sql() -> const std::string& {
            return update_sql_string;
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

      private:
        std::shared_ptr<ConnType> conn_;
    };

} // namespace storm::orm::statements
