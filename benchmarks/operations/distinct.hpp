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
        // print_info - Uses shared footer from base class
        // ====================================================================
        auto print_info() const -> void {
            constexpr std::string_view distinct_field = std::meta::identifier_of(DistinctFieldInfo);
            std::cout << "Operation: SELECT DISTINCT " << distinct_field;
            Base::print_info_footer();
        }

        // ====================================================================
        // execute - Storm ORM DISTINCT using base class helpers
        // ====================================================================
        auto execute_iteration() -> int {
            auto results = Base::qs().template distinct<DistinctFieldInfo>().select();
            return results.value().size();
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        // ====================================================================
        // execute_raw - Raw SQLite DISTINCT with compile-time feature dispatch
        // ====================================================================
      private:
        // Build DISTINCT SQL string based on enabled features
        static auto build_distinct_sql() -> std::string {
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
        auto execute_raw(int iterations) -> int {
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
                        auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
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

    // ========================================================================
    // Multi-Field DISTINCT Benchmarks (2 or 3 fields)
    // ========================================================================

    // 2-field DISTINCT benchmark
    template <
            typename BaseModel,
            std::meta::info DistinctFieldInfo1,
            std::meta::info DistinctFieldInfo2,
            typename JoinCfg  = NoJoin,
            typename WhereCfg = NoWhere>
    class Distinct2FieldBenchmark
        : public SelectQueryBenchmarkBase<
                  Distinct2FieldBenchmark<BaseModel, DistinctFieldInfo1, DistinctFieldInfo2, JoinCfg, WhereCfg>,
                  BaseModel,
                  JoinCfg,
                  WhereCfg> {
        using Base = SelectQueryBenchmarkBase<
                Distinct2FieldBenchmark<BaseModel, DistinctFieldInfo1, DistinctFieldInfo2, JoinCfg, WhereCfg>,
                BaseModel,
                JoinCfg,
                WhereCfg>;

      public:
        // Constructor - dataset_size only (no WHERE)
        explicit constexpr Distinct2FieldBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        auto print_info() const -> void {
            constexpr std::string_view field1 = std::meta::identifier_of(DistinctFieldInfo1);
            constexpr std::string_view field2 = std::meta::identifier_of(DistinctFieldInfo2);
            std::cout << "Operation: SELECT DISTINCT " << field1 << ", " << field2;
            Base::print_info_footer();
        }

        auto execute_iteration() -> int {
            auto results = Base::qs().template distinct<DistinctFieldInfo1, DistinctFieldInfo2>().select();
            return results.value().size();
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<BaseModel>();
            if (db == nullptr)
                return 0;

            constexpr std::string_view field1 = std::meta::identifier_of(DistinctFieldInfo1);
            constexpr std::string_view field2 = std::meta::identifier_of(DistinctFieldInfo2);

            std::string sql = "SELECT DISTINCT ";
            sql += field1;
            sql += ", ";
            sql += field2;
            sql += " FROM Person";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            using FieldType1 = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo1:])>;
            using FieldType2 = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo2:])>;

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                plf::hive<std::tuple<FieldType1, FieldType2>> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    auto val1 = extract_column<FieldType1>(stmt, 0);
                    auto val2 = extract_column<FieldType2>(stmt, 1);
                    results.insert({std::move(val1), std::move(val2)});
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }

      private:
        template <typename T>
        __attribute__((always_inline)) static auto extract_column(sqlite3_stmt* stmt, int col) -> T {
            if constexpr (std::is_same_v<T, std::string>) {
                auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
                return text != nullptr ? T(text) : T{};
            } else if constexpr (std::is_same_v<T, int>) {
                return sqlite3_column_int(stmt, col);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return sqlite3_column_int64(stmt, col);
            } else if constexpr (std::is_same_v<T, double>) {
                return sqlite3_column_double(stmt, col);
            } else if constexpr (std::is_same_v<T, bool>) {
                return sqlite3_column_int(stmt, col) != 0;
            } else {
                return static_cast<T>(sqlite3_column_int64(stmt, col));
            }
        }
    };

    // 3-field DISTINCT benchmark
    template <
            typename BaseModel,
            std::meta::info DistinctFieldInfo1,
            std::meta::info DistinctFieldInfo2,
            std::meta::info DistinctFieldInfo3,
            typename JoinCfg  = NoJoin,
            typename WhereCfg = NoWhere>
    class Distinct3FieldBenchmark : public SelectQueryBenchmarkBase<
                                            Distinct3FieldBenchmark<
                                                    BaseModel,
                                                    DistinctFieldInfo1,
                                                    DistinctFieldInfo2,
                                                    DistinctFieldInfo3,
                                                    JoinCfg,
                                                    WhereCfg>,
                                            BaseModel,
                                            JoinCfg,
                                            WhereCfg> {
        using Base = SelectQueryBenchmarkBase<
                Distinct3FieldBenchmark<
                        BaseModel,
                        DistinctFieldInfo1,
                        DistinctFieldInfo2,
                        DistinctFieldInfo3,
                        JoinCfg,
                        WhereCfg>,
                BaseModel,
                JoinCfg,
                WhereCfg>;

      public:
        // Constructor - dataset_size only (no WHERE)
        explicit constexpr Distinct3FieldBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        auto print_info() const -> void {
            constexpr std::string_view field1 = std::meta::identifier_of(DistinctFieldInfo1);
            constexpr std::string_view field2 = std::meta::identifier_of(DistinctFieldInfo2);
            constexpr std::string_view field3 = std::meta::identifier_of(DistinctFieldInfo3);
            std::cout << "Operation: SELECT DISTINCT " << field1 << ", " << field2 << ", " << field3;
            Base::print_info_footer();
        }

        auto execute_iteration() -> int {
            auto results =
                    Base::qs().template distinct<DistinctFieldInfo1, DistinctFieldInfo2, DistinctFieldInfo3>().select();
            return results.value().size();
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<BaseModel>();
            if (db == nullptr)
                return 0;

            constexpr std::string_view field1 = std::meta::identifier_of(DistinctFieldInfo1);
            constexpr std::string_view field2 = std::meta::identifier_of(DistinctFieldInfo2);
            constexpr std::string_view field3 = std::meta::identifier_of(DistinctFieldInfo3);

            std::string sql = "SELECT DISTINCT ";
            sql += field1;
            sql += ", ";
            sql += field2;
            sql += ", ";
            sql += field3;
            sql += " FROM Person";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            using FieldType1 = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo1:])>;
            using FieldType2 = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo2:])>;
            using FieldType3 = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo3:])>;

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                plf::hive<std::tuple<FieldType1, FieldType2, FieldType3>> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    auto val1 = extract_column<FieldType1>(stmt, 0);
                    auto val2 = extract_column<FieldType2>(stmt, 1);
                    auto val3 = extract_column<FieldType3>(stmt, 2);
                    results.insert({std::move(val1), std::move(val2), std::move(val3)});
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }

      private:
        template <typename T>
        __attribute__((always_inline)) static auto extract_column(sqlite3_stmt* stmt, int col) -> T {
            if constexpr (std::is_same_v<T, std::string>) {
                auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
                return text != nullptr ? T(text) : T{};
            } else if constexpr (std::is_same_v<T, int>) {
                return sqlite3_column_int(stmt, col);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return sqlite3_column_int64(stmt, col);
            } else if constexpr (std::is_same_v<T, double>) {
                return sqlite3_column_double(stmt, col);
            } else if constexpr (std::is_same_v<T, bool>) {
                return sqlite3_column_int(stmt, col) != 0;
            } else {
                return static_cast<T>(sqlite3_column_int64(stmt, col));
            }
        }
    };

    // ========================================================================
    // DISTINCT + ORDER BY Benchmark
    // ========================================================================

    template <
            typename BaseModel,
            std::meta::info DistinctFieldInfo,
            std::meta::info OrderByFieldInfo,
            OrderDirection  Dir = OrderDirection::ASC,
            typename JoinCfg    = NoJoin,
            typename WhereCfg   = NoWhere>
    class DistinctOrderByBenchmark
        : public SelectQueryBenchmarkBase<
                  DistinctOrderByBenchmark<BaseModel, DistinctFieldInfo, OrderByFieldInfo, Dir, JoinCfg, WhereCfg>,
                  BaseModel,
                  JoinCfg,
                  WhereCfg,
                  NoLimitOffset,
                  OrderByConfig<OrderByFieldInfo, Dir>> {
        using Base = SelectQueryBenchmarkBase<
                DistinctOrderByBenchmark<BaseModel, DistinctFieldInfo, OrderByFieldInfo, Dir, JoinCfg, WhereCfg>,
                BaseModel,
                JoinCfg,
                WhereCfg,
                NoLimitOffset,
                OrderByConfig<OrderByFieldInfo, Dir>>;

      public:
        // Constructor - dataset_size only (no WHERE)
        explicit constexpr DistinctOrderByBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        auto print_info() const -> void {
            constexpr std::string_view distinct_field = std::meta::identifier_of(DistinctFieldInfo);
            std::cout << "Operation: SELECT DISTINCT " << distinct_field;
            // ORDER BY is printed by base class print_info_footer()
            Base::print_info_footer();
        }

        auto execute_iteration() -> int {
            // NOTE: Storm ORM handles ORDER BY via the base class apply_query_filters()
            // which was already called, so just call distinct().select()
            auto results = Base::qs().template distinct<DistinctFieldInfo>().select();
            return results.value().size();
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<BaseModel>();
            if (db == nullptr)
                return 0;

            constexpr std::string_view distinct_field = std::meta::identifier_of(DistinctFieldInfo);
            constexpr std::string_view order_field    = std::meta::identifier_of(OrderByFieldInfo);

            std::string sql = "SELECT DISTINCT ";
            sql += distinct_field;
            sql += " FROM Person ORDER BY ";
            sql += order_field;
            if constexpr (Dir == OrderDirection::DESC) {
                sql += " DESC";
            } else {
                sql += " ASC";
            }

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Determine field type at compile time
            using FieldType = std::remove_cvref_t<decltype(std::declval<BaseModel>().[:DistinctFieldInfo:])>;

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                if constexpr (std::is_same_v<FieldType, std::string>) {
                    plf::hive<std::string> results;
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
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
    // Type Aliases for Multi-Field and ORDER BY Configurations
    // ========================================================================

    // Simple 2-field DISTINCT
    template <typename Model, std::meta::info FieldInfo1, std::meta::info FieldInfo2>
    using SimpleDistinct2FieldBenchmark = Distinct2FieldBenchmark<Model, FieldInfo1, FieldInfo2, NoJoin, NoWhere>;

    // Simple 3-field DISTINCT
    template <typename Model, std::meta::info FieldInfo1, std::meta::info FieldInfo2, std::meta::info FieldInfo3>
    using SimpleDistinct3FieldBenchmark =
            Distinct3FieldBenchmark<Model, FieldInfo1, FieldInfo2, FieldInfo3, NoJoin, NoWhere>;

    // Simple DISTINCT + ORDER BY (ASC)
    template <typename Model, std::meta::info DistinctFieldInfo, std::meta::info OrderByFieldInfo>
    using SimpleDistinctOrderByAscBenchmark =
            DistinctOrderByBenchmark<Model, DistinctFieldInfo, OrderByFieldInfo, OrderDirection::ASC, NoJoin, NoWhere>;

    // Simple DISTINCT + ORDER BY (DESC)
    template <typename Model, std::meta::info DistinctFieldInfo, std::meta::info OrderByFieldInfo>
    using SimpleDistinctOrderByDescBenchmark =
            DistinctOrderByBenchmark<Model, DistinctFieldInfo, OrderByFieldInfo, OrderDirection::DESC, NoJoin, NoWhere>;

} // namespace storm::benchmark
