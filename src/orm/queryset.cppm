module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_queryset;

import storm_db_concept;
import storm_db_sqlite;
import storm_orm_statements_base;
import storm_orm_statements_remove;
import storm_orm_statements_insert;

import <expected>;
import <string>;
import <string_view>;
import <span>;
import <concepts>;
import <memory>;
import <vector>;

export namespace storm {

    // Default connection management for QuerySet
    // WARNING: Not thread-safe - use external synchronization in multi-threaded environments
    namespace detail {
        inline auto& get_default_connection_ptr() {
            static std::unique_ptr<db::sqlite::Connection> conn_;
            return conn_;
        }
    } // namespace detail

    template <class T, storm::db::DatabaseConnection ConnType = storm::db::sqlite::Connection> class QuerySet {
        using Error = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

      public:
        // Default constructor using default connection
        QuerySet()
            requires std::same_as<ConnType, storm::db::sqlite::Connection>
            : conn_(get_default_connection()) {}

        std::expected<void, Error> remove(const T& obj) {
            // Ultra-optimized fast path - inline DELETE with cached statement
            if (!cached_delete_stmt_) {
                // Get DELETE SQL from RemoveStatement
                auto delete_sql = orm::statements::RemoveStatement<T, ConnType>::get_delete_sql_static();
                auto stmt_result = conn_.prepare_cached(delete_sql);
                if (!stmt_result) {
                    return std::unexpected(stmt_result.error());
                }
                cached_delete_stmt_ = *stmt_result;
            }

            // Inline bind and execute - minimal overhead
            using Base = orm::statements::BaseStatement<T>;
            auto pk_value = obj.[:Base::get_primary_key():];

            // Direct bind without abstraction
            std::expected<void, Error> bind_result;
            if constexpr (std::is_same_v<decltype(pk_value), int>) {
                bind_result = cached_delete_stmt_->bind_int(1, pk_value);
            } else if constexpr (std::is_convertible_v<decltype(pk_value), std::string_view>) {
                bind_result = cached_delete_stmt_->bind_text(1, std::string_view{pk_value});
            }

            if (!bind_result) {
                return std::unexpected(bind_result.error());
            }

            auto exec_result = cached_delete_stmt_->execute();
            if (!exec_result) {
                cached_delete_stmt_->reset();
                return std::unexpected(exec_result.error());
            }

            cached_delete_stmt_->reset();
            return {};
        }

        // Bulk remove operations
        std::expected<void, Error> remove(std::span<const T> objects) {
            return orm::statements::RemoveStatement<T, ConnType>(conn_).execute(objects);
        }

        // Insert operations
        std::expected<int64_t, Error> insert(const T& obj) {
            return execute_insert(std::span<const T>{&obj, 1})
                .transform([](const auto& ids) { return ids[0]; });
        }

        // Bulk insert operations
        std::expected<std::vector<int64_t>, Error> insert(std::span<const T> objects) {
            return execute_insert(objects);
        }

        // Static methods for connection management
        // NOTE: These methods are NOT thread-safe. For multi-threaded use:
        // 1. Use external synchronization (mutex/lock) around these calls, OR
        // 2. Create QuerySet with explicit connection per thread
        [[nodiscard]] static auto set_default_connection(std::string_view db_path) noexcept
                -> std::expected<void, db::sqlite::Error> {
            auto conn_result = db::sqlite::Connection::open(db_path);
            if (!conn_result) {
                return std::unexpected(conn_result.error());
            }

            auto conn_ptr = std::make_unique<db::sqlite::Connection>(std::move(conn_result.value()));

            // Pre-populate statement cache for better performance
            conn_ptr->prepare_common_statements();

            detail::get_default_connection_ptr() = std::move(conn_ptr);
            return {};
        }

        [[nodiscard]] static auto& get_default_connection() {
            if (!detail::get_default_connection_ptr()) {
                throw std::runtime_error(
                        "Default database connection not set. Call QuerySet::set_default_connection() first."
                );
            }
            return *detail::get_default_connection_ptr();
        }

        [[nodiscard]] static bool has_default_connection() noexcept {
            return static_cast<bool>(detail::get_default_connection_ptr());
        }

        static void clear_default_connection() noexcept {
            detail::get_default_connection_ptr().reset();
        }

      private:
        [[nodiscard]] std::expected<std::vector<int64_t>, Error>
        execute_insert(std::span<const T> objects) const noexcept {
            return orm::statements::InsertStatement<T, ConnType>(conn_).execute(objects);
        }

        ConnType& conn_;
        mutable Statement* cached_delete_stmt_ = nullptr; // Cached statement for ultra-fast single DELETE
    };

    // Factory function for convenient QuerySet creation with default connection
    template <typename T> [[nodiscard]] auto make_queryset() -> QuerySet<T> {
        return QuerySet<T>{};
    }

} // namespace storm