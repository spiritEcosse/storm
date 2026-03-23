#pragma once

/**
 * CRTP Base Class for SELECT-like Benchmarks (SELECT, DISTINCT, AGGREGATE)
 *
 * Provides shared functionality:
 * - WHERE value storage and clause building
 * - JOIN configuration and data preparation
 * - Model creation with varied data
 * - Parameter binding utilities
 * - print_info helpers (WHERE/JOIN suffix, dataset footer)
 *
 * Derived classes implement:
 * - print_info() - calls print_info_footer() after operation header
 * - execute() - Storm ORM execution
 * - execute_raw() - Raw SQLite execution
 * - build_*_sql() - operation-specific SQL generation
 */

#include "base.hpp"
#include <format>
#include <meta>
#include <plf_hive/plf_hive.h>
#include <variant>
#include <vector>

namespace storm::benchmark {

    using storm::orm::where::field;

    // ========================================================================
    // Feature Configuration Tags (zero-size types for compile-time dispatch)
    // ========================================================================

    // No JOIN configured
    struct NoJoin {
        static constexpr bool enabled = false;
    };

    // No WHERE configured
    struct NoWhere {
        static constexpr bool enabled = false;
    };

    enum class JoinType { Inner, Left, Right };

    // JOIN configuration - encodes FK field, related model type, and join type
    template <auto FKFieldPtr, typename RelatedModel, JoinType Type = JoinType::Inner> struct JoinConfig {
        static constexpr bool     enabled   = true;
        static constexpr auto     fk_ptr    = FKFieldPtr;
        static constexpr JoinType join_type = Type;
        using related_type                  = RelatedModel;
    };

    // Multi-FK JOIN configuration - for benchmarking multiple FK joins
    template <typename RelatedModel, JoinType Type, auto... FKFieldPtrs> struct MultiJoinConfig {
        static constexpr bool     enabled     = true;
        static constexpr bool     is_multi_fk = true;
        static constexpr JoinType join_type   = Type;
        static constexpr size_t   fk_count    = sizeof...(FKFieldPtrs);
        using related_type                    = RelatedModel;
    };

    // WHERE configuration - encodes field, operator, and value type
    template <std::meta::info FieldInfo, auto Op, typename ValueType> struct WhereConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        static constexpr auto            op         = Op;
        using value_type                            = ValueType;
    };

    // LIKE operator configuration
    template <std::meta::info FieldInfo> struct WhereLikeConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        using value_type                            = std::string;
    };

    // BETWEEN operator configuration - two values of same type
    template <std::meta::info FieldInfo, typename ValueType> struct WhereBetweenConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        using value_type                            = ValueType;
    };

    // IN operator configuration - array of values
    template <std::meta::info FieldInfo, typename ValueType, size_t Count> struct WhereInConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        static constexpr size_t          count      = Count;
        using value_type                            = ValueType;
    };

    // Complex AND/OR configuration - two conditions combined
    template <
            std::meta::info FieldInfo1,
            auto            Op1,
            typename ValueType1,
            std::meta::info FieldInfo2,
            auto            Op2,
            typename ValueType2,
            bool IsAnd = true>
    struct WhereAndOrConfig {
        static constexpr bool            enabled     = true;
        static constexpr std::meta::info field_info1 = FieldInfo1;
        static constexpr auto            op1         = Op1;
        static constexpr std::meta::info field_info2 = FieldInfo2;
        static constexpr auto            op2         = Op2;
        static constexpr bool            is_and      = IsAnd;
        using value_type1                            = ValueType1;
        using value_type2                            = ValueType2;
    };

    // No LIMIT/OFFSET configured
    struct NoLimitOffset {
        static constexpr bool enabled      = false;
        static constexpr int  limit_value  = 0;
        static constexpr int  offset_value = 0;
    };

    // LIMIT/OFFSET configuration - encodes limit and offset values at compile time
    template <int LimitValue, int OffsetValue = 0> struct LimitOffsetConfig {
        static constexpr bool enabled      = true;
        static constexpr int  limit_value  = LimitValue;
        static constexpr int  offset_value = OffsetValue;
    };

    // No ORDER BY configured
    struct NoOrderBy {
        static constexpr bool enabled = false;
    };

    // ORDER BY direction enum
    enum class OrderDirection { ASC, DESC };

    // ORDER BY configuration - encodes field and direction at compile time
    template <std::meta::info FieldInfo, OrderDirection Dir = OrderDirection::ASC> struct OrderByConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        static constexpr OrderDirection  direction  = Dir;
    };

    // ORDER BY with COLLATE configuration
    template <std::meta::info FieldInfo, storm::orm::utilities::Collate Col, OrderDirection Dir = OrderDirection::ASC>
    struct OrderByCollateConfig {
        static constexpr bool                           enabled    = true;
        static constexpr std::meta::info                field_info = FieldInfo;
        static constexpr OrderDirection                 direction  = Dir;
        static constexpr storm::orm::utilities::Collate collation  = Col;
    };

    // Multi-field ORDER BY configuration (2 fields with individual directions)
    template <std::meta::info FieldInfo1, OrderDirection Dir1, std::meta::info FieldInfo2, OrderDirection Dir2>
    struct OrderBy2Config {
        static constexpr bool            enabled     = true;
        static constexpr std::meta::info field_info1 = FieldInfo1;
        static constexpr OrderDirection  direction1  = Dir1;
        static constexpr std::meta::info field_info2 = FieldInfo2;
        static constexpr OrderDirection  direction2  = Dir2;
    };

    // No GROUP BY configured
    struct NoGroupBy {
        static constexpr bool enabled = false;
    };

    // GROUP BY configuration - encodes field at compile time
    template <std::meta::info FieldInfo> struct GroupByConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
    };

    // ========================================================================
    // Field dispatcher - compile-time field lookup by name
    // ========================================================================
    template <typename Model> consteval std::meta::info dispatch_field(std::string_view field_name) {
        constexpr auto model_info = ^^Model;

        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(model_info, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(member) == field_name) {
                return member;
            }
        }

        throw std::runtime_error( // NOSONAR(cpp:S112) - std::runtime_error is appropriate for field lookup failure
                "Field not found"
        );
    }

    // ========================================================================
    // Type helpers to avoid std::conditional_t evaluating invalid types
    // ========================================================================

    template <typename Cfg, bool Enabled = Cfg::enabled> struct WhereValueHelper {
        using type = typename Cfg::value_type;
    };

    template <typename Cfg> struct WhereValueHelper<Cfg, false> {
        using type = std::monostate;
    };

    template <typename Cfg, bool Enabled = Cfg::enabled> struct RelatedModelHelper {
        using type = typename Cfg::related_type;
    };

    template <typename Cfg> struct RelatedModelHelper<Cfg, false> {
        using type = std::monostate;
    };

    template <typename Cfg, typename Default, bool Enabled = Cfg::enabled> struct RelatedQSHelper {
        using type = QuerySet<typename Cfg::related_type>;
    };

    template <typename Cfg, typename Default> struct RelatedQSHelper<Cfg, Default, false> {
        using type = std::monostate;
    };

    // ========================================================================
    // CRTP Base Class for SELECT-like Benchmarks
    // ========================================================================
    template <
            typename Derived,
            typename BaseModel,
            typename JoinCfg,
            typename WhereCfg,
            typename LimitOffsetCfg = NoLimitOffset,
            typename OrderByCfg     = NoOrderBy,
            typename GroupByCfg     = NoGroupBy>
    class SelectQueryBenchmarkBase : public DataBenchmarkBase<Derived, BaseModel, 1> {
        using Base = DataBenchmarkBase<Derived, BaseModel, 1>;

      public:
        // WHERE value storage - zero size if NoWhere, otherwise stores the value
        using WhereValueType = typename WhereValueHelper<WhereCfg>::type;
        // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
        [[no_unique_address]] WhereValueType where_value_{};

        // QuerySets - use BaseModel's QuerySet, and RelatedModel's if JOIN enabled
        using RelatedModel = typename RelatedModelHelper<JoinCfg>::type;

        // For JOIN operations, we need a separate QuerySet for related model insertion
        // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
        [[no_unique_address]]
        typename RelatedQSHelper<JoinCfg, std::monostate>::type related_qs_{};

        // Accessor for where_value_ from derived classes
        const WhereValueType& where_value() const {
            return where_value_;
        }

        // ====================================================================
        // print_info helpers - shared formatting for WHERE/JOIN/dataset
        // ====================================================================
        void print_where_suffix() const {
            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str     = WhereCfg::op.view();
                std::cout << " WHERE " << field_name << " " << op_str << " " << where_value_;
            }
        }

        void print_join_suffix() const {
            if constexpr (JoinCfg::enabled) {
                if constexpr (JoinCfg::join_type == JoinType::Left) {
                    std::cout << " + LEFT JOIN";
                } else if constexpr (JoinCfg::join_type == JoinType::Right) {
                    std::cout << " + RIGHT JOIN";
                } else {
                    std::cout << " + INNER JOIN";
                }
            }
        }

        void print_limit_offset_suffix() const {
            if constexpr (LimitOffsetCfg::enabled) {
                if constexpr (LimitOffsetCfg::limit_value > 0) {
                    std::cout << " LIMIT " << LimitOffsetCfg::limit_value;
                }
                if constexpr (LimitOffsetCfg::offset_value > 0) {
                    std::cout << " OFFSET " << LimitOffsetCfg::offset_value;
                }
            }
        }

        void print_order_by_suffix() const {
            if constexpr (OrderByCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(OrderByCfg::field_info);
                std::cout << " ORDER BY " << field_name;
                if constexpr (requires { OrderByCfg::collation; }) {
                    std::cout << storm::orm::utilities::collate_to_sql(OrderByCfg::collation);
                }
                if constexpr (OrderByCfg::direction == OrderDirection::DESC) {
                    std::cout << " DESC";
                } else {
                    std::cout << " ASC";
                }
            }
        }

        void print_group_by_suffix() const {
            if constexpr (GroupByCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(GroupByCfg::field_info);
                std::cout << " GROUP BY " << field_name;
            }
        }

        void print_info_footer() const {
            print_where_suffix();
            print_join_suffix();
            print_group_by_suffix();
            print_order_by_suffix();
            print_limit_offset_suffix();
            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        // ====================================================================
        // execute helpers - shared JOIN/WHERE application and loop structure
        // ====================================================================
        void apply_query_filters() {
            if constexpr (JoinCfg::enabled) {
                // Dispatch to correct JOIN method based on join_type
                if constexpr (JoinCfg::join_type == JoinType::Left) {
                    Base::qs().template left_join<JoinCfg::fk_ptr>();
                } else if constexpr (JoinCfg::join_type == JoinType::Right) {
                    Base::qs().template right_join<JoinCfg::fk_ptr>();
                } else {
                    Base::qs().template join<JoinCfg::fk_ptr>();
                }
            }
            if constexpr (WhereCfg::enabled) {
                auto where_clause = build_where_clause();
                Base::qs()        = Base::qs().where(where_clause);
            }
            if constexpr (OrderByCfg::enabled) {
                if constexpr (requires { OrderByCfg::collation; }) {
                    // ORDER BY with COLLATE
                    if constexpr (OrderByCfg::direction == OrderDirection::DESC) {
                        Base::qs().template order_by<OrderByCfg::field_info, OrderByCfg::collation, false>();
                    } else {
                        Base::qs().template order_by<OrderByCfg::field_info, OrderByCfg::collation, true>();
                    }
                } else {
                    // Storm ORM order_by uses boolean: true = ASC (default), false = DESC
                    if constexpr (OrderByCfg::direction == OrderDirection::DESC) {
                        Base::qs().template order_by<OrderByCfg::field_info, false>();
                    } else {
                        Base::qs().template order_by<OrderByCfg::field_info, true>();
                    }
                }
            }
            if constexpr (GroupByCfg::enabled) {
                Base::qs().template group_by<GroupByCfg::field_info>();
            }
            if constexpr (LimitOffsetCfg::enabled) {
                if constexpr (LimitOffsetCfg::limit_value > 0) {
                    Base::qs().limit(LimitOffsetCfg::limit_value);
                }
                if constexpr (LimitOffsetCfg::offset_value > 0) {
                    Base::qs().offset(LimitOffsetCfg::offset_value);
                }
            }
        }

        // CRTP execute helper - derived class provides execute_iteration()
        int execute_with_filters(int iterations) {
            apply_query_filters();
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                total += static_cast<Derived*>(this)->execute_iteration();
            }
            Base::qs().reset();
            return total;
        }

      public:
        // ====================================================================
        // Constructors
        // ====================================================================

        // Basic (no WHERE) - dataset_size only
        explicit constexpr SelectQueryBenchmarkBase(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // With WHERE - value + dataset_size
        template <typename V>
        explicit constexpr SelectQueryBenchmarkBase(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(dataset_size), where_value_(value) {}

        // ====================================================================
        // create_model - Generate varied data for WHERE clause testing
        // ====================================================================
        static BaseModel create_model(int index = 0) {
            int i = index + 1;

            if constexpr (JoinCfg::enabled) {
                // For JOIN benchmarks, create with FK stubs
                // This will be overwritten in prepare() with real FK references
                return BaseModel{};
            } else {
                // For basic SELECT WHERE, create Person-like model
                return BaseModel{
                        .id        = 0,
                        .name      = std::format("Person{}", i),
                        .age       = 20 + (i % 50),
                        .is_active = (i % 2 == 0),
                        .salary    = 30000.0 + (i * 1000.0),
                        .score     = (i % 3 == 0) ? std::optional<int>(60 + (i % 40)) : std::nullopt
                };
            }
        }

        // ====================================================================
        // prepare - Feature-aware data preparation
        // ====================================================================
        void prepare(int iterations) {
            if constexpr (JoinCfg::enabled) {
                prepare_join_data(iterations);
            } else {
                Base::prepare_with_insert(iterations);
            }
        }

      protected:
        // Prepare data for JOIN benchmarks (creates related records + FK references)
        void prepare_join_data([[maybe_unused]] int iterations) {
            if constexpr (JoinCfg::enabled) {
                sqlite3* db = get_db<BaseModel>();
                if (!db)
                    return;

                int dataset_size = Base::batch_size();

                // Clear tables
                sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
                sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

                // Insert related records (users)
                std::vector<RelatedModel> users;
                users.reserve(dataset_size);
                for (int i = 0; i < dataset_size; i++) {
                    users.push_back(RelatedModel{.id = 0, .name = std::format("User{}", i + 1), .age = 20 + (i % 50)});
                }

                if (auto user_result = related_qs_.insert(users).execute(); !user_result.has_value()) {
                    std::cerr << "Failed to insert users for benchmark\n";
                    return;
                }

                // SELECT back to get the auto-generated user IDs
                auto user_select = related_qs_.select().execute();
                if (!user_select.has_value()) {
                    std::cerr << "Failed to select users for benchmark\n";
                    return;
                }

                // Extract user IDs into a vector for FK references
                std::vector<int64_t> user_ids;
                user_ids.reserve(user_select.value().size());
                for (const auto& user : user_select.value()) {
                    user_ids.push_back(user.id);
                }

                // Insert base records with FK references
                std::vector<BaseModel> messages;
                messages.reserve(dataset_size);
                for (int i = 0; i < dataset_size; i++) {
                    RelatedModel sender{static_cast<int>(user_ids[i % user_ids.size()]), "", 0};
                    RelatedModel receiver{static_cast<int>(user_ids[(i + 1) % user_ids.size()]), "", 0};

                    messages.push_back(
                            BaseModel{
                                    .id       = 0,
                                    .sender   = sender,
                                    .receiver = receiver,
                                    .text     = std::format("Message{}", i + 1)
                            }
                    );
                }

                auto msg_result = Base::qs().insert(messages).execute();
                if (!msg_result.has_value()) {
                    std::cerr << "Failed to insert messages for benchmark\n";
                }
            }
        }

        // ====================================================================
        // WHERE clause helpers
        // ====================================================================
        auto build_where_clause() const
            requires(WhereCfg::enabled)
        {
            constexpr std::string_view op_str = WhereCfg::op.view();

            if constexpr (op_str == ">") {
                return field<WhereCfg::field_info>() > where_value_;
            } else if constexpr (op_str == ">=") {
                return field<WhereCfg::field_info>() >= where_value_;
            } else if constexpr (op_str == "<") {
                return field<WhereCfg::field_info>() < where_value_;
            } else if constexpr (op_str == "<=") {
                return field<WhereCfg::field_info>() <= where_value_;
            } else if constexpr (op_str == "==") {
                return field<WhereCfg::field_info>() == where_value_;
            } else if constexpr (op_str == "!=") {
                return field<WhereCfg::field_info>() != where_value_;
            }
        }

        // Bind WHERE value to prepared statement
        void bind_where_value(sqlite3_stmt* stmt) const
            requires(WhereCfg::enabled)
        {
            using V = typename WhereCfg::value_type;

            if constexpr (std::is_same_v<V, int>) {
                sqlite3_bind_int(stmt, 1, where_value_);
            } else if constexpr (std::is_same_v<V, double>) {
                sqlite3_bind_double(stmt, 1, where_value_);
            } else if constexpr (std::is_same_v<V, bool>) {
                sqlite3_bind_int(stmt, 1, where_value_ ? 1 : 0);
            } else if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, const char*>) {
                if constexpr (std::is_same_v<V, std::string>) {
                    sqlite3_bind_text(stmt, 1, where_value_.c_str(), -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_text(stmt, 1, where_value_, -1, SQLITE_TRANSIENT);
                }
            }
        }

        // ====================================================================
        // ORDER BY SQL helpers for raw SQLite benchmarks
        // ====================================================================
        static void append_order_by_sql(std::string& sql) {
            if constexpr (OrderByCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(OrderByCfg::field_info);
                sql += " ORDER BY ";
                sql += std::string(field_name);
                if constexpr (requires { OrderByCfg::collation; }) {
                    sql += storm::orm::utilities::collate_to_sql(OrderByCfg::collation);
                }
                if constexpr (OrderByCfg::direction == OrderDirection::DESC) {
                    sql += " DESC";
                } else {
                    sql += " ASC";
                }
            }
        }

        // ====================================================================
        // LIMIT/OFFSET SQL helpers for raw SQLite benchmarks
        // ====================================================================
        static void append_limit_offset_sql(std::string& sql) {
            if constexpr (LimitOffsetCfg::enabled) {
                if constexpr (LimitOffsetCfg::limit_value > 0) {
                    sql += " LIMIT ";
                    sql += std::to_string(LimitOffsetCfg::limit_value);
                } else if constexpr (LimitOffsetCfg::offset_value > 0) {
                    // SQLite requires LIMIT when using OFFSET
                    sql += " LIMIT -1";
                }
                if constexpr (LimitOffsetCfg::offset_value > 0) {
                    sql += " OFFSET ";
                    sql += std::to_string(LimitOffsetCfg::offset_value);
                }
            }
        }

        // ====================================================================
        // GROUP BY SQL helpers for raw SQLite benchmarks
        // ====================================================================
        static void append_group_by_sql(std::string& sql) {
            if constexpr (GroupByCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(GroupByCfg::field_info);
                sql += " GROUP BY ";
                sql += std::string(field_name);
            }
        }
    };

} // namespace storm::benchmark
