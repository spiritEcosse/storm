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

#include "base.hpp"
#include <format>
#include <meta>
#include <plf_hive/plf_hive.h>
#include <stdexcept>
#include <variant>

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

    // JOIN configuration - encodes FK field and related model type
    template <auto FKFieldPtr, typename RelatedModel> struct JoinConfig {
        static constexpr bool enabled = true;
        static constexpr auto fk_ptr  = FKFieldPtr;
        using related_type            = RelatedModel;
    };

    // WHERE configuration - encodes field, operator, and value type
    template <std::meta::info FieldInfo, auto Op, typename ValueType> struct WhereConfig {
        static constexpr bool            enabled    = true;
        static constexpr std::meta::info field_info = FieldInfo;
        static constexpr auto            op         = Op;
        using value_type                            = ValueType;
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

        throw std::runtime_error("Field not found");
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
    // Unified SelectBenchmark - Compile-Time Feature Configuration
    // ========================================================================
    template <typename BaseModel, typename JoinCfg = NoJoin, typename WhereCfg = NoWhere>
    class SelectBenchmark : public DataBenchmarkBase<SelectBenchmark<BaseModel, JoinCfg, WhereCfg>, BaseModel, 1> {
        using Base = DataBenchmarkBase<SelectBenchmark<BaseModel, JoinCfg, WhereCfg>, BaseModel, 1>;

        // WHERE value storage - zero size if NoWhere, otherwise stores the value
        using WhereValueType = typename WhereValueHelper<WhereCfg>::type;

        [[no_unique_address]] WhereValueType where_value_{};

        // QuerySets - use BaseModel's QuerySet, and RelatedModel's if JOIN enabled
        using RelatedModel = typename RelatedModelHelper<JoinCfg>::type;

        // For JOIN operations, we need a separate QuerySet for related model insertion
        [[no_unique_address]] typename RelatedQSHelper<JoinCfg, std::monostate>::type related_qs_{};

      public:
        // ====================================================================
        // Constructors
        // ====================================================================

        // Basic SELECT (no WHERE) - dataset_size only
        explicit constexpr SelectBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // SELECT with WHERE - value + dataset_size
        template <typename V>
        explicit constexpr SelectBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(dataset_size), where_value_(value) {}

        // ====================================================================
        // Builder Methods (return NEW types for compile-time chaining)
        // ====================================================================

        // Add JOIN - returns new SelectBenchmark type with JOIN enabled
        template <auto FK, typename Related> auto with_join() const {
            using NewJoinCfg = JoinConfig<FK, Related>;

            if constexpr (WhereCfg::enabled) {
                return SelectBenchmark<BaseModel, NewJoinCfg, WhereCfg>{where_value_, Base::batch_size()};
            } else {
                return SelectBenchmark<BaseModel, NewJoinCfg, WhereCfg>{Base::batch_size()};
            }
        }

        // Add WHERE - returns new SelectBenchmark type with WHERE enabled
        template <std::meta::info FieldInfo, auto Op, typename V> auto with_where(V value) const {
            using NewWhereCfg = WhereConfig<FieldInfo, Op, V>;
            return SelectBenchmark<BaseModel, JoinCfg, NewWhereCfg>{value, Base::batch_size()};
        }

        // ====================================================================
        // print_info - Feature-aware info output
        // ====================================================================
        void print_info() const {
            std::cout << "Operation: SELECT";

            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str     = WhereCfg::op.view();
                std::cout << " WHERE " << field_name << " " << op_str << " " << where_value_;
            }

            if constexpr (JoinCfg::enabled) {
                std::cout << " + JOIN";
            }

            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

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
                        .salary    = 30000.0 + (i * 1000.0)
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

      private:
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

                auto user_result = related_qs_.insert(users, storm::orm::statements::InsertOptions{.return_ids = true});
                if (!user_result.has_value()) {
                    std::cerr << "Failed to insert users for JOIN benchmark\n";
                    return;
                }
                const auto& user_ids = user_result.value();

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

                auto msg_result = Base::qs().insert(messages);
                if (!msg_result.has_value()) {
                    std::cerr << "Failed to insert messages for JOIN benchmark\n";
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

      public:
        // ====================================================================
        // execute - Storm ORM SELECT with compile-time feature dispatch
        // ====================================================================
        int execute(int iterations) {
            int total_rows = 0;

            // Apply JOIN if configured (compile-time check)
            if constexpr (JoinCfg::enabled) {
                Base::qs().template join<JoinCfg::fk_ptr>();
            }

            // Apply WHERE if configured (compile-time check)
            if constexpr (WhereCfg::enabled) {
                auto where_clause = build_where_clause();
                Base::qs().where(where_clause);
            }

            // Execute query loop
            for (int i = 0; i < iterations; i++) {
                auto results = Base::qs().select();
                total_rows += results.value().size();
            }

            Base::qs().reset();
            return total_rows;
        }

        // ====================================================================
        // execute_raw - Raw SQLite with compile-time feature dispatch
        // ====================================================================
      private:
        // Build SQL string based on enabled features
        static std::string build_select_sql() {
            std::string sql;

            if constexpr (JoinCfg::enabled) {
                // JOIN query
                sql = "SELECT fm.id, fm.sender_id, fm.receiver_id, fm.text, "
                      "u.id, u.name, u.age "
                      "FROM FKMessage fm "
                      "INNER JOIN User u ON fm.sender_id = u.id";

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

            return sql;
        }

        // Bind WHERE value
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
                bind_where_value(stmt);
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

} // namespace storm::benchmark
