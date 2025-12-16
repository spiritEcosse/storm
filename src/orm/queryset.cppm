module;

#include <sqlite3.h>
#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_queryset;

import storm_db_concept;
import storm_db_sqlite;
import storm_orm_statements_base;
import storm_orm_statements_remove;
import storm_orm_statements_insert;
import storm_orm_statements_select;
import storm_orm_statements_distinct;
import storm_orm_statements_update;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
import storm_orm_where;
import storm_orm_statements_aggregate;
import storm_orm_utilities;

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
import <tuple>;
import <array>;
import <meta>;

export namespace storm {

    // Default connection management for QuerySet
    // WARNING: Not thread-safe - use external synchronization in multi-threaded environments
    namespace detail {
        inline auto& get_default_connection_ptr() {
            static std::shared_ptr<db::sqlite::Connection> conn_;
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
            return get_remove_statement().execute_one(obj);
        }

        // Bulk remove operations
        std::expected<void, Error> remove(std::span<const T> objects) {
            return get_remove_statement().execute(objects);
        }

        // Insert single object (SFINAE: only accept T, not span/container)
        template <typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T>
        std::expected<int64_t, Error>
        insert(const U& obj, std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
            // Use default options if not provided
            orm::statements::InsertOptions options = opts.value_or(orm::statements::InsertOptions{});

            // Call optimized single-object method with return_id flag
            return get_insert_statement().execute_single_optimized(obj, options.return_ids);
        }

        // Bulk insert (span overload)
        std::expected<std::vector<int64_t>, Error>
        insert(std::span<const T> objects, std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
            return execute_insert(objects, opts);
        }

        // WHERE clause support - builder pattern with method chaining using type-safe expressions
        //
        // Usage examples:
        //   queryset.where(field<^^Person::age>() > 25).select()
        //   queryset.where(field<^^Person::id>().in(1, 2, 3)).select()
        //   queryset.where(field<^^Person::name>().like("A%")).select()
        //   queryset.where(field<^^Person::age>().between(25, 50)).select()
        //
        // Chaining with AND composition:
        //   queryset.where(field<^^Person::age>() > 25)
        //           .where(field<^^Person::name>() == "Alice")
        //           .select()
        //
        // Complex expressions with AND/OR:
        //   queryset.where(field<^^Person::age>() > 25 and field<^^Person::is_active>() == true).select()
        //   queryset.where((field<^^Person::age>() > 25) or (field<^^Person::name>().like("A%"))).select()
        //
        constexpr auto&& where(this auto&& self, orm::where::ExpressionVariantPtr expr) {
            if (self.where_expr_) {
                // Combine with existing expression using AND
                self.where_expr_ = orm::where::and_(self.where_expr_, expr);
            } else {
                self.where_expr_ = expr;
            }
            return self_cast(self);
        }

        // LIMIT clause support - builder pattern with method chaining
        // Usage: queryset.limit(10).select()
        //        queryset.where(age > 25).limit(10).select()
        constexpr auto&& limit(this auto&& self, int n) {
            self.limit_value_ = n;
            return self_cast(self);
        }

        // OFFSET clause support - builder pattern with method chaining
        // Usage: queryset.offset(5).select()
        //        queryset.limit(10).offset(5).select()
        constexpr auto&& offset(this auto&& self, int n) {
            self.offset_value_ = n;
            return self_cast(self);
        }

        // ORDER BY clause support - builder pattern with method chaining
        // Supports all variations:
        //   order_by<^^Person::age>()                          // Single field, default ASC
        //   order_by<^^Person::age, false>()                   // Single field, explicit DESC
        //   order_by<^^Person::age, ^^Person::name>()          // Multiple fields, all ASC
        //   order_by<^^Person::age, true, ^^Person::name, false>()  // Mixed directions
        //
        // Usage examples:
        //   queryset.order_by<^^Person::age>().select()
        //   queryset.where(age > 25).order_by<^^Person::name>().select()
        //   queryset.order_by<^^Person::age, false>().limit(10).select()
        //
        template <auto... Args> constexpr auto&& order_by(this auto&& self) {
            // Create lightweight wrapper to compile-time generated static SQL
            self.order_by_wrapper_ = orm::statements::make_order_by_wrapper<Args...>();
            return self_cast(self);
        }

        // Select operations - returns all rows (optimized with statement caching)
        // NOTE: WHERE and JOIN state is preserved after select() for query reusability.
        // Call reset() to clear state when needed.
        [[nodiscard]] __attribute__((hot)) std::expected<plf::hive<T>, Error> select() {
            std::expected<plf::hive<T>, Error> result;

            if (join_stmt_.has_value() && where_expr_) {
                // JOIN + WHERE
                result = get_select_statement().execute_with_where_and_join(
                        *join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_
                );
            } else if (join_stmt_.has_value()) {
                // JOIN only (no WHERE)
                result = get_select_statement()
                                 .execute_optimized(*join_stmt_, limit_value_, offset_value_, order_by_wrapper_);
            } else if (where_expr_) {
                // WHERE only (no JOIN)
                result = get_select_statement()
                                 .execute_with_where(where_expr_, limit_value_, offset_value_, order_by_wrapper_);
            } else {
                // Simple SELECT (no JOIN, no WHERE)
                result = get_select_statement().execute_optimized(limit_value_, offset_value_, order_by_wrapper_);
            }

            return result;
        }

        // Field-specific DISTINCT support using reflection
        // Usage:
        //   auto names = queryset.distinct<^^Person::name>().select();  // std::vector<std::string>
        //   auto pairs = queryset.distinct<^^Person::name, ^^Person::age>().select();  //
        //   std::vector<std::tuple<std::string, int>> auto ids = queryset.distinct().select();  // std::vector<int>
        //   (defaults to PK)
        //
        // OPTIMIZATION: Returns cached DistinctStatement for optimal performance
        // - Thread-local caching eliminates DistinctQuerySet wrapper overhead
        // - Statement pointer caching avoids prepare_cached() hash lookup
        // - SQL string caching avoids repeated to_sql() calls
        template <std::meta::info... FieldInfos> constexpr auto& distinct() {
            if constexpr (sizeof...(FieldInfos) == 0) {
                // Default to primary key when no fields specified
                using StmtType = orm::statements::
                        DistinctStatement<T, ConnType, orm::statements::BaseStatement<T>::primary_key_>;

                // Use TLS for per-thread, per-field-combination caching
                // Preserves statement caches across calls for optimal performance
                static thread_local std::optional<StmtType> cached_stmt;

                // Create on first use, update state on subsequent calls
                if (!cached_stmt.has_value()) {
                    cached_stmt.emplace(conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_);
                } else {
                    cached_stmt->update_state(
                            conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_
                    );
                }

                return *cached_stmt;
            } else {
                // User-specified fields
                using StmtType = orm::statements::DistinctStatement<T, ConnType, FieldInfos...>;

                // Use TLS for per-thread, per-field-combination caching
                // Preserves statement caches across calls for optimal performance
                static thread_local std::optional<StmtType> cached_stmt;

                // Create on first use, update state on subsequent calls
                if (!cached_stmt.has_value()) {
                    cached_stmt.emplace(conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_);
                } else {
                    cached_stmt->update_state(
                            conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_
                    );
                }

                return *cached_stmt;
            }
        }

        // INNER JOIN support for single or multiple FK fields
        // Usage:
        //   Single FK: message_qs.join<&Message::sender>().select()
        //   Multi FK:  message_qs.join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        constexpr auto&& join(this auto&& self) {
            // Create type-erased wrapper with compile-time generated SQL (INNER JOIN)
            self.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Inner, FKFieldPtrs...>();
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
            self.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Left, FKFieldPtrs...>();
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
            self.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Right, FKFieldPtrs...>();
            return self_cast(self);
        }

        // Update operations (SFINAE: only accept T, not span/container)
        template <typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T>
        std::expected<void, Error> update(const U& obj) {
            // Use cached UpdateStatement instance for optimal performance
            return get_update_statement().execute_single_optimized(obj);
        }

        // Bulk update operations
        std::expected<void, Error> update(std::span<const T> objects) {
            return get_update_statement().execute(objects);
        }

        // Reset WHERE, JOIN, LIMIT, and OFFSET state
        // Use this to clear conditions and start fresh with the same QuerySet instance
        // Example:
        //   auto qs = QuerySet<Person>(conn);
        //   qs.where(age > 25).limit(10).select();  // WHERE age > 25 LIMIT 10
        //   qs.reset();                              // Clear state
        //   qs.select();                             // No WHERE, no LIMIT
        void reset() noexcept {
            join_stmt_.reset();
            where_expr_.reset();
            limit_value_.reset();
            offset_value_.reset();
        }

        // Aggregate functions - fluent builder pattern for multiple aggregates
        // Usage: queryset.aggregate().sum<^^Person::age>().count().avg<^^Person::salary>().select()
        constexpr auto aggregate() {
            return orm::statements::AggregateBuilder<T, ConnType>{conn_};
        }

        // Shortcut: SUM aggregate (multi-field: SUM(f1 + f2 + ...))
        // Usage: queryset.sum<^^Person::age>().select()
        //        queryset.sum<^^Person::age, ^^Person::years>().select()  // SUM(age + years)
        template <std::meta::info... FieldInfos> constexpr auto sum() {
            return orm::statements::
                    SingleAggregateStatement<T, ConnType, orm::statements::AggregateType::SUM, FieldInfos...>{conn_};
        }

        // Shortcut: COUNT aggregate (defaults to COUNT(*) if no fields)
        // Usage: queryset.count().select()  // COUNT(*)
        //        queryset.count<^^Person::id>().select()  // COUNT(id)
        template <std::meta::info... FieldInfos> constexpr auto count() {
            return orm::statements::
                    SingleAggregateStatement<T, ConnType, orm::statements::AggregateType::COUNT, FieldInfos...>{conn_};
        }

        // Shortcut: AVG aggregate (multi-field: AVG(f1 + f2 + ...))
        // Usage: queryset.avg<^^Person::salary>().select()
        template <std::meta::info... FieldInfos> constexpr auto avg() {
            return orm::statements::
                    SingleAggregateStatement<T, ConnType, orm::statements::AggregateType::AVG, FieldInfos...>{conn_};
        }

        // Shortcut: MIN aggregate (multi-field: MIN(f1 + f2 + ...))
        // Usage: queryset.min<^^Person::age>().select()
        template <std::meta::info... FieldInfos> constexpr auto min() {
            return orm::statements::
                    SingleAggregateStatement<T, ConnType, orm::statements::AggregateType::MIN, FieldInfos...>{conn_};
        }

        // Shortcut: MAX aggregate (multi-field: MAX(f1 + f2 + ...))
        // Usage: queryset.max<^^Person::age>().select()
        template <std::meta::info... FieldInfos> constexpr auto max() {
            return orm::statements::
                    SingleAggregateStatement<T, ConnType, orm::statements::AggregateType::MAX, FieldInfos...>{conn_};
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

            auto conn_ptr = std::make_shared<db::sqlite::Connection>(std::move(conn_result.value()));

            // Pre-populate statement cache for better performance
            conn_ptr->prepare_common_statements();

            detail::get_default_connection_ptr() = conn_ptr;
            return {};
        }

        [[nodiscard]] static auto get_default_connection() -> const std::shared_ptr<ConnType>& {
            if (!detail::get_default_connection_ptr()) {
                throw std::runtime_error(
                        "Default database connection not set. Call QuerySet::set_default_connection() first."
                );
            }
            return detail::get_default_connection_ptr();
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

        [[nodiscard]] std::expected<std::vector<int64_t>, Error> execute_insert(
                std::span<const T> objects, std::optional<orm::statements::InsertOptions> opts = std::nullopt
        ) const noexcept {
            return get_insert_statement().execute(objects, opts);
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

        std::shared_ptr<ConnType>                                              conn_;
        mutable std::unique_ptr<orm::statements::InsertStatement<T, ConnType>> insert_stmt_;
        mutable std::unique_ptr<orm::statements::RemoveStatement<T, ConnType>> remove_stmt_;
        mutable std::unique_ptr<orm::statements::SelectStatement<T, ConnType>> select_stmt_;
        mutable std::unique_ptr<orm::statements::UpdateStatement<T, ConnType>> update_stmt_;

        mutable std::optional<orm::statements::JoinStatementWrapper> join_stmt_;
        mutable orm::where::ExpressionVariantPtr                     where_expr_;
        mutable std::optional<int>                                   limit_value_;
        mutable std::optional<int>                                   offset_value_;
        mutable std::optional<orm::statements::OrderByWrapper>       order_by_wrapper_;
    };

} // namespace storm