module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_insert;

import storm_orm_statements_base;
import storm_orm_utilities;
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

    // Import utilities for code convenience
    using storm::orm::utilities::BulkSQLCache;
    using storm::orm::utilities::ConstexprString;

    // Statement class for ORM insert operations
    template <typename T, storm::db::DatabaseConnection ConnType> class InsertStatement : private BaseStatement<T> {
        friend class BaseStatement<T>; // Allow BaseStatement to access protected/private members
        using Base       = BaseStatement<T>;
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Pre-compute field names at template instantiation time (like RemoveStatement does with primary key)
        static consteval std::string build_field_names() {
            std::string result;
            bool        first = true;
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
            bool        first = true;
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
        static constexpr auto field_names_  = build_field_names();
        static constexpr auto placeholders_ = build_placeholders();

        // Compile-time SQL size calculation
        static consteval size_t calculate_insert_sql_size() {
            size_t size = 0;
            size += 12; // "INSERT INTO "

            // Table name length
            size += Base::table_name_.size();

            size += 2; // " ("

            // Field names length
            size += field_names_.size();

            size += 10; // ") VALUES ("

            // Placeholders length
            size += placeholders_.size();

            size += 1; // ")"
            size += 1; // null terminator

            return size;
        }

        // Build INSERT SQL at compile-time using ConstexprString
        static consteval auto build_insert_sql_array() {
            constexpr size_t          sql_size = calculate_insert_sql_size() + 100; // Add buffer for safety
            ConstexprString<sql_size> result;

            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(field_names_);
            result.append(") VALUES (");
            result.append(placeholders_);
            result.append(")");

            return result;
        }

        // Pre-computed INSERT SQL generated at compile-time
        static constexpr auto           insert_sql_array  = build_insert_sql_array();
        static inline const std::string insert_sql_string = std::string(insert_sql_array);

        // Compile-time bulk INSERT prefix calculation
        static consteval size_t calculate_bulk_insert_prefix_size() {
            size_t size = 0;
            size += 12; // "INSERT INTO "
            size += Base::table_name_.size();
            size += 2; // " ("
            size += field_names_.size();
            size += 10; // ") VALUES "
            size += 1;  // null terminator
            return size;
        }

        // Build bulk INSERT prefix at compile-time using ConstexprString
        static consteval auto build_bulk_insert_prefix() {
            constexpr size_t prefix_size = calculate_bulk_insert_prefix_size() + 50; // Add buffer for safety
            ConstexprString<prefix_size> result;

            result.append("INSERT INTO ");
            result.append(Base::table_name_);
            result.append(" (");
            result.append(field_names_);
            result.append(") VALUES ");

            return result;
        }

        // Pre-computed bulk INSERT prefix generated at compile-time
        static constexpr auto           bulk_insert_prefix_array = build_bulk_insert_prefix();
        static inline const std::string bulk_insert_prefix       = std::string(bulk_insert_prefix_array);
        static constexpr size_t         bulk_insert_prefix_size =
                calculate_bulk_insert_prefix_size() - 1; // Exclude null terminator

        // Generate INSERT SQL string (compile-time computed, runtime accessible)
        static const std::string& get_insert_sql() {
            return insert_sql_string;
        }

        // Generate bulk INSERT SQL with multiple value sets (with caching)
        static std::string get_bulk_insert_sql(size_t count) {
            // Thread-local cache for common batch sizes
            static thread_local BulkSQLCache cache;

            // Check cache first
            if (const auto* cached = cache.find(count)) {
                return *cached;
            }

            // Build optimized SQL with pre-allocation
            // Pre-compute the value template once
            std::string value_template = "(";
            value_template += placeholders_;
            value_template += ")";

            // Calculate exact size needed using pre-computed prefix size
            const size_t value_size     = value_template.size();
            const size_t separator_size = 2; // ", "
            const size_t total_size = bulk_insert_prefix_size + (value_size * count) + (separator_size * (count - 1));

            // Reserve exact memory upfront
            std::string sql;
            sql.reserve(total_size);

            // Build SQL with minimal allocations using pre-computed prefix
            sql = bulk_insert_prefix;
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) {
                    sql += ", ";
                }
                sql += value_template;
            }

            // Cache the result for future use
            cache.insert(count, sql);

            return sql;
        }

      public:
        explicit InsertStatement(Connection& conn) : conn_(conn) {}

        // Batch insert operation with optimization strategies
        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<std::vector<int64_t>, Error> {
            return Base::execute_standard_batch(*this, objects, Base::field_count_);
        }

      protected: // Changed to protected so BaseStatement can access
        // Bind all fields of an object using compile-time index sequence optimization
        [[nodiscard]] auto bind_all_fields(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            return Base::template bind_all_fields_impl<ConnType, Statement>(
                    stmt, obj, typename Base::field_indices_t()
            );
        }

        // Execute bulk INSERT with multiple VALUES clauses
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<std::vector<int64_t>, Error> {
            const auto sql = get_bulk_insert_sql(objects.size());

            return Base::template execute_with_transaction<ConnType>(
                    conn_,
                    Base::should_use_transaction(objects),
                    [this, &sql, objects]() -> std::expected<std::vector<int64_t>, Error> {
                        return conn_.prepare(sql).and_then(
                                [this, objects](Statement stmt) -> std::expected<std::vector<int64_t>, Error> {
                                    // Bind all parameters for all objects using index sequence optimization
                                    if (auto result = Base::template bind_all_objects_bulk_impl<ConnType, Statement>(
                                                stmt, objects, typename Base::field_indices_t()
                                        );
                                        !result) {
                                        return std::unexpected(result.error());
                                    }

                                    // Execute the bulk insert
                                    auto exec_result = stmt.execute();
                                    if (!exec_result) {
                                        return std::unexpected(exec_result.error());
                                    }

                                    // Get the last inserted row ID
                                    // For bulk INSERT with multiple VALUES, last_insert_rowid() returns the ID of the LAST row
                                    // We need to calculate the first ID by subtracting the count
                                    int64_t last_id = conn_.last_insert_rowid();
                                    int64_t first_id = last_id - static_cast<int64_t>(objects.size()) + 1;

                                    // Generate consecutive IDs for bulk insert
                                    std::vector<int64_t> ids(objects.size());
                                    for (size_t i = 0; i < objects.size(); ++i) {
                                        ids[i] = first_id + static_cast<int64_t>(i);
                                    }

                                    return ids;
                                }
                        );
                    }
            );
        }

        // Execute individual inserts for large batches (with transaction)
        [[nodiscard]] auto execute_individual_batch(std::span<const T> objects) noexcept -> std::expected<std::vector<int64_t>, Error> {
            std::vector<int64_t> ids;
            ids.reserve(objects.size());

            auto result = Base::template execute_with_statement<ConnType>(
                    conn_, get_insert_sql(), [this, objects, &ids](auto& stmt) -> std::expected<void, Error> {
                        for (const auto& obj : objects) {
                            // Monadic composition: reset → bind → execute
                            if (auto result = Base::reset_bind_and_execute(
                                        stmt, [this, &obj](auto& s) { return bind_all_fields(s, obj); }
                                );
                                !result) {
                                return std::unexpected(result.error());
                            }
                            // Get the generated ID after each insert
                            ids.push_back(conn_.last_insert_rowid());
                        }
                        return {};
                    }
            );

            if (!result) {
                return std::unexpected(result.error());
            }

            return ids;
        }

        Connection& conn_;
    };

} // namespace storm::orm::statements