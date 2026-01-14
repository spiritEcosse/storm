#pragma once

/**
 * Unified SELECT Benchmark - Compile-Time Feature Configuration
 *
 * Single class handles all SELECT variations:
 * - Basic SELECT (no filters)
 * - SELECT + WHERE
 * - SELECT + JOIN
 * - SELECT + WHERE + JOIN
 *
 * Features are configured via template parameters at compile time.
 * Uses `if constexpr` for zero-overhead feature dispatch.
 *
 * Usage:
 *   // WHERE only
 *   SelectBenchmark<Person, NoJoin, WhereConfig<^^Person::age, ">", int>>{30, 1000}
 *
 *   // JOIN only
 *   SelectBenchmark<FKMessage, JoinConfig<&FKMessage::sender, User>, NoWhere>{1000}
 *
 *   // WHERE + JOIN
 *   SelectBenchmark<FKMessage, JoinConfig<&FKMessage::sender, User>,
 *                   WhereConfig<^^User::age, ">", int>>{30, 1000}
 *
 *   // Builder pattern (returns new types)
 *   SelectBenchmark<Person>(1000)
 *       .with_join<&FKMessage::sender, User>()
 *       .with_where<^^User::age, ">">(30)
 */

#include "select_query_base.hpp"
#include <format>
#include <plf_hive/plf_hive.h>
#include <string>

namespace storm::benchmark {

    // ========================================================================
    // Unified SelectBenchmark - Inherits from SelectQueryBenchmarkBase
    // ========================================================================
    template <
            typename BaseModel,
            typename JoinCfg        = NoJoin,
            typename WhereCfg       = NoWhere,
            typename LimitOffsetCfg = NoLimitOffset,
            typename OrderByCfg     = NoOrderBy,
            typename GroupByCfg     = NoGroupBy>
    class SelectBenchmark
        : public SelectQueryBenchmarkBase<
                  SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, GroupByCfg>,
                  BaseModel,
                  JoinCfg,
                  WhereCfg,
                  LimitOffsetCfg,
                  OrderByCfg,
                  GroupByCfg> {
        using Base = SelectQueryBenchmarkBase<
                SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, GroupByCfg>,
                BaseModel,
                JoinCfg,
                WhereCfg,
                LimitOffsetCfg,
                OrderByCfg,
                GroupByCfg>;

      public:
        // ====================================================================
        // Constructors - delegate to base
        // ====================================================================

        // Basic SELECT (no WHERE) - dataset_size only
        explicit constexpr SelectBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // SELECT with WHERE - value + dataset_size
        template <typename V>
        explicit constexpr SelectBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(value, dataset_size) {}

        // ====================================================================
        // Builder Methods (return NEW types for compile-time chaining)
        // ====================================================================

        // Add JOIN - returns new SelectBenchmark type with JOIN enabled
        template <auto FK, typename Related> auto with_join() const {
            using NewJoinCfg = JoinConfig<FK, Related>;

            if constexpr (WhereCfg::enabled) {
                return SelectBenchmark<BaseModel, NewJoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, GroupByCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return SelectBenchmark<BaseModel, NewJoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, GroupByCfg>{
                        Base::batch_size()
                };
            }
        }

        // Add WHERE - returns new SelectBenchmark type with WHERE enabled
        template <std::meta::info FieldInfo, auto Op, typename V> auto with_where(V value) const {
            using NewWhereCfg = WhereConfig<FieldInfo, Op, V>;
            return SelectBenchmark<BaseModel, JoinCfg, NewWhereCfg, LimitOffsetCfg, OrderByCfg, GroupByCfg>{
                    value, Base::batch_size()
            };
        }

        // Add LIMIT/OFFSET - returns new SelectBenchmark type with LIMIT/OFFSET enabled
        template <int LimitValue, int OffsetValue = 0> auto with_limit_offset() const {
            using NewLimitOffsetCfg = LimitOffsetConfig<LimitValue, OffsetValue>;
            if constexpr (WhereCfg::enabled) {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, NewLimitOffsetCfg, OrderByCfg, GroupByCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, NewLimitOffsetCfg, OrderByCfg, GroupByCfg>{
                        Base::batch_size()
                };
            }
        }

        // Add ORDER BY - returns new SelectBenchmark type with ORDER BY enabled
        template <std::meta::info FieldInfo, OrderDirection Dir = OrderDirection::ASC> auto with_order_by() const {
            using NewOrderByCfg = OrderByConfig<FieldInfo, Dir>;
            if constexpr (WhereCfg::enabled) {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, NewOrderByCfg, GroupByCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, NewOrderByCfg, GroupByCfg>{
                        Base::batch_size()
                };
            }
        }

        // Add GROUP BY - returns new SelectBenchmark type with GROUP BY enabled
        template <std::meta::info FieldInfo> auto with_group_by() const {
            using NewGroupByCfg = GroupByConfig<FieldInfo>;
            if constexpr (WhereCfg::enabled) {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, NewGroupByCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return SelectBenchmark<BaseModel, JoinCfg, WhereCfg, LimitOffsetCfg, OrderByCfg, NewGroupByCfg>{
                        Base::batch_size()
                };
            }
        }

        // ====================================================================
        // print_info - Uses shared footer from base class
        // ====================================================================
        void print_info() const {
            std::cout << "Operation: SELECT";
            Base::print_info_footer();
        }

        // ====================================================================
        // execute - Storm ORM SELECT using base class helpers
        // ====================================================================
        int execute_iteration() {
            auto results = Base::qs().select();
            return results.value().size();
        }

        int execute(int iterations) {
            return Base::execute_with_filters(iterations);
        }

        // ====================================================================
        // execute_raw - Raw SQLite with compile-time feature dispatch
        // ====================================================================
      private:
        // Helper to get JOIN keyword based on JoinType
        static constexpr const char* get_join_keyword() {
            if constexpr (!JoinCfg::enabled) {
                return "";
            } else if constexpr (JoinCfg::join_type == JoinType::Left) {
                return "LEFT JOIN";
            } else if constexpr (JoinCfg::join_type == JoinType::Right) {
                return "RIGHT JOIN";
            } else {
                return "INNER JOIN";
            }
        }

        // Build SQL string based on enabled features
        static std::string build_select_sql() {
            std::string sql;

            if constexpr (JoinCfg::enabled) {
                // JOIN query
                sql = "SELECT fm.id, fm.sender_id, fm.receiver_id, fm.text, "
                      "u.id, u.name, u.age "
                      "FROM FKMessage fm ";
                sql += get_join_keyword();
                sql += " User u ON fm.sender_id = u.id";

                if constexpr (WhereCfg::enabled) {
                    constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                    constexpr std::string_view op_str      = WhereCfg::op.view();
                    sql += " WHERE u.";
                    sql += std::string(where_field);
                    sql += " ";
                    sql += std::string(op_str);
                    sql += " ?";
                }
            } else {
                // Basic SELECT query
                sql = "SELECT id, name, age, is_active, salary FROM Person";

                if constexpr (WhereCfg::enabled) {
                    constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                    constexpr std::string_view op_str      = WhereCfg::op.view();
                    sql += " WHERE ";
                    sql += std::string(where_field);
                    sql += " ";
                    sql += std::string(op_str);
                    sql += " ?";
                }
            }

            // Append GROUP BY if configured (must come before ORDER BY)
            Base::append_group_by_sql(sql);

            // Append ORDER BY if configured (must come before LIMIT/OFFSET)
            Base::append_order_by_sql(sql);

            // Append LIMIT/OFFSET if configured
            Base::append_limit_offset_sql(sql);

            return sql;
        }

        // Extract row for basic SELECT (Person model)
        __attribute__((always_inline)) static BaseModel extract_row_basic(sqlite3_stmt* stmt) {
            BaseModel obj;
            obj.id        = sqlite3_column_int64(stmt, 0);
            obj.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            obj.age       = sqlite3_column_int(stmt, 2);
            obj.is_active = sqlite3_column_int(stmt, 3) != 0;
            obj.salary    = sqlite3_column_double(stmt, 4);
            return obj;
        }

        // Extract row for JOIN SELECT (FKMessage model with joined User)
        __attribute__((always_inline)) static BaseModel extract_row_join(sqlite3_stmt* stmt)
            requires(JoinCfg::enabled)
        {
            BaseModel obj;

            // Base model fields
            obj.id   = sqlite3_column_int64(stmt, 0);
            obj.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            // Joined FK object (columns 4, 5, 6)
            obj.sender.id   = sqlite3_column_int(stmt, 4);
            obj.sender.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            obj.sender.age  = sqlite3_column_int(stmt, 6);

            // Receiver (not joined, just ID)
            obj.receiver.id = sqlite3_column_int(stmt, 2);

            return obj;
        }

      public:
        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            const std::string sql = build_select_sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Bind WHERE value if configured
            if constexpr (WhereCfg::enabled) {
                Base::bind_where_value(stmt);
            }

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                plf::hive<BaseModel> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    if constexpr (JoinCfg::enabled) {
                        results.insert(extract_row_join(stmt));
                    } else {
                        results.insert(extract_row_basic(stmt));
                    }
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }
    };

    // ========================================================================
    // Type Aliases for Common Configurations (backward compatibility)
    // ========================================================================

    // SELECT WHERE - equivalent to old SelectBenchmark
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType>
    using SelectWhereBenchmark = SelectBenchmark<Model, NoJoin, WhereConfig<FieldInfo, Op, ValueType>>;

    // SELECT JOIN - equivalent to old SelectJoinBenchmark
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr>
    using SelectJoinBenchmark = SelectBenchmark<BaseModel, JoinConfig<FKFieldPtr, RelatedModel>, NoWhere>;

    // SELECT WHERE + JOIN - equivalent to old SelectWhereJoinBenchmark
    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    using SelectWhereJoinBenchmark = SelectBenchmark<
            BaseModel,
            JoinConfig<FKFieldPtr, RelatedModel>,
            WhereConfig<WhereFieldInfo, Op, ValueType>>;

    // ========================================================================
    // LIMIT/OFFSET Type Aliases
    // ========================================================================

    // SELECT LIMIT - simple LIMIT clause
    template <typename Model, int LimitValue>
    using SelectLimitBenchmark = SelectBenchmark<Model, NoJoin, NoWhere, LimitOffsetConfig<LimitValue, 0>>;

    // SELECT OFFSET - OFFSET with LIMIT -1 (all rows)
    template <typename Model, int OffsetValue>
    using SelectOffsetBenchmark = SelectBenchmark<Model, NoJoin, NoWhere, LimitOffsetConfig<0, OffsetValue>>;

    // SELECT LIMIT + OFFSET - pagination
    template <typename Model, int LimitValue, int OffsetValue>
    using SelectLimitOffsetBenchmark =
            SelectBenchmark<Model, NoJoin, NoWhere, LimitOffsetConfig<LimitValue, OffsetValue>>;

    // SELECT WHERE + LIMIT
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType, int LimitValue>
    using SelectWhereLimitBenchmark =
            SelectBenchmark<Model, NoJoin, WhereConfig<FieldInfo, Op, ValueType>, LimitOffsetConfig<LimitValue, 0>>;

    // SELECT WHERE + LIMIT + OFFSET
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType, int LimitValue, int OffsetValue>
    using SelectWhereLimitOffsetBenchmark = SelectBenchmark<
            Model,
            NoJoin,
            WhereConfig<FieldInfo, Op, ValueType>,
            LimitOffsetConfig<LimitValue, OffsetValue>>;

    // SELECT JOIN + LIMIT
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr, int LimitValue>
    using SelectJoinLimitBenchmark =
            SelectBenchmark<BaseModel, JoinConfig<FKFieldPtr, RelatedModel>, NoWhere, LimitOffsetConfig<LimitValue, 0>>;

    // SELECT JOIN + LIMIT + OFFSET
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr, int LimitValue, int OffsetValue>
    using SelectJoinLimitOffsetBenchmark = SelectBenchmark<
            BaseModel,
            JoinConfig<FKFieldPtr, RelatedModel>,
            NoWhere,
            LimitOffsetConfig<LimitValue, OffsetValue>>;

    // ========================================================================
    // ORDER BY Type Aliases
    // ========================================================================

    // SELECT ORDER BY ASC - single field ascending
    template <typename Model, std::meta::info FieldInfo>
    using SelectOrderByAscBenchmark =
            SelectBenchmark<Model, NoJoin, NoWhere, NoLimitOffset, OrderByConfig<FieldInfo, OrderDirection::ASC>>;

    // SELECT ORDER BY DESC - single field descending
    template <typename Model, std::meta::info FieldInfo>
    using SelectOrderByDescBenchmark =
            SelectBenchmark<Model, NoJoin, NoWhere, NoLimitOffset, OrderByConfig<FieldInfo, OrderDirection::DESC>>;

    // SELECT ORDER BY + WHERE
    template <
            typename Model,
            std::meta::info OrderField,
            OrderDirection  Dir,
            std::meta::info WhereField,
            auto            Op,
            typename ValueType>
    using SelectOrderByWhereBenchmark = SelectBenchmark<
            Model,
            NoJoin,
            WhereConfig<WhereField, Op, ValueType>,
            NoLimitOffset,
            OrderByConfig<OrderField, Dir>>;

    // SELECT ORDER BY + LIMIT (Top-N query pattern)
    template <typename Model, std::meta::info FieldInfo, OrderDirection Dir, int LimitValue>
    using SelectOrderByLimitBenchmark =
            SelectBenchmark<Model, NoJoin, NoWhere, LimitOffsetConfig<LimitValue, 0>, OrderByConfig<FieldInfo, Dir>>;

    // ========================================================================
    // GROUP BY Type Aliases
    // ========================================================================

    // SELECT GROUP BY - single field grouping
    template <typename Model, std::meta::info FieldInfo>
    using SelectGroupByBenchmark =
            SelectBenchmark<Model, NoJoin, NoWhere, NoLimitOffset, NoOrderBy, GroupByConfig<FieldInfo>>;

    // SELECT GROUP BY + WHERE
    template <typename Model, std::meta::info GroupField, std::meta::info WhereField, auto Op, typename ValueType>
    using SelectGroupByWhereBenchmark = SelectBenchmark<
            Model,
            NoJoin,
            WhereConfig<WhereField, Op, ValueType>,
            NoLimitOffset,
            NoOrderBy,
            GroupByConfig<GroupField>>;

    // ========================================================================
    // LEFT JOIN Type Aliases
    // ========================================================================

    // SELECT LEFT JOIN - basic LEFT JOIN
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr>
    using SelectLeftJoinBenchmark =
            SelectBenchmark<BaseModel, JoinConfig<FKFieldPtr, RelatedModel, JoinType::Left>, NoWhere>;

    // SELECT LEFT JOIN + WHERE
    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    using SelectLeftJoinWhereBenchmark = SelectBenchmark<
            BaseModel,
            JoinConfig<FKFieldPtr, RelatedModel, JoinType::Left>,
            WhereConfig<WhereFieldInfo, Op, ValueType>>;

    // ========================================================================
    // RIGHT JOIN Type Aliases
    // ========================================================================

    // SELECT RIGHT JOIN - basic RIGHT JOIN
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr>
    using SelectRightJoinBenchmark =
            SelectBenchmark<BaseModel, JoinConfig<FKFieldPtr, RelatedModel, JoinType::Right>, NoWhere>;

    // SELECT RIGHT JOIN + WHERE
    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    using SelectRightJoinWhereBenchmark = SelectBenchmark<
            BaseModel,
            JoinConfig<FKFieldPtr, RelatedModel, JoinType::Right>,
            WhereConfig<WhereFieldInfo, Op, ValueType>>;

    // ========================================================================
    // Multi-FK JOIN Benchmark - Joins on both sender and receiver FK fields
    // ========================================================================

    template <typename BaseModel, typename RelatedModel> class SelectMultiFKJoinBenchmark {
        int                    dataset_size_;
        QuerySet<BaseModel>    qs_{};
        QuerySet<RelatedModel> related_qs_{};

      public:
        explicit SelectMultiFKJoinBenchmark(int dataset_size) : dataset_size_(dataset_size) {}

        void print_info() const {
            std::cout << "Operation: SELECT + Multi-FK INNER JOIN (sender + receiver)\n";
            std::cout << "  Dataset: " << dataset_size_ << " rows\n";
        }

        void prepare([[maybe_unused]] int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return;

            // Clear tables
            sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

            // Insert related records (users)
            std::vector<RelatedModel> users;
            users.reserve(dataset_size_);
            for (int i = 0; i < dataset_size_; i++) {
                users.push_back(RelatedModel{.id = 0, .name = std::format("User{}", i + 1), .age = 20 + (i % 50)});
            }

            auto user_result = related_qs_.insert(users);
            if (!user_result.has_value()) {
                std::cerr << "Failed to insert users for benchmark\n";
                return;
            }

            // SELECT back to get the auto-generated user IDs
            auto user_select = related_qs_.select();
            if (!user_select.has_value()) {
                std::cerr << "Failed to select users for benchmark\n";
                return;
            }

            // Extract user IDs
            std::vector<int64_t> user_ids;
            user_ids.reserve(user_select.value().size());
            for (const auto& user : user_select.value()) {
                user_ids.push_back(user.id);
            }

            // Insert base records with FK references (sender AND receiver)
            std::vector<BaseModel> messages;
            messages.reserve(dataset_size_);
            for (int i = 0; i < dataset_size_; i++) {
                RelatedModel sender{static_cast<int>(user_ids[i % user_ids.size()]), "", 0};
                RelatedModel receiver{static_cast<int>(user_ids[(i + 1) % user_ids.size()]), "", 0};

                messages.push_back(
                        BaseModel{
                                .id = 0, .sender = sender, .receiver = receiver, .text = std::format("Message{}", i + 1)
                        }
                );
            }

            auto msg_result = qs_.insert(messages);
            if (!msg_result.has_value()) {
                std::cerr << "Failed to insert messages for benchmark\n";
            }
        }

        int execute(int iterations) {
            // JOIN on both sender AND receiver FK fields
            qs_.template join<&BaseModel::sender, &BaseModel::receiver>();

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto results = qs_.select();
                total += results.value().size();
            }
            qs_.reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            // Multi-FK JOIN: JOIN on both sender and receiver
            const std::string sql = "SELECT fm.id, fm.sender_id, fm.receiver_id, fm.text, "
                                    "s.id, s.name, s.age, "
                                    "r.id, r.name, r.age "
                                    "FROM FKMessage fm "
                                    "INNER JOIN User s ON fm.sender_id = s.id "
                                    "INNER JOIN User r ON fm.receiver_id = r.id";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                plf::hive<BaseModel> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    BaseModel obj;
                    obj.id   = sqlite3_column_int64(stmt, 0);
                    obj.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

                    // Sender (columns 4, 5, 6)
                    obj.sender.id   = sqlite3_column_int(stmt, 4);
                    obj.sender.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    obj.sender.age  = sqlite3_column_int(stmt, 6);

                    // Receiver (columns 7, 8, 9)
                    obj.receiver.id   = sqlite3_column_int(stmt, 7);
                    obj.receiver.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                    obj.receiver.age  = sqlite3_column_int(stmt, 9);

                    results.insert(obj);
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }
    };

} // namespace storm::benchmark
