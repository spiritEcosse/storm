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
#include <expected>
#include <format>
#include <meta>
#include <string_view>

namespace storm::benchmark {

    // ========================================================================
    // Aggregate Type Configuration
    // ========================================================================
    enum class AggregateOp { Count, CountField, CountDistinct, Sum, Avg, Min, Max };

    // Get aggregate operation name for display
    constexpr auto aggregate_op_name(AggregateOp op) -> std::string_view {
        using enum AggregateOp;
        switch (op) {
        case Count:
            return "COUNT(*)";
        case CountField:
            return "COUNT(field)";
        case CountDistinct:
            return "COUNT(DISTINCT)";
        case Sum:
            return "SUM";
        case Avg:
            return "AVG";
        case Min:
            return "MIN";
        case Max:
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
        // print_info - Uses shared footer from base class
        // ====================================================================
        auto print_info() const -> void {
            std::cout << "Operation: " << aggregate_op_name(Op);

            if constexpr (Op != AggregateOp::Count) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                std::cout << " (" << field_name << ")";
            }

            Base::print_info_footer();
        }

        // ====================================================================
        // execute - Storm ORM aggregate using base class helpers
        // ====================================================================
      private:
        // Create the aggregate statement once (like raw prepares once)
        auto create_aggregate_stmt() {
            using enum AggregateOp;
            if constexpr (Op == Count) {
                return Base::qs().count();
            } else if constexpr (Op == CountField) {
                return Base::qs().template count<FieldInfo>();
            } else if constexpr (Op == CountDistinct) {
                return Base::qs().template count_distinct<FieldInfo>();
            } else if constexpr (Op == Sum) {
                return Base::qs().template sum<FieldInfo>();
            } else if constexpr (Op == Avg) {
                return Base::qs().template avg<FieldInfo>();
            } else if constexpr (Op == Min) {
                return Base::qs().template min<FieldInfo>();
            } else if constexpr (Op == Max) {
                return Base::qs().template max<FieldInfo>();
            }
        }

      public:
        // For compatibility with execute_with_filters
        auto execute_iteration() -> int {
            auto result = create_aggregate_stmt().select().execute();
            return (result.has_value() && result.value() >= 0) ? 1 : 0;
        }

        auto execute(int iterations) -> int {
            // Create aggregate statement ONCE (like raw prepares statement once)
            // then call execute() in the loop - matches fair benchmark pattern
            Base::apply_query_filters();
            auto agg_stmt = create_aggregate_stmt();

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = agg_stmt.execute();
                total += (result.has_value() && result.value() >= 0) ? 1 : 0;
            }
            Base::qs().reset();
            return total;
        }

        // ====================================================================
        // execute_raw - Raw SQLite aggregate query
        // ====================================================================
      private:
        // Build aggregate SQL — ORM-generated for non-JOIN, manual for JOIN
        auto build_aggregate_sql() const -> std::string {
            if constexpr (JoinCfg::enabled) {
                // JOIN uses custom aliases (fm/u) — keep manual
                std::string sql = "SELECT ";
                if constexpr (Op == AggregateOp::Count) {
                    sql += "COUNT(*)";
                } else {
                    constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                    if constexpr (Op == AggregateOp::CountField) {
                        sql += "COUNT(";
                        sql += field_name;
                        sql += ")";
                    } else if constexpr (Op == AggregateOp::CountDistinct) {
                        sql += "COUNT(DISTINCT ";
                        sql += field_name;
                        sql += ")";
                    } else if constexpr (Op == AggregateOp::Sum) {
                        sql += "SUM(";
                        sql += field_name;
                        sql += ")";
                    } else if constexpr (Op == AggregateOp::Avg) {
                        sql += "AVG(";
                        sql += field_name;
                        sql += ")";
                    } else if constexpr (Op == AggregateOp::Min) {
                        sql += "MIN(";
                        sql += field_name;
                        sql += ")";
                    } else if constexpr (Op == AggregateOp::Max) {
                        sql += "MAX(";
                        sql += field_name;
                        sql += ")";
                    }
                }
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
                return sql;
            } else {
                // SQL from ORM — single source of truth
                QuerySet<Model> qs_tmp;
                if constexpr (WhereCfg::enabled) {
                    qs_tmp = qs_tmp.where(Base::build_where_clause());
                }
                if constexpr (Op == AggregateOp::Count) {
                    return qs_tmp.count().sql();
                } else if constexpr (Op == AggregateOp::CountField) {
                    return qs_tmp.template count<FieldInfo>().sql();
                } else if constexpr (Op == AggregateOp::CountDistinct) {
                    // ORM doesn't have count_distinct — keep manual
                    constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                    std::string                sql        = "SELECT COUNT(DISTINCT ";
                    sql += field_name;
                    sql += std::format(") FROM {}", std::meta::identifier_of(^^Model));
                    if constexpr (WhereCfg::enabled) {
                        constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                        constexpr std::string_view op_str      = WhereCfg::op.view();
                        sql += " WHERE ";
                        sql += std::string(where_field);
                        sql += " ";
                        sql += std::string(op_str);
                        sql += " ?";
                    }
                    return sql;
                } else if constexpr (Op == AggregateOp::Sum) {
                    return qs_tmp.template sum<FieldInfo>().sql();
                } else if constexpr (Op == AggregateOp::Avg) {
                    return qs_tmp.template avg<FieldInfo>().sql();
                } else if constexpr (Op == AggregateOp::Min) {
                    return qs_tmp.template min<FieldInfo>().sql();
                } else if constexpr (Op == AggregateOp::Max) {
                    return qs_tmp.template max<FieldInfo>().sql();
                }
            }
        }

      public:
        auto execute_raw(int iterations) -> int {
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

            // Error type matching Storm's return type for fair comparison
            // (same decision logic: wrap result in std::expected, check has_value, extract value)
            using StormError = storm::db::sqlite::Error;
            using enum AggregateOp;

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                if constexpr (Op == Count || Op == CountField || Op == CountDistinct || Op == Sum) {
                    std::expected<int64_t, StormError> result;
                    int                                rc = sqlite3_step(stmt);
                    if (rc == SQLITE_ROW) {
                        result = sqlite3_column_int64(stmt, 0);
                    } else {
                        result = std::unexpected(StormError{rc, "step failed"});
                    }
                    total += (result.has_value() && result.value() > 0) ? 1 : 0;
                } else {
                    std::expected<double, StormError> result;
                    int                               rc = sqlite3_step(stmt);
                    if (rc == SQLITE_ROW) {
                        result = sqlite3_column_double(stmt, 0);
                    } else {
                        result = std::unexpected(StormError{rc, "step failed"});
                    }
                    total += (result.has_value() && result.value() > 0) ? 1 : 0;
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
