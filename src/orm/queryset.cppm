module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_queryset;

import storm_db_concept;
import storm_db_sqlite;
import storm_orm_statements_base;
import storm_orm_statements_remove;
import storm_orm_statements_insert;
import storm_orm_statements_select;

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
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

      public:
        // Default constructor using default connection
        QuerySet()
            requires std::same_as<ConnType, storm::db::sqlite::Connection>
            : conn_(get_default_connection()) {}

        std::expected<void, Error> remove(const T& obj) {
            // Use cached RemoveStatement instance for optimal performance
            return get_remove_statement().execute_single_optimized(obj);
        }

        // Bulk remove operations
        std::expected<void, Error> remove(std::span<const T> objects) {
            return get_remove_statement().execute(objects);
        }

        // Insert operations
        std::expected<int64_t, Error> insert(const T& obj) {
            return execute_insert(std::span<const T>{&obj, 1}).transform([](const auto& ids) { return ids[0]; });
        }

        // Bulk insert operations
        std::expected<std::vector<int64_t>, Error> insert(std::span<const T> objects) {
            return execute_insert(objects);
        }

        // Select operations - returns all rows (optimized with statement caching)
        std::expected<std::vector<T>, Error> select() {
            return get_select_statement().execute_optimized();
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

        // Lazy-initialize and return cached RemoveStatement for optimal performance
        auto get_remove_statement() const -> orm::statements::RemoveStatement<T, ConnType>& {
            if (!remove_stmt_) {
                remove_stmt_ = std::make_unique<orm::statements::RemoveStatement<T, ConnType>>(conn_);
            }
            return *remove_stmt_;
        }

        // Lazy-initialize and return cached SelectStatement for optimal performance
        auto get_select_statement() const -> orm::statements::SelectStatement<T, ConnType>& {
            if (!select_stmt_) {
                select_stmt_ = std::make_unique<orm::statements::SelectStatement<T, ConnType>>(conn_);
            }
            return *select_stmt_;
        }

        ConnType&                                                              conn_;
        mutable std::unique_ptr<orm::statements::RemoveStatement<T, ConnType>> remove_stmt_;
        mutable std::unique_ptr<orm::statements::SelectStatement<T, ConnType>> select_stmt_;
    };

    // Factory function for convenient QuerySet creation with default connection
    template <typename T> [[nodiscard]] auto make_queryset() -> QuerySet<T> {
        return QuerySet<T>{};
    }

} // namespace storm