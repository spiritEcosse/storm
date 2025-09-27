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

        // Thread-local cache for bulk INSERT SQL strings
        struct BulkSQLCache {
            static constexpr size_t CACHE_SIZE = 8;
            struct Entry {
                size_t      count = 0;
                std::string sql;
            };
            std::array<Entry, CACHE_SIZE> entries{};
            size_t                        next_slot = 0; // For round-robin replacement

            const std::string* find(size_t count) const {
                for (const auto& entry : entries) {
                    if (entry.count == count && !entry.sql.empty()) {
                        return &entry.sql;
                    }
                }
                return nullptr;
            }

            void insert(size_t count, std::string sql) {
                // Try to find empty slot first
                for (auto& entry : entries) {
                    if (entry.count == 0) {
                        entry.count = count;
                        entry.sql   = std::move(sql);
                        return;
                    }
                }
                // Use round-robin replacement
                entries[next_slot].count = count;
                entries[next_slot].sql   = std::move(sql);
                next_slot                = (next_slot + 1) % CACHE_SIZE;
            }
        };

        // Generate INSERT SQL string at runtime (cached)
        static const std::string& get_insert_sql() {
            static const std::string sql =
                    std::format("INSERT INTO {} ({}) VALUES ({})", Base::table_name_, field_names_, placeholders_);
            return sql;
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

            // Calculate exact size needed
            const size_t base_size = std::format("INSERT INTO {} ({}) VALUES ", Base::table_name_, field_names_).size();
            const size_t value_size     = value_template.size();
            const size_t separator_size = 2; // ", "
            const size_t total_size     = base_size + (value_size * count) + (separator_size * (count - 1));

            // Reserve exact memory upfront
            std::string sql;
            sql.reserve(total_size);

            // Build SQL with minimal allocations
            sql = std::format("INSERT INTO {} ({}) VALUES ", Base::table_name_, field_names_);
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
        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<void, Error> {
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
        [[nodiscard]] auto execute_bulk(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            const auto sql = get_bulk_insert_sql(objects.size());

            return Base::template execute_with_transaction<ConnType>(
                    conn_,
                    Base::should_use_transaction(objects),
                    [this, &sql, objects]() -> std::expected<void, Error> {
                        return conn_.prepare(sql).and_then(
                                [this, objects](Statement stmt) -> std::expected<void, Error> {
                                    // Bind all parameters for all objects using index sequence optimization
                                    if (auto result = Base::template bind_all_objects_bulk_impl<ConnType, Statement>(
                                                stmt, objects, typename Base::field_indices_t()
                                        );
                                        !result) {
                                        return std::unexpected(result.error());
                                    }

                                    // Execute the bulk insert
                                    return stmt.execute();
                                }
                        );
                    }
            );
        }

        // Execute individual inserts for large batches (with transaction)
        [[nodiscard]] auto execute_individual_batch(std::span<const T> objects) noexcept -> std::expected<void, Error> {
            return Base::template execute_with_statement<ConnType>(
                    conn_, get_insert_sql(), [this, objects](auto& stmt) -> std::expected<void, Error> {
                        for (const auto& obj : objects) {
                            // Monadic composition: reset → bind → execute
                            if (auto result = Base::reset_bind_and_execute(
                                        stmt, [this, &obj](auto& s) { return bind_all_fields(s, obj); }
                                );
                                !result) {
                                return std::unexpected(result.error());
                            }
                        }
                        return {};
                    }
            );
        }

        Connection& conn_;
    };

} // namespace storm::orm::statements