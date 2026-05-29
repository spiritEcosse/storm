// storm_benchmark_crud
//
// CRUD-family benchmark fixture: INSERT / INSERT (no return) / UPDATE_PK /
// DELETE_PK driven by a BenchmarkTest NTTP.
//
// Issue #235 — Phase 3.
//
// Sibling of storm_benchmark_query (SELECT family). Storm-only path — the
// raw-SQLite comparisons from benchmarks/operations/{insert,update,delete}.hpp
// are intentionally dropped here; raw anchors land in their own TU in Phase 4.
//
// Per-iteration semantics that drive the run_once() shape:
//   * INSERT: Person has UNIQUE(name) — must clear the table before every
//     iteration so the next bind doesn't trip on the previous row.
//   * INSERT (no return): same constraint, same clear; uses the
//     ReturnId::No path so RETURNING is not generated.
//   * UPDATE_PK: rows seeded once in prepare(); each iteration re-issues the
//     same parameterised UPDATE — no clear needed.
//   * DELETE_PK: destructive; first iteration empties the table. Each
//     subsequent iteration must re-insert before deleting, otherwise we'd be
//     timing zero-row deletes.
//
// The fixture deliberately mirrors QueryBenchmark's prepare(N)/run_once()
// surface so register.cpp can route to either via the same Trampoline shape.

module;

// `sqlite3` typedef used in clear_table — base.cppm exports get_db() but the
// SQLite types remain hidden behind its BMI.
#include <sqlite3.h>

// `<tuple>` + models.hpp must precede any `import` so reflection annotations
// on Person/User/FKMessage are textually visible (clang-p2996 #262, see
// feedback_cpp26_module_reflection_annotations).
#include <tuple>

#include "models.hpp"

// std::meta:: is used in this module's purview; import std; does not export it
// (issue #326 Finding A), so the reflection header must be textually included.
#include <meta>

export module storm_benchmark_crud;

import std;

import storm;
import storm_benchmark_base;

export namespace storm::benchmark {

    template <typename Model, auto const& test>
    class CrudBenchmark : public DataBenchmarkBase<CrudBenchmark<Model, test>, Model> {
        using Base = DataBenchmarkBase<CrudBenchmark<Model, test>, Model>;

        static consteval auto is_insert_op() -> bool {
            constexpr auto op = test.operation.view();
            return op == "insert" || op == "insert_no_return";
        }
        static consteval auto is_insert_no_return_op() -> bool {
            return test.operation.view() == "insert_no_return";
        }
        static consteval auto is_update_op() -> bool {
            return test.operation.view() == "update_pk";
        }
        static consteval auto is_delete_op() -> bool {
            return test.operation.view() == "delete_pk";
        }

        static auto clear_table() -> void {
            sqlite3* db = get_db<Model>();
            if (db == nullptr) {
                return;
            }
            auto sql = std::format("DELETE FROM {}", std::meta::identifier_of(^^Model));
            sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        }

      public:
        explicit constexpr CrudBenchmark(int batch_size = 1) : Base(batch_size) {}

        // Same model factory shape as QueryBenchmark — Person varies fields by
        // index, others fall back to default-constructed.
        static auto create_model(int index = 0) -> Model {
            int i = index + 1;
            if constexpr (std::is_same_v<Model, Person>) {
                return Model{
                        .name      = std::format("Person{}", i),
                        .age       = 20 + (i % 50),
                        .salary    = 30000.0 + (i * 1000.0),
                        .is_active = (i % 2 == 0),
                };
            } else if constexpr (std::is_same_v<Model, User>) {
                return Model{.id = 0, .name = std::format("User{}", i), .age = 20 + (i % 50)};
            } else {
                return Model{};
            }
        }

        auto prepare(int n) -> void {
            if constexpr (is_insert_op()) {
                // INSERT seeds nothing — each iteration generates / inserts.
                // Generate the dataset once; run_once() clears + re-inserts.
                Base::prepare(n);
            } else if constexpr (is_update_op()) {
                Base::prepare_with_insert(n);
                // Mutate field values so each UPDATE actually rewrites rows.
                for (std::size_t i = 0; i < Base::data().size(); ++i) {
                    auto& obj = Base::data()[i];
                    if constexpr (std::is_same_v<Model, Person>) {
                        obj.name      = std::format("Updated{}", i);
                        obj.age       = obj.age + 5;
                        obj.salary    = obj.salary * 1.1;
                        obj.is_active = !obj.is_active;
                    }
                }
            } else if constexpr (is_delete_op()) {
                Base::prepare_with_insert(n);
            }
        }

        // Single benchmark iteration. Google Benchmark's loop body invokes
        // this once per `for (auto _ : state)` step.
        // Result expecteds are intentionally discarded — the benchmark times
        // execution, not consumption of the result.
        auto run_once() -> void {
            if constexpr (is_insert_no_return_op()) {
                clear_table();
                if (Base::batch_size() == 1) {
                    (void)Base::qs().template insert<storm::orm::statements::ReturnId::No>(Base::data()[0]).execute();
                } else {
                    (void)Base::qs().template insert<storm::orm::statements::ReturnId::No>(Base::data()).execute();
                }
            } else if constexpr (is_insert_op()) {
                clear_table();
                if (Base::batch_size() == 1) {
                    (void)Base::qs().insert(Base::data()[0]).execute();
                } else {
                    (void)Base::qs().insert(Base::data()).execute();
                }
            } else if constexpr (is_update_op()) {
                if (Base::batch_size() == 1) {
                    (void)Base::qs().update(Base::data()[0]).execute();
                } else {
                    (void)Base::qs().update(Base::data()).execute();
                }
            } else if constexpr (is_delete_op()) {
                // Re-seed before each DELETE iteration — the previous one
                // emptied the rows we'd otherwise time. Re-insert refreshes
                // PKs on Base::data() so the next erase() targets real rows.
                reinsert_for_delete();
                if (Base::batch_size() == 1) {
                    (void)Base::qs().erase(Base::data()[0]).execute();
                } else {
                    (void)Base::qs().erase(Base::data()).execute();
                }
            }
        }

      private:
        auto reinsert_for_delete() -> void {
            // Re-seed via the same path as the initial prepare_with_insert:
            // clear table → insert → select-back IDs. This sidesteps the
            // explicit-id INSERT path (which can produce SQLITE_CONSTRAINT
            // when the previous iteration left sqlite_sequence advanced).
            Base::prepare_with_insert(static_cast<int>(Base::data().size()));
        }
    };

} // namespace storm::benchmark
