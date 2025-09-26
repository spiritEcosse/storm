module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_insert;

import storm_orm_statements_base;
import storm_db_concept;
import storm_db_sqlite;

import <expected>;
import <string>;
import <string_view>;
import <concepts>;
import <format>;
import <meta>;
import <array>;
import <span>;
import <vector>;
import <type_traits>;

export namespace storm::orm::statements {

    // Statement class for ORM insert operations
    template <typename T, storm::db::DatabaseConnection ConnType>
    class InsertStatement : private BaseStatement<T> {
        using Base = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-compute field names at template instantiation time (like RemoveStatement does with primary key)
        static consteval std::string build_field_names() {
            std::string result;
            bool first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (!first) {
                    result += ", ";
                }
                result += std::meta::identifier_of(Base::all_members_[i]);
                first = false;
            }
            return result;
        }

        static consteval std::string build_placeholders() {
            std::string result;
            bool first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (!first) {
                    result += ", ";
                }
                result += "?";
                first = false;
            }
            return result;
        }

        // Pre-computed field information (use Base class utilities)
        static constexpr auto field_names_ = build_field_names();
        static constexpr auto placeholders_ = build_placeholders();

        // Generate INSERT SQL string at runtime (cached)
        static const std::string& get_insert_sql() {
            static const std::string sql = std::format("INSERT INTO {} ({}) VALUES ({})", Base::table_name_, field_names_, placeholders_);
            return sql;
        }

        // Generate bulk INSERT SQL with multiple value sets
        static std::string get_bulk_insert_sql(size_t count) {
            std::string sql = std::format("INSERT INTO {} ({}) VALUES ", Base::table_name_, field_names_);
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) sql += ", ";
                sql += "(";
                sql += placeholders_;
                sql += ")";
            }
            return sql;
        }

      public:
        explicit InsertStatement(Connection& conn) : conn_(conn) {}

        [[nodiscard]] auto execute(const T& obj) noexcept -> std::expected<void, Error> {
            // Use cached prepared statement for better performance if available
            if constexpr (requires { conn_.prepare_cached(get_insert_sql()); }) {
                return conn_.prepare_cached(get_insert_sql())
                        .and_then([this, &obj](Statement* stmt) -> std::expected<void, Error> {
                            return bind_and_execute_cached(*stmt, obj);
                        });
            } else {
                // Fallback to regular prepare for non-cached connections
                return conn_.prepare(get_insert_sql())
                        .and_then([this, &obj](Statement stmt) -> std::expected<void, Error> {
                            return bind_and_execute(std::move(stmt), obj);
                        });
            }
        }

        // Batch insert operation with optimization strategies
        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            if (objects.empty()) {
                return {};
            }

            // Single object - use regular execute
            if (objects.size() == 1) {
                return execute(objects[0]);
            }

            // For small batches, use bulk INSERT with multiple VALUES
            // SQLite has SQLITE_MAX_VARIABLE_NUMBER limit (default 999)
            // With field_count_ fields per object, we can insert up to 999/field_count_ objects
            constexpr size_t max_bulk_size = 999 / Base::field_count_;
            constexpr size_t bulk_threshold = std::min(size_t(50), max_bulk_size);

            if (objects.size() <= bulk_threshold) {
                return execute_bulk(objects);
            }

            // For large batches, use individual statements with transactions
            return execute_with_cached_statement(objects);
        }

      private:
        // Bind all fields and execute (regular statement)
        [[nodiscard]] auto bind_and_execute(Statement stmt, const T& obj) noexcept -> std::expected<void, Error> {
            // Bind all field values
            if (auto result = bind_all_fields(stmt, obj); !result) {
                return std::unexpected(result.error());
            }

            // Execute statement
            if (auto result = stmt.execute(); !result) {
                return std::unexpected(result.error());
            }

            return {};
        }

        // Bind all fields and execute (cached statement)
        [[nodiscard]] auto bind_and_execute_cached(Statement& stmt, const T& obj) noexcept
                -> std::expected<void, Error> {
            // Reset statement for reuse
            stmt.reset();

            // Bind all field values
            if (auto result = bind_all_fields(stmt, obj); !result) {
                return std::unexpected(result.error());
            }

            // Execute statement
            if (auto result = stmt.execute(); !result) {
                return std::unexpected(result.error());
            }

            return {};
        }

        // Helper template to bind a single field at compile-time index
        template<size_t Index>
        [[nodiscard]] auto bind_field_by_index(Statement& stmt, const T& obj, int param_index) noexcept -> std::expected<void, Error> {
            if constexpr (Index < Base::field_count_) {
                // Use pre-computed constexpr member (from Base class)
                constexpr auto member = Base::all_members_[Index];
                auto field_value = obj.[:member:];

                // Use shared binding utility from Base class
                return Base::template bind_value_by_type<ConnType>(stmt, param_index, field_value);
            }
            return {};
        }

        // Template recursion to bind all fields using constexpr index
        template<size_t Index>
        [[nodiscard]] auto bind_fields_recursive(Statement& stmt, const T& obj, int& param_index) noexcept -> std::expected<void, Error> {
            if constexpr (Index < Base::field_count_) {
                // Bind current field
                if (auto result = bind_field_by_index<Index>(stmt, obj, param_index); !result) {
                    return std::unexpected(result.error());
                }
                ++param_index;

                // Recursively bind remaining fields
                return bind_fields_recursive<Index + 1>(stmt, obj, param_index);
            }
            return {};
        }

        // Bind all fields of an object using template expansion (following RemoveStatement pattern)
        [[nodiscard]] auto bind_all_fields(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            int param_index = 1;
            return bind_fields_recursive<0>(stmt, obj, param_index);
        }

        // Execute bulk INSERT with multiple VALUES clauses
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            const auto sql = get_bulk_insert_sql(objects.size());
            bool use_transaction = Base::should_use_transaction(objects);

            if (use_transaction) {
                if (auto result = Base::begin_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            auto result = conn_.prepare(sql)
                    .and_then([this, objects](Statement stmt) -> std::expected<void, Error> {
                        // Bind all parameters for all objects
                        int param_index = 1;
                        for (const auto& obj : objects) {
                            if (auto result = bind_fields_recursive<0>(stmt, obj, param_index); !result) {
                                return std::unexpected(result.error());
                            }
                        }

                        // Execute the bulk insert
                        if (auto result = stmt.execute(); !result) {
                            return std::unexpected(result.error());
                        }

                        return {};
                    });

            if (use_transaction) {
                if (!result) {
                    Base::rollback_transaction(conn_);
                    return result;
                }

                if (auto commit_result = Base::commit_transaction(conn_); !commit_result) {
                    Base::rollback_transaction(conn_);
                    return std::unexpected(commit_result.error());
                }
            }

            return result;
        }

        // Execute with cached prepared statement for large batches
        [[nodiscard]] auto execute_with_cached_statement(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            bool use_transaction = Base::should_use_transaction(objects);

            if (use_transaction) {
                if (auto result = Base::begin_transaction(conn_); !result) {
                    return std::unexpected(result.error());
                }
            }

            // Use cached prepared statement for better performance
            auto result = [this, objects]() -> std::expected<void, Error> {
                if constexpr (requires { conn_.prepare_cached(get_insert_sql()); }) {
                    return conn_.prepare_cached(get_insert_sql())
                            .and_then([this, objects](Statement* stmt) -> std::expected<void, Error> {
                                for (const auto& obj : objects) {
                                    if (auto result = bind_and_execute_cached(*stmt, obj); !result) {
                                        return std::unexpected(result.error());
                                    }
                                }
                                return {};
                            });
                } else {
                    // Fallback to regular prepare for non-cached connections
                    return conn_.prepare(get_insert_sql())
                            .and_then([this, objects](Statement stmt) -> std::expected<void, Error> {
                                for (const auto& obj : objects) {
                                    stmt.reset();
                                    if (auto result = bind_all_fields(stmt, obj); !result) {
                                        return std::unexpected(result.error());
                                    }
                                    if (auto result = stmt.execute(); !result) {
                                        return std::unexpected(result.error());
                                    }
                                }
                                return {};
                            });
                }
            }();

            if (use_transaction) {
                if (!result) {
                    Base::rollback_transaction(conn_);
                    return result;
                }

                if (auto commit_result = Base::commit_transaction(conn_); !commit_result) {
                    Base::rollback_transaction(conn_);
                    return std::unexpected(commit_result.error());
                }
            }

            return result;
        }

        Connection& conn_;
    };

} // namespace storm::orm::statements