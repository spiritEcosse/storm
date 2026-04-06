#pragma once

/**
 * Benchmarks for first() and get() QuerySet methods
 *
 * first() - Returns first matching row with LIMIT 1, std::optional<T>
 * get()   - Returns exactly one row, errors on 0 or >1 rows (LIMIT 2 internally)
 *
 * Both are compared against equivalent raw SQLite for fair benchmarking.
 */

#include "select_query_base.hpp"
#include <string>

namespace storm::benchmark {

    // ========================================================================
    // FirstBenchmark - Benchmarks QuerySet::first() (LIMIT 1, returns optional)
    // ========================================================================
    template <typename BaseModel, typename WhereCfg = NoWhere, typename OrderByCfg = NoOrderBy>
    class FirstBenchmark : public SelectQueryBenchmarkBase<
                                   FirstBenchmark<BaseModel, WhereCfg, OrderByCfg>,
                                   BaseModel,
                                   NoJoin,
                                   WhereCfg,
                                   NoLimitOffset,
                                   OrderByCfg> {
        using Base = SelectQueryBenchmarkBase<
                FirstBenchmark<BaseModel, WhereCfg, OrderByCfg>,
                BaseModel,
                NoJoin,
                WhereCfg,
                NoLimitOffset,
                OrderByCfg>;

      public:
        // Basic (no WHERE) - dataset_size only
        explicit constexpr FirstBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // With WHERE - value + dataset_size
        template <typename V>
        explicit constexpr FirstBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(value, dataset_size) {}

        auto print_info() const -> void {
            std::cout << "Operation: FIRST";
            Base::print_info_footer();
        }

        auto execute_iteration() -> int {
            auto result = Base::qs().first().execute();
            return (result.has_value() && result.value().has_value()) ? 1 : 0;
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            // SQL from ORM — single source of truth
            QuerySet<BaseModel> qs_tmp;
            if constexpr (WhereCfg::enabled) {
                qs_tmp = qs_tmp.where(Base::build_where_clause());
            }
            std::string sql = qs_tmp.first().sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            if constexpr (WhereCfg::enabled) {
                Base::bind_where_value(stmt);
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                std::optional<BaseModel> result;
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    result.emplace(storm::benchmark::extract_row<BaseModel>(stmt));
                }
                if (result.has_value()) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

    // Type aliases for common first() configurations
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType>
    using FirstWhereBenchmark = FirstBenchmark<Model, WhereConfig<FieldInfo, Op, ValueType>>;

    template <typename Model, std::meta::info FieldInfo, OrderDirection Dir = OrderDirection::ASC>
    using FirstOrderByBenchmark = FirstBenchmark<Model, NoWhere, OrderByConfig<FieldInfo, Dir>>;

    // ========================================================================
    // GetBenchmark - Benchmarks QuerySet::get() (exactly one row, errors on 0 or >1)
    // ========================================================================
    template <typename BaseModel, typename WhereCfg = NoWhere>
    class GetBenchmark
        : public SelectQueryBenchmarkBase<GetBenchmark<BaseModel, WhereCfg>, BaseModel, NoJoin, WhereCfg> {
        using Base = SelectQueryBenchmarkBase<GetBenchmark<BaseModel, WhereCfg>, BaseModel, NoJoin, WhereCfg>;

      public:
        // Basic (no WHERE) - dataset_size only
        explicit constexpr GetBenchmark(int dataset_size = 1000)
            requires(!WhereCfg::enabled)
            : Base(dataset_size) {}

        // With WHERE - value + dataset_size
        template <typename V>
        explicit constexpr GetBenchmark(V value, int dataset_size = 1000)
            requires(WhereCfg::enabled)
            : Base(value, dataset_size) {}

        auto print_info() const -> void {
            std::cout << "Operation: GET";
            Base::print_info_footer();
        }

        auto execute_iteration() -> int {
            auto result = Base::qs().get().execute();
            return result.has_value() ? 1 : 0;
        }

        auto execute(int iterations) -> int {
            return Base::execute_with_filters(iterations);
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            // SQL from ORM — single source of truth (get() uses LIMIT 2 internally)
            QuerySet<BaseModel> qs_tmp;
            if constexpr (WhereCfg::enabled) {
                qs_tmp = qs_tmp.where(Base::build_where_clause());
            }
            std::string sql = qs_tmp.get().sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            if constexpr (WhereCfg::enabled) {
                Base::bind_where_value(stmt);
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                int       row_count = 0;
                BaseModel obj;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    if (row_count == 0) {
                        obj = storm::benchmark::extract_row<BaseModel>(stmt);
                    }
                    row_count++;
                }

                // get() semantics: exactly one row
                if (row_count == 1) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

    // Type alias for get() with WHERE
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType>
    using GetWhereBenchmark = GetBenchmark<Model, WhereConfig<FieldInfo, Op, ValueType>>;

} // namespace storm::benchmark
