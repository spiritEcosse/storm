module;

#include <cassert>
#include <meta>
#include <plf_hive/plf_hive.h>

export module storm_orm_queryset;

import storm_db_concept;
import storm_db_sqlite;
import storm_db_pool;
import storm_orm_statements_base;
import storm_orm_statements_erase;
import storm_orm_statements_insert;
import storm_orm_statements_select;
import storm_orm_statements_projection;
import storm_orm_statements_update;
import storm_orm_statements_join;
import storm_orm_statements_orderby;
import storm_orm_where;
import storm_orm_statements_aggregate;
import storm_orm_statements_setop;
import storm_orm_utilities;

import <expected>;
import <string>;
import <string_view>;
import <span>;
import <concepts>;
import <memory>;
import <variant>;
import <optional>;
import <functional>;
import <tuple>;
import <array>;
import <meta>;

export namespace storm {

    // Default connection management for QuerySet
    // Thread-safe via thread_local - each thread gets its own connection
    namespace detail {
        template <typename ConnType> inline auto get_default_connection_ptr() -> auto& {
            static thread_local std::shared_ptr<ConnType> conn_;
            return conn_;
        }
    } // namespace detail

    template <
            class T,
            storm::db::DatabaseConnection ConnType   = storm::db::sqlite::Connection,
            bool                          Finalized_ = false>
    class QuerySet { // NOSONAR(cpp:S1448) — ORM facade class; method count grows with supported operations
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        template <class U, storm::db::DatabaseConnection C, bool F> friend class QuerySet;

      public:
        // Default constructor using default connection
        QuerySet() : conn_(get_default_connection()) {}

        // LCOV_EXCL_START — compile-time only, used in static_assert
        static constexpr auto is_finalized() -> bool {
            return Finalized_;
        }
        // LCOV_EXCL_STOP

        // Erase single object - returns proxy with .execute() and .to_sql()
        auto erase(const T& obj) {
            return get_erase_statement().query(obj);
        }

        // Bulk erase - returns proxy with .execute() and .to_sql()
        auto erase(std::span<const T> objects) {
            return get_erase_statement().query(objects);
        }

        // Erase all rows — executes DELETE FROM <table> with no WHERE clause
        [[nodiscard]] auto erase_all() {
            return get_erase_statement().query_all();
        }

        // Insert single object - returns proxy with .execute() and .to_sql()
        // ReturnId::Yes (default): .execute() returns std::expected<int64_t, Error>
        // ReturnId::No: .execute() returns std::expected<void, Error> (faster, no RETURNING clause)
        // (SFINAE: only accept T, not span/container)
        template <orm::statements::ReturnId R = orm::statements::ReturnId::Yes, typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T>
        auto insert(const U& obj) {
            return get_insert_statement().template query<R>(obj);
        }

        // Bulk insert - returns proxy with .execute() and .to_sql()
        // Default (no template arg): .execute() returns std::expected<void, Error>
        // ReturnId::Yes: .execute() returns std::expected<std::vector<int64_t>, Error>
        auto insert(std::span<const T> objects, std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
            return get_insert_statement().query(objects, opts);
        }
        template <orm::statements::ReturnId R>
        auto insert(std::span<const T> objects, std::optional<orm::statements::InsertOptions> opts = std::nullopt) {
            return get_insert_statement().template query<R>(objects, opts);
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
        [[nodiscard]] auto where(orm::where::ExpressionVariantPtr expr) const -> QuerySet {
            auto result = clone_state();
            if (result.where_expr_) {
                // Combine with existing expression using AND
                result.where_expr_ = std::make_shared<orm::where::ExpressionVariant>(
                        orm::where::LogicalExpr{result.where_expr_, orm::where::LogicalOp::And, expr}
                );
            } else {
                result.where_expr_ = expr;
            }
            return result;
        }

        // LIMIT clause support - returns finalized QuerySet (prevents use as set-op operand)
        // Usage: queryset.limit(10).select()
        //        queryset.where(age > 25).limit(10).select()
        [[nodiscard]] constexpr auto limit(int n) -> QuerySet<T, ConnType, true> {
            auto result         = to_finalized();
            result.limit_value_ = n;
            return result;
        }

        // OFFSET clause support - returns finalized QuerySet (prevents use as set-op operand)
        // Usage: queryset.offset(5).select()
        //        queryset.limit(10).offset(5).select()
        [[nodiscard]] constexpr auto offset(int n) -> QuerySet<T, ConnType, true> {
            auto result          = to_finalized();
            result.offset_value_ = n;
            return result;
        }

        // ORDER BY clause support - returns finalized QuerySet (prevents use as set-op operand)
        // Supports single/multiple fields with ASC (default) or DESC direction
        template <auto... Args> [[nodiscard]] constexpr auto order_by() -> QuerySet<T, ConnType, true> {
            auto result              = to_finalized();
            result.order_by_wrapper_ = orm::statements::make_order_by_wrapper<Args...>();
            return result;
        }

        // Select - returns proxy with .execute() and .to_sql()
        // NOTE: WHERE and JOIN state is preserved after select() for query reusability.
        // Call reset() to clear state when needed.
        [[nodiscard]] __attribute__((hot)) auto select() {
            return get_select_statement()
                    .query(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_);
        }

        // Lazy row-by-row iteration via std::generator — no full materialization
        // Each iteration yields std::expected<T, Error> — one row at a time from DB
        // Uses a dedicated (non-cached) prepared statement per generator instance
        // Usage: for (auto&& result : qs.where(age > 30).rows()) { ... }
        [[nodiscard]] auto rows() {
            return get_select_statement()
                    .rows_generator(conn_, join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_);
        }

        // First - returns proxy with .execute() and .to_sql()
        // Automatically applies LIMIT 1 to the query for optimal performance
        // Usage: auto result = queryset.where(age > 30).first().execute();
        [[nodiscard]] __attribute__((hot)) auto first() {
            const bool fast = !where_expr_ && !join_stmt_.has_value() && !order_by_wrapper_.has_value() &&
                              !offset_value_.has_value();
            return get_select_statement()
                    .query_first(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_, fast);
        }

        // Get - returns proxy with .execute() and .to_sql()
        // Returns exactly one matching row, or an error if 0 or >1 rows match
        // Does NOT apply LIMIT — validates uniqueness of the result (like Django's get())
        // Uses LIMIT 2 internally for efficient duplicate detection without plf::hive allocation
        // Usage: auto result = queryset.where(id == 42).get().execute();
        [[nodiscard]] __attribute__((hot)) auto get() {
            const bool fast = !where_expr_ && !join_stmt_.has_value() && !order_by_wrapper_.has_value() &&
                              !offset_value_.has_value();
            return get_select_statement()
                    .query_get(join_stmt_, where_expr_, limit_value_, offset_value_, order_by_wrapper_, fast);
        }

        // Field-specific DISTINCT support using reflection
        // Usage:
        //   auto names = queryset.distinct<^^Person::name>().execute();  // plf::hive<std::string>
        //   auto pairs = queryset.distinct<^^Person::name, ^^Person::age>().execute();
        //   // Returns plf::hive<std::tuple<std::string, int>>
        //   auto ids = queryset.distinct().execute();  // plf::hive<int> (defaults to PK)
        //
        // OPTIMIZATION: Returns cached DistinctStatement for optimal performance
        // Returns DistinctStatement by value - connection-level prepare_cached() handles SQL caching
        // No static thread_local needed since actual statement caching is at connection level
        template <std::meta::info... FieldInfos> constexpr auto distinct() {
            if constexpr (sizeof...(FieldInfos) == 0) {
                using StmtType = orm::statements::
                        DistinctStatement<T, ConnType, orm::statements::BaseStatement<T>::primary_key_>;
                return StmtType{conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_};
            } else {
                using StmtType = orm::statements::DistinctStatement<T, ConnType, FieldInfos...>;
                return StmtType{conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_};
            }
        }

        // Column projection support using reflection (SELECT specific columns)
        // Usage:
        //   auto names = queryset.values<^^Person::name>().execute();  // plf::hive<std::string>
        //   auto pairs = queryset.values<^^Person::name, ^^Person::age>().execute();
        //   // Returns plf::hive<std::tuple<std::string, int>>
        //
        // Unlike distinct(), values() does NOT apply DISTINCT — all rows are returned
        // including duplicates. Works with WHERE, JOIN, ORDER BY, LIMIT/OFFSET.
        template <std::meta::info... FieldInfos>
            requires(sizeof...(FieldInfos) > 0)
        constexpr auto values() {
            using StmtType = orm::statements::ValuesStatement<T, ConnType, FieldInfos...>;
            return StmtType{conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_};
        }

        // INNER JOIN support for single or multiple FK fields
        // Immutable: returns a new QuerySet with the join attached (Django-style).
        // Usage:
        //   Single FK: message_qs.join<&Message::sender>().select()
        //   Multi FK:  message_qs.join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        [[nodiscard]] auto join() const -> QuerySet {
            auto result = clone_state();
            result.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Inner, FKFieldPtrs...>();
            return result;
        }

        // LEFT JOIN support for single or multiple FK fields
        // Immutable: returns a new QuerySet with the join attached.
        // Usage:
        //   Single FK: message_qs.left_join<&Message::sender>().select()
        //   Multi FK:  message_qs.left_join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1)
        [[nodiscard]] auto left_join() const -> QuerySet {
            auto result = clone_state();
            result.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Left, FKFieldPtrs...>();
            return result;
        }

        // RIGHT JOIN support for single or multiple FK fields
        // Immutable: returns a new QuerySet with the join attached.
        // Requires backend support (SQLite 3.39.0+, PostgreSQL always)
        // Usage:
        //   Single FK: message_qs.right_join<&Message::sender>().select()
        //   Multi FK:  message_qs.right_join<&Message::sender, &Message::receiver>().select()
        template <auto... FKFieldPtrs>
            requires(sizeof...(FKFieldPtrs) >= 1 && ConnType::supports_right_join)
        [[nodiscard]] auto right_join() const -> QuerySet {
            auto result = clone_state();
            result.join_stmt_ =
                    orm::statements::make_join_wrapper<T, ConnType, orm::statements::JoinType::Right, FKFieldPtrs...>();
            return result;
        }

        // Update single object - returns proxy with .execute() and .to_sql()
        // (SFINAE: only accept T, not span/container)
        template <typename U = T>
            requires std::same_as<std::remove_cvref_t<U>, T>
        auto update(const U& obj) {
            return get_update_statement().query(obj);
        }

        // Bulk update - returns proxy with .execute() and .to_sql()
        auto update(std::span<const T> objects) {
            return get_update_statement().query(objects);
        }

        // Reset WHERE, JOIN, LIMIT, and OFFSET state
        // Use this to clear conditions and start fresh with the same QuerySet instance
        // Example:
        //   auto qs = QuerySet<Person>(conn);
        //   qs.where(age > 25).limit(10).select();  // WHERE age > 25 LIMIT 10
        //   qs.reset();                              // Clear state
        //   qs.select();                             // No WHERE, no LIMIT
        auto reset() noexcept -> void {
            join_stmt_.reset();
            where_expr_.reset();
            limit_value_.reset();
            offset_value_.reset();
            order_by_wrapper_.reset();
            // Invalidate SelectStatement's cache to ensure fresh query on next execute
            if (select_stmt_) {
                select_stmt_->invalidate_cache();
            }
        }

        // GROUP BY - returns GroupByBuilder for fluent aggregate chaining
        // Usage:
        //   qs.group_by<^^Person::department>().count().execute()    // grouped → .execute()
        //   qs.group_by<^^Person::dept, ^^Person::role>().sum<^^Person::salary>().execute()
        //   qs.where(age > 25).group_by<^^Person::years_exp>().count().execute()
        template <std::meta::info... GroupFieldInfos>
            requires(sizeof...(GroupFieldInfos) > 0)
        auto group_by() {
            return orm::statements::GroupByBuilder<T, ConnType, GroupFieldInfos...>{
                    {conn_, where_expr_, join_stmt_, limit_value_, offset_value_, order_by_wrapper_, nullptr}
            };
        }
        // =====================================================================
        // SET OPERATIONS: UNION, UNION ALL, EXCEPT, INTERSECT
        // =====================================================================

        template <bool F_ = Finalized_>
        auto union_(const QuerySet<T, ConnType, false>& other)
            requires(!F_)
        {
            return make_setop_builder(other, orm::statements::SetOpType::Union);
        }

        template <bool F_ = Finalized_>
        auto union_all(const QuerySet<T, ConnType, false>& other)
            requires(!F_)
        {
            return make_setop_builder(other, orm::statements::SetOpType::UnionAll);
        }

        template <bool F_ = Finalized_>
        auto except_(const QuerySet<T, ConnType, false>& other)
            requires(!F_)
        {
            return make_setop_builder(other, orm::statements::SetOpType::Except);
        }

        template <bool F_ = Finalized_>
        auto intersect_(const QuerySet<T, ConnType, false>& other)
            requires(!F_)
        {
            return make_setop_builder(other, orm::statements::SetOpType::Intersect);
        }

        template <bool F_ = Finalized_>
        auto capture_operand() const -> orm::statements::SetOpOperand<T>
            requires(!F_)
        {
            std::string sql = join_stmt_.has_value() ? std::string(join_stmt_->get_complete_sql())
                                                     : orm::statements::SelectStatement<T, ConnType>::get_select_sql();
            if (where_expr_) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr_);
            }
            return {std::move(sql), where_expr_};
        }

        // SUM aggregate (multi-field: SUM(f1 + f2 + ...))
        // Supports WHERE and JOIN clauses
        // Usage: queryset.sum<^^Person::age>().execute()
        //        queryset.where(age > 30).sum<^^Person::age>().execute()
        //        queryset.join<FK>().sum<^^Person::salary>().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto sum() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::SUM, FieldInfos...>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // COUNT aggregate (defaults to COUNT(*) if no fields)
        // Supports WHERE and JOIN clauses
        // Usage: queryset.count().execute()  // COUNT(*)
        //        queryset.where(age > 30).count().execute()
        //        queryset.join<FK>().count().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto count() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::COUNT, FieldInfos...>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // AVG aggregate (multi-field: AVG(f1 + f2 + ...))
        // Supports WHERE and JOIN clauses
        // Usage: queryset.avg<^^Person::salary>().execute()
        //        queryset.where(department == "Engineering").avg<^^Person::salary>().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto avg() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::AVG, FieldInfos...>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // MIN aggregate (multi-field: MIN(f1 + f2 + ...))
        // Supports WHERE and JOIN clauses
        // Usage: queryset.min<^^Person::age>().execute()
        //        queryset.where(active == true).min<^^Person::age>().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto min() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::MIN, FieldInfos...>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // MAX aggregate (multi-field: MAX(f1 + f2 + ...))
        // Supports WHERE and JOIN clauses
        // Usage: queryset.max<^^Person::age>().execute()
        //        queryset.where(department == "Sales").max<^^Person::salary>().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info... FieldInfos> auto max() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::MAX, FieldInfos...>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // COUNT(DISTINCT field) aggregate
        // Supports WHERE and JOIN clauses
        // Usage: queryset.count_distinct<^^Person::age>().execute()
        //        queryset.where(active == true).count_distinct<^^Person::department>().execute()
        // Returns statement by value - connection-level prepare_cached() handles SQL caching
        template <std::meta::info FieldInfo> auto count_distinct() {
            using StmtType = orm::statements::AggregateStatement<
                    T,
                    ConnType,
                    orm::statements::NoGroupBy,
                    orm::statements::AggregateOp<orm::statements::AggregateType::COUNT_DISTINCT, FieldInfo>>;
            return StmtType{{conn_, where_expr_, join_stmt_, {}, {}, {}, nullptr}};
        }

        // Static methods for connection management
        // NOTE: These methods are NOT thread-safe. For multi-threaded use:
        // 1. Use external synchronization (mutex/lock) around these calls, OR
        // 2. Create QuerySet with explicit connection per thread
        [[nodiscard]] static auto set_default_connection(std::string_view db_path)
                -> std::expected<void, typename ConnType::Error> {
            auto conn_result = ConnType::open(db_path);
            if (!conn_result) {
                return std::unexpected(conn_result.error());
            }

            auto conn_ptr = std::make_shared<ConnType>(std::move(conn_result.value()));

            // Pre-populate statement cache for better performance
            if constexpr (requires { conn_ptr->prepare_common_statements(); }) {
                conn_ptr->prepare_common_statements();
            }

            detail::get_default_connection_ptr<ConnType>() = conn_ptr;
            return {};
        }

        // Set default connection from a connection pool
        // Checks out a connection from the pool and sets it as the thread-local default.
        // When clear_default_connection() is called, the custom deleter returns it to the pool.
        [[nodiscard]] static auto set_default_connection(storm::db::ConnectionPool<ConnType>& pool)
                -> std::expected<void, typename ConnType::Error> {
            auto conn = pool.checkout();
            if (!conn) {
                return std::unexpected(conn.error());
            }
            detail::get_default_connection_ptr<ConnType>() = std::move(*conn);
            return {};
        }

        [[nodiscard]] static auto get_default_connection() -> const std::shared_ptr<ConnType>& {
            assert(detail::get_default_connection_ptr<ConnType>() &&
                   "Default database connection not set. Call QuerySet::set_default_connection() first.");
            return detail::get_default_connection_ptr<ConnType>();
        }

        [[nodiscard]] static auto has_default_connection() noexcept -> bool {
            return static_cast<bool>(detail::get_default_connection_ptr<ConnType>());
        }

        static auto clear_default_connection() noexcept -> void {
            detail::get_default_connection_ptr<ConnType>().reset();
        }

        [[nodiscard]] auto to_finalized() const -> QuerySet<T, ConnType, true> {
            QuerySet<T, ConnType, true> result(conn_);
            copy_state_into(result);
            return result;
        }

      private:
        // Lazy-initialize and return cached InsertStatement for optimal performance
        auto get_insert_statement() const -> orm::statements::InsertStatement<T, ConnType>& {
            if (!insert_stmt_) [[unlikely]] {
                insert_stmt_ = std::make_unique<orm::statements::InsertStatement<T, ConnType>>(conn_);
            }
            return *insert_stmt_;
        }

        // Lazy-initialize and return cached EraseStatement for optimal performance
        auto get_erase_statement() const -> orm::statements::EraseStatement<T, ConnType>& {
            if (!erase_stmt_) [[unlikely]] {
                erase_stmt_ = std::make_unique<orm::statements::EraseStatement<T, ConnType>>(conn_);
            }
            return *erase_stmt_;
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

        explicit QuerySet(std::shared_ptr<ConnType> conn) : conn_(std::move(conn)) {}

        template <bool F2> auto copy_state_into(QuerySet<T, ConnType, F2>& dst) const -> void {
            dst.where_expr_       = where_expr_;
            dst.join_stmt_        = join_stmt_;
            dst.limit_value_      = limit_value_;
            dst.offset_value_     = offset_value_;
            dst.order_by_wrapper_ = order_by_wrapper_;
        }

        auto clone_state() const -> QuerySet {
            QuerySet result(conn_);
            copy_state_into(result);
            return result;
        }

        auto make_setop_builder(const QuerySet<T, ConnType, false>& other, orm::statements::SetOpType op)
                -> orm::statements::SetOpBuilder<T, ConnType> {
            auto                                          left  = capture_operand();
            auto                                          right = other.capture_operand();
            std::vector<orm::statements::SetOpOperand<T>> operands;
            operands.push_back(std::move(left));
            operands.push_back(std::move(right));
            std::vector<orm::statements::SetOpType> operators;
            operators.push_back(op);
            return {conn_, std::move(operands), std::move(operators)};
        }

        std::shared_ptr<ConnType>                                              conn_;
        mutable std::unique_ptr<orm::statements::InsertStatement<T, ConnType>> insert_stmt_;
        mutable std::unique_ptr<orm::statements::EraseStatement<T, ConnType>>  erase_stmt_;
        mutable std::unique_ptr<orm::statements::SelectStatement<T, ConnType>> select_stmt_;
        mutable std::unique_ptr<orm::statements::UpdateStatement<T, ConnType>> update_stmt_;

        mutable std::optional<orm::statements::JoinStatementWrapper> join_stmt_;
        mutable orm::where::ExpressionVariantPtr                     where_expr_;
        mutable std::optional<int>                                   limit_value_;
        mutable std::optional<int>                                   offset_value_;
        mutable std::optional<orm::statements::OrderByWrapper>       order_by_wrapper_;
    };

} // namespace storm