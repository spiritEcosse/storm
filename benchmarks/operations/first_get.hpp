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

        void print_info() const {
            std::cout << "Operation: FIRST";
            Base::print_info_footer();
        }

        int execute_iteration() {
            auto result = Base::qs().first().execute();
            return (result.has_value() && result.value().has_value()) ? 1 : 0;
        }

        int execute(int iterations) {
            return Base::execute_with_filters(iterations);
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            std::string sql = "SELECT id, name, age, is_active, salary FROM Person";

            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str      = WhereCfg::op.view();
                sql += " WHERE ";
                sql += std::string(where_field);
                sql += " ";
                sql += std::string(op_str);
                sql += " ?";
            }

            Base::append_order_by_sql(sql);
            sql += " LIMIT 1";

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
                // Fair comparison: wrap result in std::optional like Storm's first()
                std::optional<BaseModel> result;
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    BaseModel obj;
                    obj.id        = sqlite3_column_int64(stmt, 0);
                    obj.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    obj.age       = sqlite3_column_int(stmt, 2);
                    obj.is_active = sqlite3_column_int(stmt, 3) != 0;
                    obj.salary    = sqlite3_column_double(stmt, 4);
                    result.emplace(std::move(obj));
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

        void print_info() const {
            std::cout << "Operation: GET";
            Base::print_info_footer();
        }

        int execute_iteration() {
            auto result = Base::qs().get().execute();
            return result.has_value() ? 1 : 0;
        }

        int execute(int iterations) {
            return Base::execute_with_filters(iterations);
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            // get() uses LIMIT 2 internally to detect multiple rows
            std::string sql = "SELECT id, name, age, is_active, salary FROM Person";

            if constexpr (WhereCfg::enabled) {
                constexpr std::string_view where_field = std::meta::identifier_of(WhereCfg::field_info);
                constexpr std::string_view op_str      = WhereCfg::op.view();
                sql += " WHERE ";
                sql += std::string(where_field);
                sql += " ";
                sql += std::string(op_str);
                sql += " ?";
            }

            sql += " LIMIT 2";

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
                        obj.id        = sqlite3_column_int64(stmt, 0);
                        obj.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        obj.age       = sqlite3_column_int(stmt, 2);
                        obj.is_active = sqlite3_column_int(stmt, 3) != 0;
                        obj.salary    = sqlite3_column_double(stmt, 4);
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
