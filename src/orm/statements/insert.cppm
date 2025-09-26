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

        Connection& conn_;
    };

} // namespace storm::orm::statements