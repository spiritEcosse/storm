#pragma once

/**
 * Aggregate Benchmark - Performance testing for aggregate functions
 *
 * Tests all aggregate operations with optional WHERE/JOIN:
 * - COUNT(*) - count all rows
 * - COUNT(field) - count non-null values
 * - COUNT(DISTINCT field) - count unique values
 * - SUM(field) - sum of field values
 * - AVG(field) - average of field values
 * - MIN(field) - minimum value
 * - MAX(field) - maximum value
 *
 * Inherits shared WHERE/JOIN infrastructure from SelectQueryBenchmarkBase.
 *
 * Usage:
 *   // Simple aggregate
 *   AggregateBenchmark<Person, AggregateOp::Count>{1000}
 *   AggregateBenchmark<Person, AggregateOp::Sum, ^^Person::age>{1000}
 *
 *   // Aggregate + WHERE
 *   AggregateBenchmark<Person, AggregateOp::Count, std::meta::info{}, NoJoin,
 *                      WhereConfig<^^Person::age, ">", int>>{30, 1000}
 *
 *   // Aggregate + JOIN
 *   AggregateBenchmark<FKMessage, AggregateOp::Count, std::meta::info{},
 *                      JoinConfig<&FKMessage::sender, User>>{1000}
 */

#include "select_query_base.hpp"
#include <format>
#include <meta>
#include <string_view>

namespace storm::benchmark {

    // ========================================================================
    // Aggregate Type Configuration
    // ========================================================================
    enum class AggregateOp { Count, CountField, CountDistinct, Sum, Avg, Min, Max };

    // Get aggregate operation name for display
    constexpr std::string_view aggregate_op_name(AggregateOp op) {
        switch (op) {
        case AggregateOp::Count:
            return "COUNT(*)";
        case AggregateOp::CountField:
            return "COUNT(field)";
        case AggregateOp::CountDistinct:
            return "COUNT(DISTINCT)";
        case AggregateOp::Sum:
            return "SUM";
        case AggregateOp::Avg:
            return "AVG";
        case AggregateOp::Min:
            return "MIN";
        case AggregateOp::Max:
            return "MAX";
        }
        return "UNKNOWN";
    }

    // ========================================================================
    // AggregateBenchmark - Inherits from SelectQueryBenchmarkBase
    // ========================================================================
    template <
            typename Model,
            AggregateOp     Op,
            std::meta::info FieldInfo = std::meta::info{},
            typename JoinCfg          = NoJoin,
            typename WhereCfg         = NoWhere>
    class AggregateBenchmark : public SelectQueryBenchmarkBase<
                                       AggregateBenchmark<Model, Op, FieldInfo, JoinCfg, WhereCfg>,
                                       Model,
                                       JoinCfg,
                                       WhereCfg> {
        using Base = SelectQueryBenchmarkBase<
                AggregateBenchmark<Model, Op, FieldInfo, JoinCfg, WhereCfg>,
                Model,
                JoinCfg,
                WhereCfg>;

      public:
        // ====================================================================
        // Constructors - delegate to base
        // ====================================================================

        // Simple aggregate (no WHERE) - dataset_size only
        explicit constexpr AggregateBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // Aggregate with WHERE - value + dataset_size
        template <typename V>
        explicit constexpr AggregateBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(value, dataset_size) {}

        // ====================================================================
        // Builder Methods (return NEW types for compile-time chaining)
        // ====================================================================

        // Add JOIN - returns new AggregateBenchmark type with JOIN enabled
        template <auto FK, typename Related> auto with_join() const {
            using NewJoinCfg = JoinConfig<FK, Related>;

            if constexpr (WhereCfg::enabled) {
                return AggregateBenchmark<Model, Op, FieldInfo, NewJoinCfg, WhereCfg>{
                        Base::where_value(), Base::batch_size()
                };
            } else {
                return AggregateBenchmark<Model, Op, FieldInfo, NewJoinCfg, WhereCfg>{Base::batch_size()};
            }
        }

        // Add WHERE - returns new AggregateBenchmark type with WHERE enabled
        template <std::meta::info WhereFieldInfo, auto WhereOp, typename V> auto with_where(V value) const {
            using NewWhereCfg = WhereConfig<WhereFieldInfo, WhereOp, V>;
            return AggregateBenchmark<Model, Op, FieldInfo, JoinCfg, NewWhereCfg>{value, Base::batch_size()};
        }

        // ====================================================================
        // print_info - Display benchmark info
        // ====================================================================
        void print_info() const {
            std::cout << "Operation: " << aggregate_op_name(Op);

            if constexpr (Op != AggregateOp::Count) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                std::cout << " (" << field_name << ")";
            }

            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str      = WhereCfg::op.view();
                std::cout << " WHERE " << where_field << " " << op_str << " " << Base::where_value();
            }

            if constexpr (JoinCfg::enabled) {
                std::cout << " + JOIN";
            }

            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        // ====================================================================
        // execute - Storm ORM aggregate query
        // ====================================================================
        int execute(int iterations) {
            int total = 0;

            // Apply JOIN if configured (compile-time check)
            if constexpr (JoinCfg::enabled) {
                Base::qs().template join<JoinCfg::fk_ptr>();
            }

            // Apply WHERE if configured (compile-time check)
            if constexpr (WhereCfg::enabled) {
                auto where_clause = Base::build_where_clause();
                Base::qs().where(where_clause);
            }

            for (int i = 0; i < iterations; i++) {
                if constexpr (Op == AggregateOp::Count) {
                    auto result = Base::qs().count().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::CountField) {
                    auto result = Base::qs().template count<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::CountDistinct) {
                    auto result = Base::qs().template count_distinct<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::Sum) {
                    auto result = Base::qs().template sum<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::Avg) {
                    auto result = Base::qs().template avg<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::Min) {
                    auto result = Base::qs().template min<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                } else if constexpr (Op == AggregateOp::Max) {
                    auto result = Base::qs().template max<FieldInfo>().select();
                    if (result.has_value()) {
                        total += (result.value() >= 0 ? 1 : 0);
                    }
                }
            }

            Base::qs().reset();
            return total;
        }

        // ====================================================================
        // execute_raw - Raw SQLite aggregate query
        // ====================================================================
      private:
        static std::string build_aggregate_sql() {
            std::string sql = "SELECT ";

            // Build aggregate function
            if constexpr (Op == AggregateOp::Count) {
                sql += "COUNT(*)";
            } else if constexpr (Op == AggregateOp::CountField) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "COUNT(";
                sql += field_name;
                sql += ")";
            } else if constexpr (Op == AggregateOp::CountDistinct) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "COUNT(DISTINCT ";
                sql += field_name;
                sql += ")";
            } else if constexpr (Op == AggregateOp::Sum) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "SUM(";
                sql += field_name;
                sql += ")";
            } else if constexpr (Op == AggregateOp::Avg) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "AVG(";
                sql += field_name;
                sql += ")";
            } else if constexpr (Op == AggregateOp::Min) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "MIN(";
                sql += field_name;
                sql += ")";
            } else if constexpr (Op == AggregateOp::Max) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                sql += "MAX(";
                sql += field_name;
                sql += ")";
            }

            // Build FROM clause
            if constexpr (JoinCfg::enabled) {
                sql += " FROM FKMessage fm INNER JOIN User u ON fm.sender_id = u.id";
            } else {
                sql += " FROM Person";
            }

            // Build WHERE clause if configured
            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str      = WhereCfg::op.view();

                sql += " WHERE ";
                if constexpr (JoinCfg::enabled) {
                    sql += "u."; // Assume WHERE is on joined table for JOIN benchmarks
                }
                sql += std::string(where_field);
                sql += " ";
                sql += std::string(op_str);
                sql += " ?";
            }

            return sql;
        }

      public:
        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            const std::string sql = build_aggregate_sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Bind WHERE value if configured
            if constexpr (WhereCfg::enabled) {
                Base::bind_where_value(stmt);
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    // For COUNT, SUM - use int64
                    // For AVG, MIN, MAX - use double
                    if constexpr (Op == AggregateOp::Count || Op == AggregateOp::CountField ||
                                  Op == AggregateOp::CountDistinct || Op == AggregateOp::Sum) {
                        int64_t result = sqlite3_column_int64(stmt, 0);
                        total += (result > 0 ? 1 : 0);
                    } else {
                        double result = sqlite3_column_double(stmt, 0);
                        total += (result > 0 ? 1 : 0);
                    }
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

    // ========================================================================
    // Type Aliases for Common Aggregate Benchmarks
    // ========================================================================

    // COUNT(*) benchmark
    template <typename Model> using CountBenchmark = AggregateBenchmark<Model, AggregateOp::Count>;

    // COUNT(field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using CountFieldBenchmark = AggregateBenchmark<Model, AggregateOp::CountField, FieldInfo>;

    // COUNT(DISTINCT field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using CountDistinctBenchmark = AggregateBenchmark<Model, AggregateOp::CountDistinct, FieldInfo>;

    // SUM(field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using SumBenchmark = AggregateBenchmark<Model, AggregateOp::Sum, FieldInfo>;

    // AVG(field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using AvgBenchmark = AggregateBenchmark<Model, AggregateOp::Avg, FieldInfo>;

    // MIN(field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using MinBenchmark = AggregateBenchmark<Model, AggregateOp::Min, FieldInfo>;

    // MAX(field) benchmark
    template <typename Model, std::meta::info FieldInfo>
    using MaxBenchmark = AggregateBenchmark<Model, AggregateOp::Max, FieldInfo>;

    // ========================================================================
    // Type Aliases for Aggregate + WHERE
    // ========================================================================

    template <
            typename Model,
            AggregateOp     Op,
            std::meta::info FieldInfo,
            std::meta::info WhereFieldInfo,
            auto            WhereOp,
            typename ValueType>
    using AggregateWhereBenchmark =
            AggregateBenchmark<Model, Op, FieldInfo, NoJoin, WhereConfig<WhereFieldInfo, WhereOp, ValueType>>;

    // ========================================================================
    // Type Aliases for Aggregate + JOIN
    // ========================================================================

    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            AggregateOp     Op,
            std::meta::info FieldInfo = std::meta::info{}>
    using AggregateJoinBenchmark =
            AggregateBenchmark<BaseModel, Op, FieldInfo, JoinConfig<FKFieldPtr, RelatedModel>, NoWhere>;

    // ========================================================================
    // Type Aliases for Aggregate + WHERE + JOIN
    // ========================================================================

    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            AggregateOp     Op,
            std::meta::info FieldInfo,
            std::meta::info WhereFieldInfo,
            auto            WhereOp,
            typename ValueType>
    using AggregateWhereJoinBenchmark = AggregateBenchmark<
            BaseModel,
            Op,
            FieldInfo,
            JoinConfig<FKFieldPtr, RelatedModel>,
            WhereConfig<WhereFieldInfo, WhereOp, ValueType>>;

} // namespace storm::benchmark
