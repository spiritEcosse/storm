module;

#include <meta>

export module storm_orm_statements_update;

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
import <array>;
import <utility>;

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
        static consteval auto get_updatable_field_count() -> size_t {
            size_t count = 0;
            for (const auto& member : Base::all_members_) {
                if (member != Base::primary_key_) {
                    ++count;
                }
            }
            return count;
        }

        static constexpr size_t updatable_field_count_ = get_updatable_field_count();

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
        static consteval auto calculate_update_sql_size() -> size_t {
            using utilities::sql_len::SET;
            using utilities::sql_len::UPDATE;
            using utilities::sql_len::WHERE;
            size_t size = 0;
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
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_update_sql_size() + utilities::sql_len::LARGE_BUFFER;
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

        struct SingleQuery {
            UpdateStatement&   stmt;
            const T&           obj;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute_single_optimized(obj);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(obj);
            }
            [[nodiscard]] static auto sql() -> std::string {
                return update_sql_string;
            }
        };

        struct BulkQuery {
            UpdateStatement&   stmt;
            std::span<const T> objects;
            [[nodiscard]] auto execute() -> std::expected<void, Error> {
                return stmt.execute(objects);
            }
            [[nodiscard]] auto to_sql() -> std::expected<std::string, Error> {
                return stmt.to_sql(objects);
            }
            [[nodiscard]] static auto sql() -> std::string {
                return update_sql_string;
            }
        };

        auto query(const T& obj) -> SingleQuery {
            return {*this, obj};
        }
        auto query(std::span<const T> objects) -> BulkQuery {
            return {*this, objects};
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

        // Optimized batch execute - flattened, no nested lambdas
        [[nodiscard]] __attribute__((hot)) auto execute(std::span<const T> objects) noexcept
                -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Cache the statement pointer once (avoid hash lookup per row)
            if (cached_update_stmt_ == nullptr) {
                auto stmt_result = conn_->prepare_cached(get_update_sql());
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_update_stmt_ = *stmt_result;
            }

            // Single object - no transaction needed
            if (objects.size() == 1) {
                return execute_single_row(objects[0]);
            }

            // Multiple objects - use RAII transaction guard
            auto txn = TransactionGuard<ConnType>::begin(conn_);
            if (!txn) {
                return std::unexpected(txn.error());
            }

            // Execute all updates with cached statement
            for (const auto& obj : objects) {
                cached_update_stmt_->reset();

                auto bind_result = inline_bind_all_fields(cached_update_stmt_, obj, typename Base::field_indices_t{});
                if (!bind_result) {
                    return std::unexpected(bind_result.error());
                }

                auto exec_result = cached_update_stmt_->execute();
                if (!exec_result) {
                    return std::unexpected(exec_result.error());
                }
            }

            return txn->commit();
        }

        // Execute single row with cached statement (no transaction)
        [[nodiscard]] __attribute__((always_inline)) auto execute_single_row(const T& obj) noexcept
                -> std::expected<void, Error> {
            cached_update_stmt_->reset();

            auto bind_result = inline_bind_all_fields(cached_update_stmt_, obj, typename Base::field_indices_t{});
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            auto exec_result = cached_update_stmt_->execute();
            if (!exec_result) {
                return std::unexpected(exec_result.error());
            }

            return {};
        }

        // Helper to unroll inline binding for all fields (skips PK, then binds PK last)
        template <size_t... Is>
        [[nodiscard]] __attribute__((always_inline)) static auto
        inline_bind_all_fields(Statement* stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, Error> {
            int param_index = 1;

            // Bind all non-PK fields at compile time using unified binder
            std::expected<void, Error> result{};
            ((result = Base::template bind_field_at_index<ConnType, Is, true>(stmt, obj, param_index),
              result.has_value()) &&
             ...);

            if (!result) {
                return result;
            }

            // Bind primary key last (PK is never a FK field by design)
            auto pk_value = obj.[:Base::primary_key_:];
            return Base::template bind_value_by_type<ConnType>(*stmt, param_index, pk_value);
        }

        // Ultra-optimized single UPDATE - pre-cached statement, fully inlined binding
        [[nodiscard]] __attribute__((hot)) auto execute_single_optimized(const T& obj) noexcept
                -> std::expected<void, Error> {
            // Get or cache the prepared statement
            if (cached_update_stmt_ == nullptr) {
                auto stmt_result = conn_->prepare_cached(get_update_sql());
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_update_stmt_ = *stmt_result;
            }

            // FULLY INLINED BINDING - all compile-time dispatched, no function calls
            auto bind_result = inline_bind_all_fields(cached_update_stmt_, obj, typename Base::field_indices_t{});
            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            // Execute and reset for next use
            auto exec_result = cached_update_stmt_->execute();
            if (!exec_result) {
                cached_update_stmt_->reset();
                return std::unexpected(exec_result.error());
            }

            cached_update_stmt_->reset();
            return {};
        }

      private:
        std::shared_ptr<ConnType> conn_;
        mutable Statement*        cached_update_stmt_ = nullptr; // Cached statement for optimized single UPDATE
    };

} // namespace storm::orm::statements