#pragma once

/**
 * DISTINCT Benchmark - Performance testing for SELECT DISTINCT operations
 *
 * Tests all DISTINCT configurations:
 * - Simple DISTINCT (no filters)
 * - DISTINCT + WHERE
 * - DISTINCT + JOIN
 * - DISTINCT + WHERE + JOIN
 *
 * Features are configured via template parameters at compile time.
 * Uses `if constexpr` for zero-overhead feature dispatch.
 *
 * Inherits shared WHERE/JOIN infrastructure from SelectQueryBenchmarkBase.
 *
 * Usage:
 *   // Simple DISTINCT
 *   DistinctBenchmark<Person, ^^Person::age>{1000}
 *
 *   // DISTINCT + WHERE
 *   DistinctBenchmark<Person, ^^Person::age, NoJoin, WhereConfig<^^Person::salary, ">", double>>{50000.0, 1000}
 *
 *   // DISTINCT + JOIN (on FK field)
 *   DistinctBenchmark<FKMessage, ^^FKMessage::sender, JoinConfig<&FKMessage::sender, User>>{1000}
 */

#include "select_query_base.hpp"
#include <format>
#include <plf_hive/plf_hive.h>
#include <string>

namespace storm::benchmark {

    // ========================================================================
    // DistinctBenchmark - Inherits from SelectQueryBenchmarkBase
    // ========================================================================
    template <
            typename BaseModel,
            std::meta::info DistinctFieldInfo,
            typename JoinCfg  = NoJoin,
            typename WhereCfg = NoWhere>
    class DistinctBenchmark : public SelectQueryBenchmarkBase<
                                      DistinctBenchmark<BaseModel, DistinctFieldInfo, JoinCfg, WhereCfg>,
                                      BaseModel,
                                      JoinCfg,
                                      WhereCfg> {
        using Base = SelectQueryBenchmarkBase<
                DistinctBenchmark<BaseModel, DistinctFieldInfo, JoinCfg, WhereCfg>,
                BaseModel,
                JoinCfg,
                WhereCfg>;

      public:
        // ====================================================================
        // Constructors - delegate to base
        // ====================================================================

        // Simple DISTINCT (no WHERE) - dataset_size only
        explicit constexpr DistinctBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // DISTINCT with WHERE - value + dataset_size
        template <typename V>
        explicit constexpr DistinctBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(value, dataset_size) {}

        // ====================================================================
        // Builder Methods (return NEW types for compile-time chaining)
        // ====================================================================

        // Add JOIN - returns new DistinctBenchmark type with JOIN enabled
        template <auto FK, typename Related> auto with_join() const {
            using NewJoinCfg = JoinConfig<FK, Related>;

            if constexpr (WhereCfg::enabled) {
                return DistinctBenchmark<BaseModel, DistinctFieldInfo, NewJoinCfg, WhereCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return DistinctBenchmark<BaseModel, DistinctFieldInfo, NewJoinCfg, WhereCfg>{Base::batch_size()};
            }
        }

        // Add WHERE - returns new DistinctBenchmark type with WHERE enabled
        template <std::meta::info FieldInfo, auto Op, typename V> auto with_where(V value) const {
            using NewWhereCfg = WhereConfig<FieldInfo, Op, V>;
            return DistinctBenchmark<BaseModel, DistinctFieldInfo, JoinCfg, NewWhereCfg>{value, Base::batch_size()};
        }

        // ====================================================================
        // print_info - Feature-aware info output
        // ====================================================================
        void print_info() const {
            constexpr std::string_view distinct_field = std::meta::identifier_of(DistinctFieldInfo);
            std::cout << "Operation: SELECT DISTINCT " << distinct_field;

            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view field_name = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str     = WhereCfg::op.view();
                std::cout << " WHERE " << field_name << " " << op_str << " " << Base::where_value();
            }

            if constexpr (JoinCfg::enabled) {
                std::cout << " + JOIN";
            }

            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        // ====================================================================
        // execute - Storm ORM DISTINCT with compile-time feature dispatch
        // ====================================================================
        int execute(int iterations) {
            int total_rows = 0;

            // Apply JOIN if configured (compile-time check)
            if constexpr (JoinCfg::enabled) {
                Base::qs().template join<JoinCfg::fk_ptr>();
            }

            // Apply WHERE if configured (compile-time check)
            if constexpr (WhereCfg::enabled) {
                auto where_clause = Base::build_where_clause();
                Base::qs().where(where_clause);
            }

            // Execute DISTINCT query loop
            for (int i = 0; i < iterations; i++) {
                auto results = Base::qs().template distinct<DistinctFieldInfo>().select();
                total_rows += results.value().size();
            }

            Base::qs().reset();
            return total_rows;
        }

        // ====================================================================
        // execute_raw - Raw SQLite DISTINCT with compile-time feature dispatch
        // ====================================================================
      private:
        // Build DISTINCT SQL string based on enabled features
        static std::string build_distinct_sql() {
            constexpr std::string_view distinct_field = std::meta::identifier_of(DistinctFieldInfo);
            std::string                sql;

            if constexpr (JoinCfg::enabled) {
                // DISTINCT + JOIN query
                sql = "SELECT DISTINCT fm.";
                sql += distinct_field;
                sql += " FROM FKMessage fm INNER JOIN User u ON fm.sender_id = u.id";

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
                // Basic DISTINCT query
                sql = "SELECT DISTINCT ";
                sql += distinct_field;
                sql += " FROM Person";

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

      public:
        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (db == nullptr)
                return 0;

            const std::string sql = build_distinct_sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Bind WHERE value if configured
            if constexpr (WhereCfg::enabled) {
                Base::bind_where_value(stmt);
            }

            // Determine field type at compile time
            using FieldType = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo:])>;

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                // Extract distinct values based on field type
                if constexpr (std::is_same_v<FieldType, std::string>) {
                    plf::hive<std::string> results;
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        results.insert(text != nullptr ? text : "");
                    }
                    total_rows += results.size();
                } else {
                    plf::hive<int64_t> results;
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        results.insert(sqlite3_column_int64(stmt, 0));
                    }
                    total_rows += results.size();
                }
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }
    };

    // ========================================================================
    // Type Aliases for Common DISTINCT Configurations
    // ========================================================================

    // Simple DISTINCT - equivalent to qs.distinct<Field>()
    template <typename Model, std::meta::info FieldInfo>
    using SimpleDistinctBenchmark = DistinctBenchmark<Model, FieldInfo, NoJoin, NoWhere>;

    // DISTINCT WHERE - equivalent to qs.where(...).distinct<Field>()
    template <
            typename Model,
            std::meta::info DistinctFieldInfo,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    using DistinctWhereBenchmark =
            DistinctBenchmark<Model, DistinctFieldInfo, NoJoin, WhereConfig<WhereFieldInfo, Op, ValueType>>;

    // DISTINCT JOIN - equivalent to qs.join<FK>().distinct<Field>()
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr, std::meta::info DistinctFieldInfo>
    using DistinctJoinBenchmark =
            DistinctBenchmark<BaseModel, DistinctFieldInfo, JoinConfig<FKFieldPtr, RelatedModel>, NoWhere>;

    // DISTINCT WHERE + JOIN - combined
    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            std::meta::info DistinctFieldInfo,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    using DistinctWhereJoinBenchmark = DistinctBenchmark<
            BaseModel,
            DistinctFieldInfo,
            JoinConfig<FKFieldPtr, RelatedModel>,
            WhereConfig<WhereFieldInfo, Op, ValueType>>;

} // namespace storm::benchmark
