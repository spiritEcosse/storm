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
import storm_orm_statements_update;
import storm_orm_statements_join;

import <expected>;
import <string>;
import <string_view>;
import <span>;
import <concepts>;
import <memory>;
import <vector>;
import <variant>;
import <optional>;
import <functional>;
import <meta>;

export namespace storm {

    // Default connection management for QuerySet
    // WARNING: Not thread-safe - use external synchronization in multi-threaded environments
    namespace detail {
        inline auto& get_default_connection_ptr() {
            static std::unique_ptr<db::sqlite::Connection> conn_;
            return conn_;
        }
    } // namespace detail

    // Forward declaration for DistinctQuery
    template <class T, storm::db::DatabaseConnection ConnType, auto FieldPtr> class DistinctQuery;

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
            if (join_stmt_) {
                // Pass wrapper by value (lightweight - just 3 function pointers)
                auto result = get_select_statement().execute_optimized(*join_stmt_);
                // Reset to allow reuse for non-JOIN queries (maintains original semantics)
                join_stmt_.reset();
                return result;
            }
            // Normal SELECT without JOIN
            return get_select_statement().execute_optimized();
        }

        // Field-specific DISTINCT support
        // Usage:
        //   auto names = queryset.distinct<&Person::name>().select();  // std::vector<std::string>
        //   auto ids = queryset.distinct().select();  // std::vector<int> (defaults to PK)
        template <auto FieldPtr = orm::statements::BaseStatement<T>::primary_key_>
        constexpr auto distinct() {
            return DistinctQuery<T, ConnType, FieldPtr>{conn_};
        }

        // INNER JOIN support for single or multiple FK fields
        // Usage:
        //   Single FK: message_qs.join<&Message::sender>().select()
        //   Multi FK:  message_qs.join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        constexpr auto&& join(this auto&& self) {
            // Create type-erased wrapper with compile-time generated SQL (INNER JOIN)
            self.join_stmt_ = orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Inner, FKFieldPtrs...>();
            return self_cast(self);
        }

        // LEFT JOIN support for single or multiple FK fields
        // Usage:
        //   Single FK: message_qs.left_join<&Message::sender>().select()
        //   Multi FK:  message_qs.left_join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        constexpr auto&& left_join(this auto&& self) {
            // Create type-erased wrapper with compile-time generated SQL (LEFT JOIN)
            self.join_stmt_ = orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Left, FKFieldPtrs...>();
            return self_cast(self);
        }

        // RIGHT JOIN support for single or multiple FK fields
        // Usage:
        //   Single FK: message_qs.right_join<&Message::sender>().select()
        //   Multi FK:  message_qs.right_join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        constexpr auto&& right_join(this auto&& self) {
            // Create type-erased wrapper with compile-time generated SQL (RIGHT JOIN)
            self.join_stmt_ = orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Right, FKFieldPtrs...>();
            return self_cast(self);
        }

        // Update operations
        std::expected<void, Error> update(const T& obj) {
            // Use cached UpdateStatement instance for optimal performance
            return get_update_statement().execute_single_optimized(obj);
        }

        // Bulk update operations
        std::expected<void, Error> update(std::span<const T> objects) {
            return get_update_statement().execute(objects);
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
        // Helper for perfect forwarding in method chaining (deducing this pattern)
        template <typename Self> static constexpr decltype(auto) self_cast(Self&& self) {
            return std::forward<Self>(self);
        }

        // Lazy-initialize and return cached InsertStatement for optimal performance
        auto get_insert_statement() const -> orm::statements::InsertStatement<T, ConnType>& {
            if (!insert_stmt_) [[unlikely]] {
                insert_stmt_ = std::make_unique<orm::statements::InsertStatement<T, ConnType>>(conn_);
            }
            return *insert_stmt_;
        }

        [[nodiscard]] std::expected<std::vector<int64_t>, Error>
        execute_insert(std::span<const T> objects) const noexcept {
            return get_insert_statement().execute(objects);
        }

        // Lazy-initialize and return cached RemoveStatement for optimal performance
        auto get_remove_statement() const -> orm::statements::RemoveStatement<T, ConnType>& {
            if (!remove_stmt_) [[unlikely]] {
                remove_stmt_ = std::make_unique<orm::statements::RemoveStatement<T, ConnType>>(conn_);
            }
            return *remove_stmt_;
        }

        // Lazy-initialize and return cached UpdateStatement for optimal performance
        auto get_update_statement() const -> orm::statements::UpdateStatement<T, ConnType>& {
            if (!update_stmt_) [[unlikely]] {
                update_stmt_ = std::make_unique<orm::statements::UpdateStatement<T, ConnType>>(conn_);
            }
            return *update_stmt_;
        }

        // Lazy-initialize and return cached SelectStatement for optimal performance
        auto get_select_statement() const -> orm::statements::SelectStatement<T, ConnType>& {
            if (!select_stmt_) [[unlikely]] {
                select_stmt_ = std::make_unique<orm::statements::SelectStatement<T, ConnType>>(conn_);
            }
            return *select_stmt_;
        }

        ConnType&                                                              conn_;
        mutable std::unique_ptr<orm::statements::InsertStatement<T, ConnType>> insert_stmt_;
        mutable std::unique_ptr<orm::statements::RemoveStatement<T, ConnType>> remove_stmt_;
        mutable std::unique_ptr<orm::statements::SelectStatement<T, ConnType>> select_stmt_;
        mutable std::unique_ptr<orm::statements::UpdateStatement<T, ConnType>> update_stmt_;

        mutable std::optional<orm::statements::JoinStatementWrapper> join_stmt_;
    };

    // DistinctQuery - helper for field-specific DISTINCT queries
    template <class T, storm::db::DatabaseConnection ConnType, auto FieldPtr>
    class DistinctQuery {
        using Error = typename ConnType::Error;

        // Check if FieldPtr is a meta::info or a member pointer
        static constexpr bool is_meta_info = std::is_same_v<decltype(FieldPtr), std::meta::info>;

        // Get member_info_ based on whether FieldPtr is meta::info or member pointer
        static consteval std::meta::info get_member_info() {
            if constexpr (is_meta_info) {
                // FieldPtr is already a meta::info (default PK case)
                return FieldPtr;
            } else {
                // FieldPtr is a member pointer - find matching meta::info by type
                using MemberPtrFieldType = std::remove_cvref_t<decltype(std::declval<T>().*FieldPtr)>;
                auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

                // Get the primary key to exclude it from matching (unless it's the only match)
                using Base = orm::statements::BaseStatement<T>;
                constexpr auto pk = Base::primary_key_;

                // Normalize the target field type
                constexpr auto target_type = std::meta::dealias(^^MemberPtrFieldType);

                // First pass: find matching type that's NOT the primary key
                for (auto member : members) {
                    auto field_type = std::meta::dealias(std::meta::type_of(member));
                    if (field_type == target_type && member != pk) {
                        return member;
                    }
                }

                // Second pass: if no non-PK match, allow PK match
                for (auto member : members) {
                    auto field_type = std::meta::dealias(std::meta::type_of(member));
                    if (field_type == target_type) {
                        return member;
                    }
                }

                // Fallback to first member (should never reach here)
                return members[0];
            }
        }

        static constexpr auto member_info_ = get_member_info();

        // Deduce field type from member_info_
        using FieldType = std::remove_cvref_t<decltype(std::declval<T>().[:member_info_:])>;

      public:
        explicit DistinctQuery(ConnType& conn) : conn_(conn) {}

        // Execute DISTINCT query on the specified field
        [[nodiscard]] auto select() -> std::expected<std::vector<FieldType>, Error> {
            // Build SQL: SELECT DISTINCT field_name FROM table_name
            using Base = orm::statements::BaseStatement<T>;

            std::string field_name;

            // Check if this is a FK field - if so, use column name (field_name_id)
            constexpr auto field_attr = std::meta::annotation_of_type<orm::statements::meta::FieldAttr>(member_info_);
            if constexpr (field_attr.has_value() && field_attr.value() == orm::statements::meta::FieldAttr::fk) {
                field_name = std::string(std::meta::identifier_of(member_info_)) + "_id";
            } else {
                field_name = std::meta::identifier_of(member_info_);
            }

            std::string sql = "SELECT DISTINCT " + field_name + " FROM " + std::string(Base::table_name_);

            // Prepare and execute statement
            auto prepare_result = conn_.prepare(sql);
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }

            auto stmt = std::move(prepare_result.value());
            std::vector<FieldType> results;
            results.reserve(100); // Initial capacity

            // Fetch rows
            using Statement = typename ConnType::Statement;
            while (true) {
                int step_result = stmt.step_raw();
                if (step_result == Statement::ROW_AVAILABLE) {
                    // Extract field value based on type
                    FieldType value;
                    if constexpr (std::is_same_v<FieldType, int>) {
                        value = stmt.extract_int(0);
                    } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                         std::is_same_v<FieldType, long long>) {
                        value = static_cast<FieldType>(stmt.extract_int64(0));
                    } else if constexpr (std::is_same_v<FieldType, double>) {
                        value = stmt.extract_double(0);
                    } else if constexpr (std::is_same_v<FieldType, float>) {
                        value = stmt.extract_float(0);
                    } else if constexpr (std::is_same_v<FieldType, bool>) {
                        value = stmt.extract_bool(0);
                    } else if constexpr (std::is_same_v<FieldType, std::string>) {
                        const unsigned char* text = stmt.extract_text_ptr(0);
                        if (text) {
                            int len = sqlite3_column_bytes(stmt.handle(), 0);
                            value.assign(reinterpret_cast<const char*>(text), len);
                        } else {
                            value = std::string();
                        }
                    } else {
                        static_assert(std::is_same_v<FieldType, int> || std::is_same_v<FieldType, std::string>,
                                      "Unsupported field type for DISTINCT");
                    }
                    results.push_back(std::move(value));
                } else if (step_result == Statement::NO_MORE_ROWS) {
                    break;
                } else {
                    return std::unexpected(Error{step_result, stmt.get_error_message()});
                }
            }

            return results;
        }

      private:
        ConnType& conn_;
    };

    // Factory function for convenient QuerySet creation with default connection
    template <typename T> [[nodiscard]] auto make_queryset() -> QuerySet<T> {
        return QuerySet<T>{};
    }

} // namespace storm