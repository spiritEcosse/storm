#pragma once

/**
 * Set Operation Benchmarks - UNION, UNION ALL, EXCEPT, INTERSECT
 *
 * Unlike other benchmarks, set operations use SetOpBuilder instead of QuerySet.
 * Each benchmark creates two QuerySets with WHERE filters, combines them with
 * a set operation, then executes.
 *
 * WHERE conditions per operation type (ages range 21..69):
 * - UNION/UNION ALL: disjoint (age < 45, age >= 45) — combines both halves
 * - EXCEPT:          overlapping (age < 55, age > 35) — left minus overlap
 * - INTERSECT:       overlapping (age < 55, age > 35) — overlap only
 *
 * Storm side:  qs_left.where(...).union_(qs_right.where(...)).execute()
 * Raw side:    "SELECT ... WHERE ... UNION SELECT ... WHERE ..."
 */

#include "base.hpp"
#include <format>
#include <plf_hive/plf_hive.h>
#include <string>

namespace storm::benchmark {

    using storm::orm::where::field;

    // Set operation type enum for compile-time dispatch
    enum class SetOpBenchType { Union, UnionAll, Except, Intersect };

    template <typename Model, SetOpBenchType OpType, bool WithOrderBy = false, bool WithLimit = false>
    class SetOpBenchmark : public DataBenchmarkBase<SetOpBenchmark<Model, OpType, WithOrderBy, WithLimit>, Model, 1> {
        using Base = DataBenchmarkBase<SetOpBenchmark<Model, OpType, WithOrderBy, WithLimit>, Model, 1>;

        // Left WHERE: age <op> left_threshold_
        // Right WHERE: age <op> right_threshold_
        int left_threshold_  = 0;
        int right_threshold_ = 0;
        int limit_value_     = 0;
        QuerySet<Model> qs_right_;

        // For UNION/UNION ALL: disjoint halves (age < 45, age >= 45)
        // For EXCEPT/INTERSECT: overlapping ranges (age < 55, age > 35)
        static constexpr bool needs_overlap =
                (OpType == SetOpBenchType::Except || OpType == SetOpBenchType::Intersect);

      public:
        explicit SetOpBenchmark(int dataset_size = 1000, int limit_value = 0)
            : Base(dataset_size), limit_value_(limit_value) {
            if constexpr (needs_overlap) {
                left_threshold_  = 55;
                right_threshold_ = 35;
            } else {
                left_threshold_  = 45;
                right_threshold_ = 45;
            }
        }

        static Model create_model(int index = 0) {
            int i = index + 1;
            return Model{
                    .id        = 0,
                    .name      = std::format("Person{}", i),
                    .age       = 20 + (i % 50),
                    .is_active = (i % 2 == 0),
                    .salary    = 30000.0 + (i * 1000.0)
            };
        }

        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        void print_info() const {
            std::cout << "Operation: SELECT " << op_name();
            if constexpr (WithOrderBy) {
                std::cout << " + ORDER BY";
            }
            if constexpr (WithLimit) {
                std::cout << " + LIMIT " << limit_value_;
            }
            if constexpr (needs_overlap) {
                std::cout << " (WHERE age < " << left_threshold_ << " " << op_name() << " WHERE age > "
                          << right_threshold_ << ")";
            } else {
                std::cout << " (WHERE age < " << left_threshold_ << " " << op_name() << " WHERE age >= "
                          << right_threshold_ << ")";
            }
            std::cout << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        int execute(int iterations) {
            // Build WHERE expressions and set operation builder ONCE (setup cost)
            auto left_where = field<^^Model::age>() < left_threshold_;
            Base::qs().where(left_where);

            if constexpr (needs_overlap) {
                auto right_where = field<^^Model::age>() > right_threshold_;
                qs_right_.where(right_where);
            } else {
                auto right_where = field<^^Model::age>() >= right_threshold_;
                qs_right_.where(right_where);
            }

            auto builder = make_builder(Base::qs(), qs_right_);
            apply_modifiers(builder);

            // Execute in loop — statement caching kicks in after first call
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = builder.execute();
                if (result.has_value()) {
                    total += static_cast<int>(result.value().size());
                }
            }

            Base::qs().reset();
            qs_right_.reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            std::string sql = build_raw_sql();

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
            if (!stmt)
                return 0;

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_bind_int(stmt, 1, left_threshold_);
                sqlite3_bind_int(stmt, 2, right_threshold_);

                // Store results same as Storm ORM (plf::hive) for fair comparison
                std::vector<Model> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    Model obj;
                    obj.id        = sqlite3_column_int(stmt, 0);
                    auto name_ptr = sqlite3_column_text(stmt, 1);
                    obj.name      = name_ptr ? reinterpret_cast<const char*>(name_ptr) : "";
                    obj.age       = sqlite3_column_int(stmt, 2);
                    obj.is_active = sqlite3_column_int(stmt, 3) != 0;
                    obj.salary    = sqlite3_column_double(stmt, 4);
                    results.push_back(std::move(obj));
                }
                total += static_cast<int>(results.size());
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
            return total;
        }

      private:
        static constexpr const char* op_name() {
            if constexpr (OpType == SetOpBenchType::Union)
                return "UNION";
            else if constexpr (OpType == SetOpBenchType::UnionAll)
                return "UNION ALL";
            else if constexpr (OpType == SetOpBenchType::Except)
                return "EXCEPT";
            else
                return "INTERSECT";
        }

        static constexpr const char* op_sql() {
            if constexpr (OpType == SetOpBenchType::Union)
                return " UNION ";
            else if constexpr (OpType == SetOpBenchType::UnionAll)
                return " UNION ALL ";
            else if constexpr (OpType == SetOpBenchType::Except)
                return " EXCEPT ";
            else
                return " INTERSECT ";
        }

        auto make_builder(QuerySet<Model>& left, QuerySet<Model>& right) {
            if constexpr (OpType == SetOpBenchType::Union) {
                return left.union_(right);
            } else if constexpr (OpType == SetOpBenchType::UnionAll) {
                return left.union_all(right);
            } else if constexpr (OpType == SetOpBenchType::Except) {
                return left.except_(right);
            } else {
                return left.intersect_(right);
            }
        }

        template <typename Builder> void apply_modifiers(Builder& builder) {
            if constexpr (WithOrderBy) {
                builder.template order_by<^^Model::age, true>();
            }
            if constexpr (WithLimit) {
                builder.limit(limit_value_);
            }
        }

        std::string build_raw_sql() const {
            std::string sql = "SELECT id, name, age, is_active, salary FROM Person WHERE age < ?";
            sql += op_sql();
            if constexpr (needs_overlap) {
                sql += "SELECT id, name, age, is_active, salary FROM Person WHERE age > ?";
            } else {
                sql += "SELECT id, name, age, is_active, salary FROM Person WHERE age >= ?";
            }
            if constexpr (WithOrderBy) {
                sql += " ORDER BY age ASC";
            }
            if constexpr (WithLimit) {
                sql += " LIMIT ";
                sql += std::to_string(limit_value_);
            }
            return sql;
        }
    };

    // Type aliases for cleaner usage
    template <typename Model>
    using SetOpUnionBenchmark = SetOpBenchmark<Model, SetOpBenchType::Union>;

    template <typename Model>
    using SetOpUnionAllBenchmark = SetOpBenchmark<Model, SetOpBenchType::UnionAll>;

    template <typename Model>
    using SetOpExceptBenchmark = SetOpBenchmark<Model, SetOpBenchType::Except>;

    template <typename Model>
    using SetOpIntersectBenchmark = SetOpBenchmark<Model, SetOpBenchType::Intersect>;

    template <typename Model>
    using SetOpUnionOrderByBenchmark = SetOpBenchmark<Model, SetOpBenchType::Union, true>;

    template <typename Model, int LimitValue>
    using SetOpUnionLimitBenchmark = SetOpBenchmark<Model, SetOpBenchType::Union, false, true>;

} // namespace storm::benchmark
