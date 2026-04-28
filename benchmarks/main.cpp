/**
 * Storm ORM benchmarks — Google Benchmark entry point (Issue #235 — Phase 2).
 *
 * Slim gbench shim: owns <benchmark/benchmark.h>, builds the registration
 * table from register.cpp, then drives RunSpecifiedBenchmarks.
 *
 * No `import` statements here — that's strictly the job of register.cpp.
 * Mixing both sides in one TU diverges this TU's PCM-cache hash from
 * libstorm.a's, which produces a "module defined in both" fatal at any
 * `import storm` site (project_pcm_hash_divergence). The bridge is the
 * POD-only header bench_register.h.
 *
 * Each registered benchmark is wrapped in a stable lambda: setup() runs once
 * per Range step (state.range(0) chosen by gbench), then the inner
 * `for (auto _ : state)` loop indirect-calls the trampoline run() built by
 * register.cpp.
 */

#include <benchmark/benchmark.h>
#include "bench_register.h"

#include <cstdio>

namespace {

    auto wire_one(storm::benchmark::RegisteredBenchmark const& reg) -> void {
        // Capture by value — the RegisteredBenchmark vector is owned by
        // register.cpp's static and outlives main(), but copying keeps the
        // closure self-contained.
        auto* bm = benchmark::RegisterBenchmark(reg.name, [reg](benchmark::State& state) {
            reg.setup(state.range(0));
            for (auto _ : state) {
                reg.run();
            }
            state.SetComplexityN(state.range(0));
            state.SetItemsProcessed(state.iterations() * state.range(0));
        });

        if (reg.sized) {
            bm->RangeMultiplier(reg.range_multiplier)
                    ->Range(reg.range_lo, reg.range_hi)
                    ->Complexity(benchmark::oN)
                    ->ArgName("N");
        } else {
            bm->Arg(reg.range_lo)->ArgName("N");
        }
    }

} // namespace

auto main(int argc, char** argv) -> int {
    if (!storm::benchmark::initialize_db()) {
        std::fprintf(stderr, "storm_bench: failed to open :memory: DB / create schema\n");
        return 1;
    }

    storm::benchmark::build_registration_table();
    for (auto const& reg : storm::benchmark::registered_benchmarks()) {
        wire_one(reg);
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
