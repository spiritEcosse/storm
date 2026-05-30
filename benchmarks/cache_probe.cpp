// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
/**
 * cache_probe.cpp — four statement-caching cost scenarios (Issue #214).
 *
 * Measures the net cost (or benefit) of Storm's three-level statement cache
 * across four distinct access patterns:
 *
 *   CacheProbe/Reuse      — one QuerySet reused; WHERE value is constant →
 *                           maximum L1 + L2 cache benefit.
 *   CacheProbe/NewPerOp   — fresh QuerySet<Person> per iteration → no L1
 *                           benefit; cold statement on every op.
 *   CacheProbe/MixedWhere — one QuerySet, WHERE value rotates each iteration
 *                           → L1 hit (same QS) but L2/L3 interplay as SQL
 *                           string stays identical (only bind value changes).
 *   CacheProbe/BulkUpdate — one QuerySet, batch UPDATE over the seeded
 *                           dataset each iteration → exercises the hot
 *                           update path with L2 cache.
 *
 * Plain .cpp — NOT a .cppm.  Uses runtime benchmark::RegisterBenchmark() to
 * avoid the BENCHMARK() macro, which segfaults clang-p2996 inside
 * ASTWriter::GenerateNameLookupTable when serialising a module BMI
 * (feedback_benchmark_macro_in_module_purview).
 *
 * Preamble ordering follows register.cpp / crud_benchmark.cppm convention:
 *   1. <tuple> + models.hpp (textual, before any import) so reflection
 *      annotations on Person are visible in the GMF (clang-p2996 #262).
 *   2. <meta>  (textual; import std; does not export std::meta::).
 *   3. import storm; / import std; — module imports come after the above.
 *   4. <benchmark/benchmark.h> MUST live in its own TU side to avoid PCM-
 *      cache hash divergence; here we are a plain .cpp so both are fine.
 */

// Step 1 — textual includes that must precede any `import` statement.
// models.hpp does `import storm;` internally, which is fine: it fires the
// first time and subsequent `import storm;` below is a no-op.
#include <tuple>
#include "models.hpp"

// Step 2 — reflection header (not exported by import std;, Finding A #326).
#include <meta>

// Step 3 — module imports.
import storm;
import std;

// Step 4 — Google Benchmark (plain C++ header, no module conflict here).
#include <benchmark/benchmark.h>

using namespace storm;
using namespace storm::orm::where;
namespace gbench = ::benchmark; // disambiguate ::benchmark from storm::benchmark

// ---------------------------------------------------------------------------
// DB setup helpers
// ---------------------------------------------------------------------------

namespace {

    constexpr int kSeedRows = 1000;

    // Open ":memory:", create the Person table, and seed kSeedRows rows.
    // Called once per benchmark function invocation (before the loop).
    auto setup_db(std::vector<Person>& people) -> void {
        // Open connection (or reuse the thread-local one if already open).
        if (!QuerySet<Person>::has_default_connection()) {
            (void)QuerySet<Person>::set_default_connection(":memory:");
            const auto& conn = QuerySet<Person>::get_default_connection();
            (void)storm::orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
        }

        // Clear existing rows so each benchmark starts from a known state.
        QuerySet<Person> qs;
        (void)qs.erase_all().execute();

        // Build and insert the seed dataset.
        people.clear();
        people.reserve(kSeedRows);
        for (int i = 0; i < kSeedRows; ++i) {
            people.push_back(Person{
                    .name             = std::format("Person{}", i + 1),
                    .age              = 20 + (i % 50),
                    .salary           = 30000.0 + (i * 100.0),
                    .is_active        = (i % 2 == 0),
                    .years_experience = i % 20,
                    .department       = "Engineering",
            });
        }

        (void)qs.insert(std::span<const Person>(people)).execute();

        // Refresh people with their auto-assigned PKs for use in bulk-update.
        auto selected = qs.select().execute();
        if (selected.has_value()) {
            std::size_t idx = 0;
            for (const auto& row : selected.value()) {
                if (idx >= people.size()) {
                    break;
                }
                people[idx].id = row.id;
                ++idx;
            }
        }
    }

    // Execute a WHERE query and prevent the result from being optimised away.
    auto run_select(QuerySet<Person>& qs, storm::orm::where::ExpressionVariantPtr expr) -> void {
        auto result = qs.where(expr).select().execute();
        gbench::DoNotOptimize(result);
    }

} // namespace

namespace {

// ---------------------------------------------------------------------------
// Scenario 1: CacheProbe/Reuse
//
// One QuerySet<Person> reused across all iterations.  The WHERE expression
// is built once (age > 30, constant value) and the prepared statement is
// reused from the L1 cache on every call.  Maximum L1 benefit.
// ---------------------------------------------------------------------------
auto bench_reuse(gbench::State& state) -> void {
    std::vector<Person> people;
    setup_db(people);

    QuerySet<Person> qs;
    auto             expr = field<^^Person::age>() > 30;

    for (auto _ : state) {
        run_select(qs, expr);
    }
}

// ---------------------------------------------------------------------------
// Scenario 2: CacheProbe/NewPerOp
//
// Fresh QuerySet<Person> constructed inside the loop each iteration.  The L1
// Statement-pointer cache on the QuerySet is brand-new every time; the
// connection-level L3 cache still applies, but the per-QS L2 is cold.
// ---------------------------------------------------------------------------
auto bench_new_per_op(gbench::State& state) -> void {
    std::vector<Person> people;
    setup_db(people);

    auto expr = field<^^Person::age>() > 30;

    for (auto _ : state) {
        QuerySet<Person> qs;
        run_select(qs, expr);
    }
}

// ---------------------------------------------------------------------------
// Scenario 3: CacheProbe/MixedWhere
//
// One QuerySet, but the comparison value rotates each iteration (ages 20–69).
// The SQL string is structurally identical ("WHERE age > ?") so the L3
// prepare_cached() hit is the same, but the bind value differs every call.
// Isolates the bind-value-change overhead from statement recompilation cost.
// ---------------------------------------------------------------------------
auto bench_mixed_where(gbench::State& state) -> void {
    std::vector<Person> people;
    setup_db(people);

    QuerySet<Person> qs;
    int              age_val = 20;

    for (auto _ : state) {
        // Rotate the comparison value to prevent the compiler from
        // constant-folding this into a single cached path.
        auto expr = field<^^Person::age>() > age_val;
        age_val   = 20 + ((age_val - 19) % 50); // cycles 20..69
        run_select(qs, expr);
    }
}

// ---------------------------------------------------------------------------
// Scenario 4: CacheProbe/BulkUpdate
//
// One QuerySet, batch UPDATE over the seeded dataset each iteration.
// Uses update(std::span<const Person>) — the hot update path.  Exercises L2
// caching on the UpdateStatement across repeated identical-schema calls.
// ---------------------------------------------------------------------------
auto bench_bulk_update(gbench::State& state) -> void {
    std::vector<Person> people;
    setup_db(people);

    QuerySet<Person> qs;

    for (auto _ : state) {
        // Mutate salary so the UPDATE actually writes different values.
        for (auto& p : people) {
            p.salary += 1.0;
        }
        auto result = qs.update(std::span<const Person>(people)).execute();
        gbench::DoNotOptimize(result);
    }
}

// ---------------------------------------------------------------------------
// Scenario 5: CacheProbe/GetByPk
//
// Single-row primary-key lookup in a tight loop, returning exactly one row.
// Row materialization is ~1 row, so statement setup is a large fraction of
// total time — this is the workload Storm's docs attribute the ~23% L2
// statement-pointer-cache benefit to. The four scenarios above are all
// multi-row and under-sensitive to that cost; this one isolates it.
// ---------------------------------------------------------------------------
auto bench_get_by_pk(gbench::State& state) -> void {
    std::vector<Person> people;
    setup_db(people);

    QuerySet<Person> qs;
    std::size_t      idx = 0;

    for (auto _ : state) {
        // Rotate over real seeded PKs so every iteration fetches one existing row.
        const int pk = people[idx].id;
        idx          = (idx + 1) % people.size();
        auto result  = qs.where(field<^^Person::id>() == pk).get().execute();
        gbench::DoNotOptimize(result);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// main — register + run
// ---------------------------------------------------------------------------
auto main(int argc, char** argv) -> int { // NOLINT(bugprone-exception-escape)
    gbench::RegisterBenchmark("CacheProbe/Reuse", bench_reuse);
    gbench::RegisterBenchmark("CacheProbe/NewPerOp", bench_new_per_op);
    gbench::RegisterBenchmark("CacheProbe/MixedWhere", bench_mixed_where);
    gbench::RegisterBenchmark("CacheProbe/BulkUpdate", bench_bulk_update);
    gbench::RegisterBenchmark("CacheProbe/GetByPk", bench_get_by_pk);

    gbench::Initialize(&argc, argv);
    if (gbench::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    gbench::RunSpecifiedBenchmarks();
    gbench::Shutdown();
    return 0;
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
