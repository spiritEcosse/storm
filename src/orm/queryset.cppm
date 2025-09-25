module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_queryset;

import storm_db_concept;
import storm_db_sqlite;

import <expected>;
import <string>;
import <string_view>;
import <span>;
import <concepts>;
import <memory>;
import <format>;
import <meta>;

export namespace storm {

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique };
    }

    // Statement classes for ORM operations
    template <typename T, storm::db::DatabaseConnection ConnType> class RemoveStatement {
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Helper to find primary key using storm::meta attributes
        static consteval std::meta::info find_primary_key_impl() {
            for (std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
                    return member;
                }
            }
            throw "Model must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
        }

        // Compile-time reflection attributes - computed once per template instantiation
        static constexpr auto primary_key_ = find_primary_key_impl();
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

        // Pre-compute DELETE SQL string template at compile-time
        static consteval std::string_view get_delete_sql_template() {
            return "DELETE FROM {} WHERE {} = ?";
        }

        // Generate DELETE SQL string at runtime (cached)
        static const std::string& get_delete_sql() {
            static const std::string sql = std::format(get_delete_sql_template(), table_name_, pk_name_);
            return sql;
        }

        // Generate bulk DELETE SQL string for IN clause (size-specific)
        static std::string get_bulk_delete_sql(size_t count) {
            if (count == 1) {
                return get_delete_sql();
            }

            std::string placeholders = "?";
            for (size_t i = 1; i < count; ++i) {
                placeholders += ",?";
            }

            return std::format("DELETE FROM {} WHERE {} IN ({})", table_name_, pk_name_, placeholders);
        }

      public:
        explicit RemoveStatement(Connection& conn) : conn_(conn) {}

        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            if (objects.empty()) {
                return true;
            }

            // For small batches, use bulk IN clause for better performance
            if (objects.size() <= 50) {  // SQLite has SQLITE_MAX_VARIABLE_NUMBER limit (default 999)
                return execute_bulk(objects);
            }

            // For large batches, use individual statements with transactions
            // Use cached prepared statement for better performance
            if constexpr (requires { conn_.prepare_cached(get_delete_sql()); }) {
                return conn_.prepare_cached(get_delete_sql())
                        .and_then([this, objects](Statement* stmt) -> std::expected<bool, Error> {
                            return bind_and_execute_cached(*stmt, objects);
                        });
            } else {
                // Fallback to regular prepare for non-cached connections
                return conn_.prepare(get_delete_sql())
                        .and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                            return bind_and_execute(std::move(stmt), objects);
                        });
            }
        }

        // Bulk execute using IN clause for better performance on small batches
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            auto bulk_sql = get_bulk_delete_sql(objects.size());

            return conn_.prepare(bulk_sql)
                    .and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                        return bind_and_execute_bulk(std::move(stmt), objects);
                    });
        }

      private:
        // Bind primary keys and execute for all objects (with transaction wrapping for batches)
        [[nodiscard]] auto bind_and_execute(Statement stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Use transaction for batch operations to improve performance
            bool use_transaction = objects.size() > 1;

            if (use_transaction) {
                if (auto result = conn_.execute("BEGIN TRANSACTION"); !result) {
                    return std::unexpected(result.error());
                }
            }

            for (const auto& obj : objects) {
                // Reset statement for reuse
                stmt.reset();

                // Bind primary key value using meta reflection
                if (auto result = bind_primary_key(stmt, obj); !result) {
                    if (use_transaction) {
                        (void)conn_.execute("ROLLBACK");
                    }
                    return std::unexpected(result.error());
                }

                // Execute statement
                if (auto result = stmt.execute(); !result) {
                    if (use_transaction) {
                        (void)conn_.execute("ROLLBACK");
                    }
                    return std::unexpected(result.error());
                }
            }

            if (use_transaction) {
                if (auto result = conn_.execute("COMMIT"); !result) {
                    return std::unexpected(result.error());
                }
            }

            return true;
        }

        // Optimized version for cached statements
        [[nodiscard]] auto bind_and_execute_cached(Statement& stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Use transaction for batch operations to improve performance
            bool use_transaction = objects.size() > 1;

            if (use_transaction) {
                if (auto result = conn_.execute("BEGIN TRANSACTION"); !result) {
                    return std::unexpected(result.error());
                }
            }

            for (const auto& obj : objects) {
                // Reset statement for reuse
                stmt.reset();

                // Bind primary key value using meta reflection
                if (auto result = bind_primary_key(stmt, obj); !result) {
                    if (use_transaction) {
                        (void)conn_.execute("ROLLBACK");
                    }
                    return std::unexpected(result.error());
                }

                // Execute statement
                if (auto result = stmt.execute(); !result) {
                    if (use_transaction) {
                        (void)conn_.execute("ROLLBACK");
                    }
                    return std::unexpected(result.error());
                }
            }

            if (use_transaction) {
                if (auto result = conn_.execute("COMMIT"); !result) {
                    return std::unexpected(result.error());
                }
            }

            return true;
        }

        // Bind and execute bulk operation (single statement, multiple parameters)
        [[nodiscard]] auto bind_and_execute_bulk(Statement stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            // Bind all primary key values
            int param_index = 1;
            for (const auto& obj : objects) {
                if (auto result = bind_primary_key_at_index(stmt, obj, param_index); !result) {
                    return std::unexpected(result.error());
                }
                ++param_index;
            }

            // Execute once with all parameters bound
            if (auto result = stmt.execute(); !result) {
                return std::unexpected(result.error());
            }

            return true;
        }

        // Bind primary key value at specific parameter index
        [[nodiscard]] auto bind_primary_key_at_index(Statement& stmt, const T& obj, int index) noexcept
                -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:primary_key_:];

            // Bind based on type - database-agnostic binding
            if constexpr (std::is_same_v<decltype(pk_value), int>) {
                return stmt.bind_int(index, pk_value);
            } else if constexpr (std::is_convertible_v<decltype(pk_value), std::string_view>) {
                return stmt.bind_text(index, std::string_view{pk_value});
            } else {
                static_assert(
                        std::is_same_v<decltype(pk_value), int> ||
                                std::is_convertible_v<decltype(pk_value), std::string_view>,
                        "Unsupported primary key type"
                );
            }
        }

        // Bind primary key value using pre-computed reflection data
        [[nodiscard]] auto bind_primary_key(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:primary_key_:];

            // Bind based on type - database-agnostic binding
            if constexpr (std::is_same_v<decltype(pk_value), int>) {
                return stmt.bind_int(1, pk_value);
            } else if constexpr (std::is_convertible_v<decltype(pk_value), std::string_view>) {
                return stmt.bind_text(1, std::string_view{pk_value});
            } else {
                static_assert(
                        std::is_same_v<decltype(pk_value), int> ||
                                std::is_convertible_v<decltype(pk_value), std::string_view>,
                        "Unsupported primary key type"
                );
            }
        }

        Connection& conn_;
    };

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

      public:
        // Constructor with explicit connection (backward compatibility)
        explicit QuerySet(ConnType& conn) : conn_(conn) {}

        // Default constructor using default connection
        QuerySet()
            requires std::same_as<ConnType, storm::db::sqlite::Connection>
            : conn_(get_default_connection()) {}

        std::expected<bool, Error> remove(const T& obj) {
            return execute_remove(std::span<const T>{&obj, 1});
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
        [[nodiscard]] std::expected<bool, Error> execute_remove(std::span<const T> objects) const noexcept {
            return RemoveStatement<T, ConnType>(conn_).execute(objects);
        }

        ConnType& conn_;
    };

    // Factory function for convenient QuerySet creation with default connection
    template <typename T> [[nodiscard]] auto make_queryset() -> QuerySet<T> {
        return QuerySet<T>{};
    }

    // Factory function for convenient QuerySet creation with explicit connection
    template <typename T, storm::db::DatabaseConnection ConnType>
    [[nodiscard]] auto make_queryset(ConnType& conn) -> QuerySet<T, ConnType> {
        return QuerySet<T, ConnType>{conn};
    }

} // namespace storm
