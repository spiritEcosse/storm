#pragma once

/**
 * Aggregate Benchmark - Performance testing for aggregate functions
 *
 * Tests all aggregate operations:
 * - COUNT(*) - count all rows
 * - COUNT(field) - count non-null values
 * - COUNT(DISTINCT field) - count unique values
 * - SUM(field) - sum of field values
 * - AVG(field) - average of field values
 * - MIN(field) - minimum value
 * - MAX(field) - maximum value
 *
 * Usage:
 *   AggregateBenchmark<Person, AggregateType::Count>{1000}
 *   AggregateBenchmark<Person, AggregateType::Sum, ^^Person::age>{1000}
 */

#include "base.hpp"
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
    // AggregateBenchmark - Unified class for all aggregate operations
    // ========================================================================
    template <typename Model, AggregateOp Op, std::meta::info FieldInfo = std::meta::info{}>
    class AggregateBenchmark : public DataBenchmarkBase<AggregateBenchmark<Model, Op, FieldInfo>, Model, 1> {
        using Base = DataBenchmarkBase<AggregateBenchmark<Model, Op, FieldInfo>, Model, 1>;

      public:
        // Constructor - dataset_size is how many rows to query over
        explicit constexpr AggregateBenchmark(int dataset_size = 1000) : Base(dataset_size) {}

        // ====================================================================
        // print_info - Display benchmark info
        // ====================================================================
        void print_info() const {
            std::cout << "Operation: " << aggregate_op_name(Op);

            if constexpr (Op != AggregateOp::Count) {
                constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
                std::cout << " (" << field_name << ")";
            }

            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        // ====================================================================
        // create_model - Generate varied data for aggregate testing
        // ====================================================================
        static Model create_model(int index = 0) {
            int i = index + 1;
            return Model{
                    .id        = 0,
                    .name      = std::format("Person{}", i),
                    .age       = 20 + (i % 50), // Range: 20-69
                    .is_active = (i % 2 == 0),
                    .salary    = 30000.0 + (i * 1000.0)
            };
        }

        // ====================================================================
        // prepare - Setup test data
        // ====================================================================
        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        // ====================================================================
        // execute - Storm ORM aggregate query
        // ====================================================================
        int execute(int iterations) {
            int total = 0;

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

            return total;
        }

        // ====================================================================
        // execute_raw - Raw SQLite aggregate query
        // ====================================================================
      private:
        static std::string build_aggregate_sql() {
            std::string sql = "SELECT ";

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

            sql += " FROM Person";
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

} // namespace storm::benchmark
